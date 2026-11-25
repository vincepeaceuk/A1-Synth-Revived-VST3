#include "Editor.h"
#include "OriginalUi.h"
#include <cmath>

namespace a1 {
namespace {
using Images=std::array<juce::Image,21>;
const juce::Colour ink(0xffc4d5e2);

juce::Image loadArtwork(const juce::File& file,bool transparent) {
    auto image=juce::ImageFileFormat::loadFrom(file);
    if(!transparent || image.isNull())return image;
    image=image.convertedToFormat(juce::Image::ARGB);
    juce::Image::BitmapData pixels(image,juce::Image::BitmapData::readWrite);
    for(int y=0;y<pixels.height;++y)for(int x=0;x<pixels.width;++x)
        if(pixels.getPixelColour(x,y)==juce::Colours::white)pixels.setPixelColour(x,y,juce::Colours::transparentBlack);
    return image;
}
void sprite(juce::Graphics& g,const juce::Image& image,int x,int y,int width,int height,int sourceX=0,int sourceY=0) {
    if(image.isValid())g.drawImage(image,x,y,width,height,sourceX,sourceY,width,height,false);
}
juce::String number(float value) {
    const auto magnitude=std::abs(value);
    const int digits=magnitude>=1?int(std::floor(std::log10(magnitude)))+1:1;
    return digits>=4?juce::String(juce::roundToInt(value)):juce::String(value,4-digits);
}
juce::String valueText(const ui::ControlSpec& spec,float value,bool lfoSync) {
    switch(spec.display) {
        case 0:return {};
        case 1: {
            const float position=value*128;
            const int index=int(position);const float fraction=position-index;
            const auto frequency=[](int i) {return float(440*std::pow(2.0,(juce::jmin(i,128)-64)/12.0));};
            return number(frequency(index)*(1-fraction)+frequency(index+1)*fraction)+"Hz";
        }
        case 2: {const float bipolar=value*2-1;return number(std::copysign(100*std::pow(bipolar,4),bipolar))+"%";}
        case 3: {
            constexpr const char* divisions[]={"8 bars","4 bars","2 bars","1 bar","1/2","1/4","1/8","1/16","1/32","1/64","1/2.","1/4.","1/8.","1/16.","1/32.","1/64.","1/1t","1/2t","1/4t","1/8t","1/16t","1/32t","1/64t"};
            return lfoSync?juce::String(divisions[juce::jlimit(0,22,int(value*22))]):number(float(0.01*std::pow(100000.0,value)))+"Hz";
        }
        case 4:return value==1?"1/2'":juce::String(1<<(7-int(value*8)))+"'";
        case 5:return juce::String(int(value*24+0.1f)-12);
        case 6:return number(float(100*std::pow(value*2-1,3)))+"ct";
        case 7:case 8:return juce::String(int(value*spec.steps+1.1f));
        case 9:return number(float(100*std::pow(value,4)))+"%";
        case 10:return number(50*(value+1))+"%";
        case 11:case 13:case 17:return number(value*100)+"%";
        case 12:return (value<=0?juce::String("-oo"):number(float(20*std::log10(500*std::pow(value,5)+1))))+"dB";
        case 14:return (value==0?juce::String("0"):number(float(0.001*std::pow(60000.0,value))))+"s";
        case 15:return (value<=0?juce::String("-oo"):number(float(60*std::log10(value))))+"dB";
        case 16:return value<=0?juce::String("Off"):number(float(0.01*std::pow(500.0,value)))+"Hz";
        case 18:case 20:return value>0?"On":"Off";
        case 19:return value==1?"Mono":"Poly";
        case 21: {
            constexpr const char* shapes[]={"Sine","Triangle","Saw","Square","S&H","Random"};
            return shapes[juce::jlimit(0,5,int(value*5))];
        }
        case 22: {
            constexpr const char* shapes[]={"Sine","Triangle","Sawtooth","Pulse","Pulse"};
            return shapes[juce::jlimit(0,4,int(value*4))];
        }
        case 23: {
            constexpr const char* filters[]={"Low Pass","High Pass","Band Pass","Notch"};
            return filters[juce::jlimit(0,3,int(value*3))];
        }
        case 24:return value==1?"24dB":"12dB";
        default:return {};
    }
}

class Control final : public juce::Slider {
public:
    Control(const ui::ControlSpec& s,const Images& images):spec(s),images(images) {
        setComponentID("a1-param-"+juce::String(spec.parameter));
        setName(juce::String(ui::sectionName(spec.section))+" "+spec.name);
        setTitle(getName());setDescription(getName());setTooltip(getName());
        setSliderStyle(RotaryHorizontalVerticalDrag);setTextBoxStyle(NoTextBox,false,0,0);
        setRotaryParameters(1.25f*juce::MathConstants<float>::pi,2.75f*juce::MathConstants<float>::pi,true);
        // Preserve stored values exactly; quantise only an actual user drag.
        setRange(0,1,0);
        setMouseDragSensitivity(200);
        setDoubleClickReturnValue(true,spec.defaultValue,juce::ModifierKeys::commandModifier);
        setWantsKeyboardFocus(false);setBounds(spec.bounds());
    }
    const ui::ControlSpec& spec;
    std::function<void(int)> inspect;
    bool hitTest(int x,int y) override {
        if(spec.type!=3)return Slider::hitTest(x,y);
        const float dx=x-13.0f,dy=y-13.0f;
        return dx*dx+dy*dy<=11.5f*11.5f;
    }
    void paint(juce::Graphics& g) override {
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        const auto& image=images[size_t(spec.resource-128)];
        const auto v=float(getValue());
        if(spec.type<=3) {
            const int frame=juce::jlimit(0,62,int(v*62));
            sprite(g,image,0,0,spec.width,spec.height,0,frame*spec.height);
        } else if(spec.type==4) {
            sprite(g,image,0,0,35,35);
            const double angle=3.9269907474517822-4.71238899230957*v;
            sprite(g,images[17],int(17.5+14.5*std::cos(angle)+0.5)-2,int(17.5-14.5*std::sin(angle)+0.5)-2,5,5);
        } else if(spec.type>=5 && spec.type<=9) {
            const int selected=juce::jlimit(0,spec.steps,int(v*spec.steps+0.0001f));
            const int column=spec.type==5?0:selected%2;
            const int row=spec.type==5?selected:selected/2;
            const int width=spec.type==5?22:23;
            const int x=column*width,y=row*15;
            const int height=juce::jmin(14,getHeight()-y);
            if(height>0)sprite(g,image,x,y,width,height,x,y);
        } else if(spec.type==14) {
            sprite(g,image,0,juce::roundToInt((1-v)*80),14,27);
        } else if(spec.type==18) {
            sprite(g,image,0,0,juce::jmin(image.getWidth(),getWidth()),50,0,juce::jlimit(0,63,int(v*63))*50);
        } else if(spec.type==19 || spec.type==20) {
            g.setColour(ink);g.setFont(juce::Font(juce::FontOptions("Arial",10.0f,juce::Font::plain)));
            g.drawText(valueText(spec,v,false),getLocalBounds(),juce::Justification::centred,false);
        } else {
            const bool on=v>0;
            sprite(g,image,0,0,getWidth(),getHeight(),0,on?getHeight():0);
        }
    }
    void mouseEnter(const juce::MouseEvent& e) override {if(inspect)inspect(spec.parameter);Slider::mouseEnter(e);}
    void mouseDown(const juce::MouseEvent& e) override {
        if(inspect)inspect(spec.parameter);
        if(e.mods.isCtrlDown() || e.mods.isCommandDown()) {mouseDoubleClick(e);return;}
        if(isSelector() || spec.type==18 || spec.type==14) {
            gesture=std::make_unique<ScopedDragNotification>(*this);
            if(isSelector()) {
                if(spec.type>=5 && spec.type<=9) {
                    const int item=spec.type==5?int(e.y/15):int(e.y/15)*2+(e.x>=23?1:0);
                    if(item<=spec.steps)setValue(double(item)/spec.steps,juce::sendNotificationSync);
                } else setValue(getValue()>0?0:1,juce::sendNotificationSync);
            } else setFromVerticalPosition(e.y);
        } else {
            setSliderStyle(e.mods.isAltDown()?Rotary:RotaryHorizontalVerticalDrag);
            setMouseDragSensitivity(e.mods.isShiftDown()?300:200);Slider::mouseDown(e);
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(spec.type==18 || spec.type==14)setFromVerticalPosition(e.y);
        else if(!isSelector()) {setMouseDragSensitivity(e.mods.isShiftDown()?300:200);Slider::mouseDrag(e);}
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if(spec.parameter==1)setValue(0.5,juce::sendNotificationSync);
        if(gesture)gesture.reset();else Slider::mouseUp(e);
    }
    void mouseDoubleClick(const juce::MouseEvent&) override {
        ScopedDragNotification changing(*this);setValue(spec.defaultValue,juce::sendNotificationSync);
    }
    double snapValue(double attempted,DragMode) override {
        return spec.steps>0?std::round(attempted*spec.steps)/spec.steps:attempted;
    }
private:
    const Images& images;
    std::unique_ptr<ScopedDragNotification> gesture;
    bool isSelector() const {return (spec.type>=5 && spec.type<=13) || spec.type==17;}
    void setFromVerticalPosition(float y) {
        const float travel=float(getHeight()-(spec.type==14?27:0));
        const float offset=spec.type==14?13.5f:0;
        setValue(juce::jlimit(0.0f,1.0f,1-(y-offset)/travel),juce::sendNotificationSync);
    }
};

class Envelope final : public juce::Component {
public:
    Envelope(Processor& p,int first,const Images& images,std::function<void(int)> inspect)
      :processor(p),first(first),images(images),inspect(std::move(inspect)) {
        setComponentID("a1-envelope-"+juce::String(first));
        setName(first==43?"Filter envelope":"Amplifier envelope");setTitle(getName());
        setBounds(first==43?345:555,190,184,78);setWantsKeyboardFocus(false);
        for(int i=0;i<4;++i)last[size_t(i)]=processor.parameters[size_t(first+i)]->getValue();
    }
    ~Envelope() override {endDrag();}
    void refresh() {
        bool changed=false;
        for(int i=0;i<4;++i) {
            const auto v=processor.parameters[size_t(first+i)]->getValue();
            changed|=last[size_t(i)]!=v;last[size_t(i)]=v;
        }
        if(changed)repaint();
    }
    void paint(juce::Graphics& g) override {
        const auto points=geometry();
        g.setColour(juce::Colour(0xff7696b1));
        juce::Path line;line.startNewSubPath(points[0]);
        line.lineTo(points[1]);
        const auto exponential=[&](juce::Point<float> start,juce::Point<float> end) {
            const int width=int(end.x-start.x);
            for(int x=1;x<width;++x) {
                const float curve=(std::pow(10.0f,-2.0f*x/width)-0.01f)/0.99f;
                line.lineTo(start.x+x,end.y+(start.y-end.y)*curve);
            }
            line.lineTo(end);
        };
        exponential(points[1],points[2]);line.lineTo(points[3]);exponential(points[3],points[4]);
        g.setColour(juce::Colour(0xff001423));g.strokePath(line,juce::PathStrokeType(1),juce::AffineTransform::translation(1,1));
        g.setColour(juce::Colour(0xff7696b1));g.strokePath(line,juce::PathStrokeType(1));
        for(size_t i=1;i<points.size();++i)
            sprite(g,images[15],juce::roundToInt(points[i].x)-2,juce::roundToInt(points[i].y)-2,5,5);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        endDrag();if(e.mods.getRawFlags()!=juce::ModifierKeys::leftButtonModifier)return;
        const auto points=geometry();
        for(int i=4;i>0;--i)if(std::abs(e.x-points[size_t(i)].x)<=3 && std::abs(e.y-points[size_t(i)].y)<=3) {drag=i-1;break;}
        if(drag<0) {
            drag=3;for(int i=1;i<5;++i)if(e.x<=points[size_t(i)].x) {drag=i-1;break;}
        }
        if(drag>=0) {
            processor.parameters[size_t(first+drag)]->beginChangeGesture();
            if(inspect)inspect(first+drag);
            mouseDrag(e);
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(drag<0 || e.mods.getRawFlags()!=juce::ModifierKeys::leftButtonModifier)return;
        auto edit=[this](int part,float value) {processor.parameters[size_t(first+part)]->setValueNotifyingHost(juce::jlimit(0.0f,1.0f,value));};
        const auto points=geometry();
        if(drag==0)edit(0,(e.x-2)/59.0f);
        if(drag==1)edit(1,(e.x-points[1].x)/59);
        if(drag==2)edit(2,1-(e.y-2)/73.0f);
        if(drag==3)edit(3,(e.x-points[3].x)/59);
        refresh();
    }
    void mouseUp(const juce::MouseEvent&) override {endDrag();}
private:
    Processor& processor;
    int first,drag=-1;
    const Images& images;
    std::function<void(int)> inspect;
    std::array<float,4> last{};
    std::array<juce::Point<float>,5> geometry() const {
        const int a=int(last[0]*59),d=int(last[1]*59),s=int((1-last[2])*73),r=int(last[3]*59);
        return {{{2,75},{float(2+a),2},{float(2+a+d),float(2+s)},{120,float(2+s)},{float(120+r),75}}};
    }
    void endDrag() {
        if(drag<0)return;
        processor.parameters[size_t(first+drag)]->endChangeGesture();
        drag=-1;
    }
};

class Keyboard final : public juce::Component {
public:
    Keyboard(juce::MidiKeyboardState& state,const juce::Image& image):state(state),image(image) {
        setComponentID("a1-keyboard");setName("A1 keyboard");setTitle(getName());
        setBounds(128,354,514,49);setWantsKeyboardFocus(false);
    }
    ~Keyboard() override {release();}
    void paint(juce::Graphics& g) override {
        for(int note=36;note<=96;++note)if(state.isNoteOnForChannels(0xffff,note)) {
            const auto top=keyRect(note,false);const int octave=(note/12-3)*100;
            sprite(g,image,top.getX(),0,top.getWidth(),31,note==96?100:top.getX()-octave,0);
            if(!isBlack(note)) {
                const auto bottom=keyRect(note,true);
                sprite(g,image,bottom.getX(),31,bottom.getWidth(),18,note==96?100:bottom.getX()-octave,31);
            }
        }
    }
    void refresh() {
        uint64_t notes=0;for(int i=0;i<61;++i)if(state.isNoteOnForChannels(0xffff,i+36))notes|=uint64_t(1)<<i;
        if(notes!=last) {last=notes;repaint();}
    }
    void mouseDown(const juce::MouseEvent& e) override {play(e.position);}
    void mouseDrag(const juce::MouseEvent& e) override {play(e.position);}
    void mouseUp(const juce::MouseEvent&) override {release();}
private:
    juce::MidiKeyboardState& state;
    const juce::Image& image;
    int held=-1;
    uint64_t last=0;
    static bool isBlack(int note) {const int pc=note%12;return pc==1 || pc==3 || pc==6 || pc==8 || pc==10;}
    static juce::Rectangle<int> keyRect(int note,bool lower) {
        constexpr int top[]={0,10,19,24,33,43,53,62,67,76,81,90,100};
        constexpr int bottomLeft[]={0,0,15,0,29,43,0,58,0,72,0,86};
        constexpr int bottomWidth[]={15,0,14,0,14,15,0,14,0,14,0,14};
        if(note==96)return {500,lower?31:0,14,lower?18:31};
        const int pc=note%12,octave=(note/12-3)*100;
        if(lower)return {octave+bottomLeft[pc],31,bottomWidth[pc],18};
        return {octave+top[pc],0,top[pc+1]-top[pc],31};
    }
    void play(juce::Point<float> point) {
        int found=-1;
        if(getLocalBounds().toFloat().contains(point)) {
            for(int n=36;n<=96;++n)if(keyRect(n,point.y>=31).toFloat().contains(point)) {found=n;break;}
        }
        if(found==held)return;
        release();held=found;
        if(held>=0)state.noteOn(1,held,1.0f);refresh();
    }
    void release() {if(held>=0) {state.noteOff(1,held,0);held=-1;refresh();}}
};

class AboutPanel final : public juce::Component {
public:
    explicit AboutPanel(const juce::Image& artwork):artwork(artwork) {
        setComponentID("a1-about-panel");setName("Original A1 credits");setTitle(getName());
        setBounds(0,0,769,426);setWantsKeyboardFocus(true);
    }
    void paint(juce::Graphics& g) override {g.drawImageAt(artwork,(getWidth()-artwork.getWidth())/2,(getHeight()-artwork.getHeight())/2);}
    void mouseDown(const juce::MouseEvent&) override {setVisible(false);}
    bool keyPressed(const juce::KeyPress& key) override {
        if(key==juce::KeyPress::escapeKey) {setVisible(false);return true;}return false;
    }
private:
    const juce::Image& artwork;
};
class AboutLink final : public juce::Component {
public:
    explicit AboutLink(std::function<void()> open):open(std::move(open)) {setName("A1 credits");setTitle(getName());setMouseCursor(juce::MouseCursor::PointingHandCursor);}
    void mouseUp(const juce::MouseEvent&) override {open();}
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(*this,juce::AccessibilityRole::button,
            juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,[this]{open();}));
    }
private:
    std::function<void()> open;
};
}

struct Editor::Impl {
    Impl(Editor& owner,Processor& p,const juce::File& resources):owner(owner),processor(p) {
        for(int i=0;i<21;++i)images[size_t(i)]=loadArtwork(resources.getChildFile("OriginalUI/BITMAP_"+juce::String(128+i)+".png"),i>0 && i<20);
        for(const auto& spec:ui::controls) {
            auto control=std::make_unique<Control>(spec,images);
            auto* slider=control.get();const int parameter=spec.parameter;
            control->setValue(p.parameters[size_t(parameter)]->getValue(),juce::dontSendNotification);
            control->inspect=[this](int index){inspect(index);};
            control->onDragStart=[this,parameter]{processor.parameters[size_t(parameter)]->beginChangeGesture();};
            control->onDragEnd=[this,parameter]{processor.parameters[size_t(parameter)]->endChangeGesture();};
            control->onValueChange=[this,slider,parameter] {
                processor.parameters[size_t(parameter)]->setValueNotifyingHost(float(slider->getValue()));inspect(parameter);
            };
            owner.addAndMakeVisible(*control);controls.push_back(std::move(control));
        }
        for(const int first:{43,47}) {
            auto graph=std::make_unique<Envelope>(p,first,images,[this](int index){inspect(index);});
            owner.addAndMakeVisible(*graph);envelopes.push_back(std::move(graph));
        }
        keyboard=std::make_unique<Keyboard>(p.keyboard,images[16]);owner.addAndMakeVisible(*keyboard);
        about=std::make_unique<AboutPanel>(images[20]);owner.addChildComponent(*about);
        for(const auto area:{juce::Rectangle<int>(111,178,164,23),juce::Rectangle<int>(659,367,85,27)}) {
            auto link=std::make_unique<AboutLink>([this] {about->setVisible(true);about->toFront(true);});
            link->setBounds(area);if(links.empty())link->setComponentID("a1-about");
            owner.addAndMakeVisible(*link);links.push_back(std::move(link));
        }
        inspect(34);
    }
    Editor& owner;
    Processor& processor;
    Images images;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<std::unique_ptr<Envelope>> envelopes;
    std::unique_ptr<Keyboard> keyboard;
    std::unique_ptr<AboutPanel> about;
    std::vector<std::unique_ptr<AboutLink>> links;
    int inspected=34;
    juce::String displayed;
    void updateReadout() {
        const auto* spec=ui::control(inspected);if(!spec)return;
        const auto text=juce::String(ui::sectionName(spec->section))+" / "+spec->name+" / "
            +valueText(*spec,processor.parameters[size_t(inspected)]->getValue(),processor.parameters[9]->getValue()>0)
            +" / MIDI CC "+(spec->midiCC>=0?juce::String(spec->midiCC):"-");
        if(text!=displayed) {displayed=text;owner.setDescription(text);owner.repaint(336,135,210,18);}
    }
    void inspect(int parameter) {inspected=parameter;updateReadout();}
    void refresh() {
        for(auto& control:controls)if(!control->isMouseButtonDown()) {
            const auto v=processor.parameters[size_t(control->spec.parameter)]->getValue();
            if(control->getValue()!=v)control->setValue(v,juce::dontSendNotification);
        }
        for(auto& graph:envelopes)graph->refresh();keyboard->refresh();
        updateReadout();
    }
};
Editor::Editor(Processor& p,const juce::File& resources):AudioProcessorEditor(p),impl(std::make_unique<Impl>(*this,p,resources)) {
    setOpaque(true);setSize(769,426);setResizable(false,false);setWantsKeyboardFocus(false);startTimerHz(30);
}
Editor::~Editor() {stopTimer();impl.reset();}
void Editor::paint(juce::Graphics& g) {
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    if(impl->images[0].isNull()) {
        g.fillAll(juce::Colour(0xff092034));g.setColour(juce::Colours::white);
        g.drawFittedText("A1 original artwork is missing. Reinstall the complete plug-in bundle.",getLocalBounds().reduced(30),juce::Justification::centred,3);return;
    }
    g.drawImageAt(impl->images[0],0,0);
    if(const auto* spec=ui::control(impl->inspected)) {
        const auto v=impl->processor.parameters[size_t(spec->parameter)]->getValue();
        g.setColour(ink);g.setFont(juce::Font(juce::FontOptions("Arial",9.0f,juce::Font::plain)));
        const auto text=[&](juce::String value,int x,int width) {g.drawText(value,x,137,width,14,juce::Justification::centred,false);};
        text(ui::sectionName(spec->section),342,60);text(spec->name,402,73);text(valueText(*spec,v,impl->processor.parameters[9]->getValue()>0),475,49);
        text(spec->midiCC>=0?juce::String(spec->midiCC):"-",524,20);
    }
}
int Editor::getControlParameterIndex(juce::Component& component) {
    for(const auto& control:impl->controls)if(control.get()==&component)return control->spec.parameter;
    return -1;
}
void Editor::timerCallback() {impl->refresh();}
}
