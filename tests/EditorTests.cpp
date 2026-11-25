#include "Processor.h"
#include <iostream>
#include <thread>

namespace {
juce::Component* find(juce::Component& root,const juce::String& id) {
    if(root.getComponentID()==id)return &root;
    for(auto* child:root.getChildren())if(auto* found=find(*child,id))return found;
    return nullptr;
}
struct Listener : juce::AudioProcessorParameter::Listener {
    int changes=0,begins=0,ends=0;
    void parameterValueChanged(int,float) override {++changes;}
    void parameterGestureChanged(int,bool starting) override {starting?++begins:++ends;}
};
juce::MouseEvent event(juce::Component& c,float x,float y,int modifiers=juce::ModifierKeys::leftButtonModifier) {
    return {juce::Desktop::getInstance().getMainMouseSource(),{x,y},juce::ModifierKeys(modifiers),1,0,0,0,0,
            &c,&c,juce::Time::getCurrentTime(),{x,y},juce::Time::getCurrentTime(),1,false};
}
juce::MouseEvent dragEvent(juce::Component& c,float x,float y,float startX,float startY,int modifiers=juce::ModifierKeys::leftButtonModifier) {
    return {juce::Desktop::getInstance().getMainMouseSource(),{x,y},juce::ModifierKeys(modifiers),1,0,0,0,0,
            &c,&c,juce::Time::getCurrentTime(),{startX,startY},juce::Time::getCurrentTime(),1,true};
}
void refresh() {
    // The console fixture has no host message loop. Service more than one timer
    // turn so replacing an editor cannot leave the new timer behind an old post.
    for(int i=0;i<4;++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));juce::Timer::callPendingTimersSynchronously();
    }
}
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    juce::ScopedJuceInitialiser_GUI init;
    a1::Processor p(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
    int failures=0,checks=0;
    auto check=[&](bool ok,const char* label) {++checks;std::cout<<(ok?"PASS: ":"FAIL: ")<<label<<'\n';if(!ok)++failures;};
    const auto initialLatency=p.getLatencySamples();
    std::unique_ptr<juce::AudioProcessorEditor> ui(p.createEditor());
    check(ui->getWidth()==769 && ui->getHeight()==426,"editor restores the original 769 by 426 canvas");
    bool mapped=true;
    for(int id=1;id<=58;++id)if(id!=3) {
        auto* control=find(*ui,"a1-param-"+juce::String(id));
        mapped&=control && ui->getControlParameterIndex(*control)==id && ui->getLocalBounds().contains(control->getBounds());
    }
    check(mapped,"every original control including pitch, mod, filter slope and mod-wheel cutoff maps to its parameter");
    auto* cutoff=dynamic_cast<juce::Slider*>(find(*ui,"a1-param-34"));
    check(cutoff && cutoff->getBounds()==juce::Rectangle<int>(345,26,32,32),"cutoff occupies its recovered original knob position");
    Listener listener;p.parameters[34]->addListener(&listener);
    if(cutoff) {
        cutoff->setValue(0.375,juce::sendNotificationSync);
        check(p.parameters[34]->getValue()==0.375f && listener.changes==1,"visible knob edits reach the processor and host");
        cutoff->mouseDoubleClick(event(*cutoff,16,16));
        check(p.parameters[34]->getValue()==1.0f,"double click restores the original control default");
    } else {check(false,"visible knob edits reach the processor and host");check(false,"double click restores the original control default");}
    auto* shape=find(*ui,"a1-param-14");
    if(shape) {shape->mouseDown(event(*shape,34,21));shape->mouseUp(event(*shape,34,21));}
    check(p.parameters[14]->getValue()==0.75f,"oscillator shape buttons select the clicked waveform");
    auto* slope=find(*ui,"a1-param-42");
    if(slope) {slope->mouseDown(event(*slope,35,6));slope->mouseUp(event(*slope,35,6));}
    check(slope && p.parameters[42]->getValue()==1.0f,"original filter slope buttons reach the previously hidden parameter");
    auto* semitone=find(*ui,"a1-param-16");
    check(semitone && !semitone->hitTest(0,0) && semitone->hitTest(13,13),"inner semitone control leaves the outer octave ring reachable");
    auto* pitch=find(*ui,"a1-param-1");
    if(pitch) {pitch->mouseDown(event(*pitch,5,2));pitch->mouseUp(event(*pitch,5,2));}
    check(pitch && p.parameters[1]->getValue()==0.5f,"pitch wheel springs back to its centre when released");
    auto* graph=find(*ui,"a1-envelope-47");
    check(graph && graph->getWidth()>180 && graph->getHeight()>70,"amplifier envelope has its original editable graph");
    for(int i=0;i<4;++i)p.parameters[size_t(47+i)]->setValueNotifyingHost(std::array<float,4>{0.5f,0.25f,0.75f,1.0f}[size_t(i)]);
    refresh();
    if(graph) {
        graph->mouseDown(event(*graph,31,2));graph->mouseDrag(dragEvent(*graph,49,2,31,2));graph->mouseUp(event(*graph,49,2));
    }
    check(graph && std::abs(p.parameters[47]->getValue()-47.0f/59)<0.00001f,"envelope handle drag uses the original 59-pixel time scale");
    const auto resetEnvelope=[&] {
        for(int i=0;i<4;++i)p.parameters[size_t(47+i)]->setValueNotifyingHost(std::array<float,4>{0.5f,0.25f,0.75f,1.0f}[size_t(i)]);
        refresh();
    };
    resetEnvelope();Listener decayGesture,sustainGesture;
    p.parameters[48]->addListener(&decayGesture);p.parameters[49]->addListener(&sustainGesture);
    if(graph) {graph->mouseDown(event(*graph,45,20));graph->mouseDrag(dragEvent(*graph,60,40,45,20));graph->mouseUp(event(*graph,60,40));}
    check(graph && std::abs(p.parameters[48]->getValue()-29.0f/59)<0.00001f && p.parameters[49]->getValue()==0.75f,
          "decay handle changes time without changing sustain");
    check(decayGesture.begins==1 && decayGesture.ends==1 && sustainGesture.begins==0,"envelope automation gestures identify only the edited stage");
    p.parameters[48]->removeListener(&decayGesture);p.parameters[49]->removeListener(&sustainGesture);
    resetEnvelope();
    if(graph) {graph->mouseDown(event(*graph,120,20));graph->mouseDrag(dragEvent(*graph,120,60,120,20));graph->mouseUp(event(*graph,120,60));}
    check(graph && std::abs(p.parameters[49]->getValue()-15.0f/73)<0.00001f,"sustain handle follows the original graph height");
    resetEnvelope();
    if(graph) {graph->mouseDown(event(*graph,179,75));graph->mouseDrag(dragEvent(*graph,150,75,179,75));graph->mouseUp(event(*graph,150,75));}
    check(graph && std::abs(p.parameters[50]->getValue()-30.0f/59)<0.00001f,"release handle follows the original final time region");
    resetEnvelope();
    if(graph) {graph->mouseDown(event(*graph,31,2,juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::shiftModifier));graph->mouseUp(event(*graph,31,2));}
    check(p.parameters[47]->getValue()==0.5f,"original envelope modifier behavior leaves the graph unchanged");
    auto* mod=find(*ui,"a1-param-2");
    if(mod) {mod->mouseDown(event(*mod,5,0));mod->mouseUp(event(*mod,5,0));}
    check(mod && p.parameters[2]->getValue()==1.0f,"modulation wheel retains its position after release");
    bool editIsolation=true,gestures=true;
    for(int id=1;id<=58;++id)if(id!=3) {
        auto* slider=dynamic_cast<juce::Slider*>(find(*ui,"a1-param-"+juce::String(id)));
        if(!slider) {editIsolation=false;continue;}
        std::array<float,99> before{};for(int j=0;j<99;++j)before[size_t(j)]=p.parameters[size_t(j)]->getValue();
        Listener observer;p.parameters[size_t(id)]->addListener(&observer);
        const double target=slider->getValue()<0.5?1:0;
        if(slider->onDragStart)slider->onDragStart();
        slider->setValue(target,juce::sendNotificationSync);
        if(slider->onDragEnd)slider->onDragEnd();
        gestures&=observer.begins==1 && observer.ends==1 && observer.changes==1;
        editIsolation&=p.parameters[size_t(id)]->getValue()==target;
        for(int j=0;j<99;++j)if(j!=id)editIsolation&=before[size_t(j)]==p.parameters[size_t(j)]->getValue();
        p.parameters[size_t(id)]->removeListener(&observer);
    }
    check(editIsolation,"each of 57 visible controls edits only its original parameter");
    check(gestures,"every control brackets edits with balanced host automation gestures");
    auto* keys=find(*ui,"a1-keyboard");
    if(keys) {keys->mouseDown(event(*keys,6,44));}
    check(keys && p.keyboard.isNoteOn(1,36),"original keyboard starts at MIDI note 36 and plays on-screen notes");
    if(keys)keys->mouseDrag(dragEvent(*keys,20,44,6,44));
    check(keys && !p.keyboard.isNoteOn(1,36) && p.keyboard.isNoteOn(1,38),"keyboard glissando releases the previous note");
    if(keys)keys->mouseUp(event(*keys,6,44));
    check(keys && !p.keyboard.isNoteOn(1,38),"releasing the original keyboard sends note off");
    if(keys)keys->mouseDown(event(*keys,14,10));
    check(keys && p.keyboard.isNoteOn(1,37),"original upper key hit regions select black keys");
    if(keys)keys->mouseUp(event(*keys,14,10));
    if(keys)keys->mouseDown(event(*keys,512,10));
    check(keys && p.keyboard.isNoteOn(1,96),"five octave keyboard includes its final high C");
    if(keys)keys->mouseUp(event(*keys,507,44));
    p.keyboard.noteOn(1,60,1);p.keyboard.noteOn(1,61,1);refresh();
    const auto played=keys?keys->createComponentSnapshot(keys->getLocalBounds()):juce::Image();
    check(played.isValid() && played.getPixelAt(204,40).getAlpha()>0 && played.getPixelAt(214,10).getAlpha()>0,
          "incoming MIDI paints the original played white and black key artwork");
    if(keys)keys->mouseDown(event(*keys,6,44));
    ui.reset();
    check(!p.keyboard.isNoteOn(1,36),"closing an editor releases its held keyboard notes");
    check(p.keyboard.isNoteOn(1,60),"closing the editor preserves notes owned by incoming MIDI");p.keyboard.noteOff(1,60,0);p.keyboard.noteOff(1,61,0);
    check(p.getLatencySamples()==initialLatency,"restoring the UI preserves the processor's configured audio buffering delay");
    p.setCurrentProgram(6);p.parameters[34]->setValueNotifyingHost(0.371f);
    ui.reset(p.createEditor());
    cutoff=dynamic_cast<juce::Slider*>(find(*ui,"a1-param-34"));
    check(cutoff && std::abs(cutoff->getValue()-0.371)<0.00001,"reopening the editor reflects the saved sound without changing it");
    if(cutoff)cutoff->mouseEnter(event(*cutoff,16,16,0));
    check(ui->getDescription().contains("Hz") && ui->getDescription().contains("74"),"parameter readout presents original engineering units and its MIDI controller");
    p.parameters[34]->setValueNotifyingHost(0.25f);refresh();
    listener.begins=listener.ends=0;
    if(cutoff) {cutoff->mouseDown(event(*cutoff,16,16));cutoff->mouseDrag(dragEvent(*cutoff,36,-4,16,16));cutoff->mouseUp(event(*cutoff,36,-4));}
    if(cutoff && (std::abs(p.parameters[34]->getValue()-0.45f)>=0.00001f || listener.begins!=1 || listener.ends!=1))
        std::cerr<<"Knob drag: value "<<p.parameters[34]->getValue()<<", begins "<<listener.begins<<", ends "<<listener.ends<<'\n';
    check(cutoff && std::abs(p.parameters[34]->getValue()-0.45f)<0.00001f && listener.begins==1 && listener.ends==1,
          "original knob drag combines horizontal and vertical movement in one host gesture");
    p.parameters[34]->setValueNotifyingHost(0.25f);refresh();
    constexpr int fine=juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::shiftModifier;
    if(cutoff) {cutoff->mouseDown(event(*cutoff,16,16,fine));cutoff->mouseDrag(dragEvent(*cutoff,36,-4,16,16,fine));cutoff->mouseUp(event(*cutoff,36,-4,fine));}
    check(cutoff && std::abs(p.parameters[34]->getValue()-(0.25f+40.0f/300))<0.00001f,"Shift preserves the original 1.5 times finer knob movement");
    if(cutoff) {cutoff->mouseDown(event(*cutoff,16,16,juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::ctrlModifier));cutoff->mouseUp(event(*cutoff,16,16));}
    check(cutoff && p.parameters[34]->getValue()==1.0f,"Control click restores the original knob default");
    auto* volume=find(*ui,"a1-param-51");
    if(volume) {volume->mouseDown(event(*volume,7,67));volume->mouseDrag(dragEvent(*volume,7,20,7,67));volume->mouseUp(event(*volume,7,20));}
    check(volume && std::abs(p.parameters[51]->getValue()-0.91875f)<0.00001f,"amplifier slider follows its original handle travel");
    bool toggles=true;
    for(const int id:{5,9,53,54,55}) {
        auto* button=dynamic_cast<juce::Slider*>(find(*ui,"a1-param-"+juce::String(id)));
        if(!button) {toggles=false;continue;}
        button->setValue(0,juce::sendNotificationSync);
        button->mouseDown(event(*button,5,5));button->mouseUp(event(*button,5,5));toggles&=p.parameters[size_t(id)]->getValue()==1;
        button->mouseDown(event(*button,5,5));button->mouseUp(event(*button,5,5));toggles&=p.parameters[size_t(id)]->getValue()==0;
    }
    check(toggles,"original Glide, Sync, Mono, Chorus and Quad buttons toggle correctly");
    auto* lfo=find(*ui,"a1-param-8");
    if(lfo) {lfo->mouseDown(event(*lfo,10,82));lfo->mouseUp(event(*lfo,10,82));}
    check(lfo && p.parameters[8]->getValue()==1.0f,"sixth original LFO shape button selects Random");
    const auto token=p.parameters[34]->snapshot().token;
    p.setCurrentProgram(1);const auto programToken=p.parameters[34]->snapshot().token;refresh();
    check(cutoff && float(cutoff->getValue())==p.parameters[34]->getValue() && p.parameters[34]->snapshot().token==programToken && token!=programToken,
          "host preset changes refresh the panel without writing parameters back");
    auto* logo=find(*ui,"a1-about");
    if(logo) {logo->mouseDown(event(*logo,10,10));logo->mouseUp(event(*logo,10,10));}
    auto* about=find(*ui,"a1-about-panel");
    check(about && about->isVisible(),"original logo opens the recovered A1 credits artwork");
    const auto credits=about?about->createComponentSnapshot(about->getLocalBounds()):juce::Image();
    check(credits.isValid() && credits.getPixelAt(210,80).getAlpha()>0,"credits view renders the recovered original image");
    if(about)about->mouseDown(event(*about,10,10));
    check(about && !about->isVisible(),"clicking the credits returns to the synth controls");
    // Construct the handler directly: this offscreen editor has no native peer.
    auto action=logo?logo->createAccessibilityHandler():nullptr;
    if(action)action->getActions().invoke(juce::AccessibilityActionType::press);
    check(action && about && about->isVisible(),"credits logo exposes a working native accessibility press action");
    if(about)about->mouseDown(event(*about,10,10));
    if(logo)logo->mouseUp(event(*logo,10,10));
    if(about)about->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    check(about && !about->isVisible() && ui->getControlParameterIndex(*about)==-1,"Escape dismisses credits without targeting a synth parameter");
    struct DisplayCase {int parameter;float value;const char* expected;};
    // Independent examples reconstructed from the original getParameterDisplay code.
    const DisplayCase displays[]={
        {1,0.00f,""},
        {1,0.25f,""},
        {1,0.50f,""},
        {1,0.75f,""},
        {1,1.00f,""},
        {34,0.00f,"10.91Hz"},
        {34,0.25f,"69.30Hz"},
        {34,0.50f,"440.0Hz"},
        {34,0.75f,"2794Hz"},
        {34,1.00f,"17740Hz"},
        {11,0.00f,"-100.0%"},
        {11,0.25f,"-6.250%"},
        {11,0.50f,"0.000%"},
        {11,0.75f,"6.250%"},
        {11,1.00f,"100.0%"},
        {7,0.00f,"0.010Hz"},
        {7,0.25f,"0.178Hz"},
        {7,0.50f,"3.162Hz"},
        {7,0.75f,"56.23Hz"},
        {7,1.00f,"1000Hz"},
        {15,0.00f,"128'"},
        {15,0.25f,"32'"},
        {15,0.50f,"8'"},
        {15,0.75f,"2'"},
        {15,1.00f,"1/2'"},
        {16,0.00f,"-12"},
        {16,0.25f,"-6"},
        {16,0.50f,"0"},
        {16,0.75f,"6"},
        {16,1.00f,"12"},
        {17,0.00f,"-100.0ct"},
        {17,0.25f,"-12.50ct"},
        {17,0.50f,"0.000ct"},
        {17,0.75f,"12.50ct"},
        {17,1.00f,"100.0ct"},
        {4,0.00f,"1"},
        {4,0.25f,"4"},
        {4,0.50f,"8"},
        {4,0.75f,"12"},
        {4,1.00f,"16"},
        {10,0.00f,"1"},
        {10,0.25f,"3"},
        {10,0.50f,"6"},
        {10,0.75f,"9"},
        {10,1.00f,"12"},
        {21,0.00f,"0.000%"},
        {21,0.25f,"0.391%"},
        {21,0.50f,"6.250%"},
        {21,0.75f,"31.64%"},
        {21,1.00f,"100.0%"},
        {18,0.00f,"50.00%"},
        {18,0.25f,"62.50%"},
        {18,0.50f,"75.00%"},
        {18,0.75f,"87.50%"},
        {18,1.00f,"100.0%"},
        {30,0.00f,"0.000%"},
        {30,0.25f,"25.00%"},
        {30,0.50f,"50.00%"},
        {30,0.75f,"75.00%"},
        {30,1.00f,"100.0%"},
        {36,0.00f,"-oodB"},
        {36,0.25f,"3.454dB"},
        {36,0.50f,"24.42dB"},
        {36,0.75f,"41.56dB"},
        {36,1.00f,"54.00dB"},
        {35,0.00f,"0.000%"},
        {35,0.25f,"25.00%"},
        {35,0.50f,"50.00%"},
        {35,0.75f,"75.00%"},
        {35,1.00f,"100.0%"},
        {6,0.00f,"0s"},
        {6,0.25f,"0.016s"},
        {6,0.50f,"0.245s"},
        {6,0.75f,"3.834s"},
        {6,1.00f,"60.00s"},
        {51,0.00f,"-oodB"},
        {51,0.25f,"-36.12dB"},
        {51,0.50f,"-18.06dB"},
        {51,0.75f,"-7.496dB"},
        {51,1.00f,"0.000dB"},
        {56,0.00f,"Off"},
        {56,0.25f,"0.047Hz"},
        {56,0.50f,"0.224Hz"},
        {56,0.75f,"1.057Hz"},
        {56,1.00f,"5.000Hz"},
        {57,0.00f,"0.000%"},
        {57,0.25f,"25.00%"},
        {57,0.50f,"50.00%"},
        {57,0.75f,"75.00%"},
        {57,1.00f,"100.0%"},
        {9,0.00f,"Off"},
        {9,0.25f,"On"},
        {9,0.50f,"On"},
        {9,0.75f,"On"},
        {9,1.00f,"On"},
        {53,0.00f,"Poly"},
        {53,0.25f,"Poly"},
        {53,0.50f,"Poly"},
        {53,0.75f,"Poly"},
        {53,1.00f,"Mono"},
        {5,0.00f,"Off"},
        {5,0.25f,"On"},
        {5,0.50f,"On"},
        {5,0.75f,"On"},
        {5,1.00f,"On"},
        {8,0.00f,"Sine"},
        {8,0.25f,"Triangle"},
        {8,0.50f,"Saw"},
        {8,0.75f,"Square"},
        {8,1.00f,"Random"},
        {14,0.00f,"Sine"},
        {14,0.25f,"Triangle"},
        {14,0.50f,"Sawtooth"},
        {14,0.75f,"Pulse"},
        {14,1.00f,"Pulse"},
        {41,0.00f,"Low Pass"},
        {41,0.25f,"Low Pass"},
        {41,0.50f,"High Pass"},
        {41,0.75f,"Band Pass"},
        {41,1.00f,"Notch"},
        {42,0.00f,"12dB"},
        {42,0.25f,"12dB"},
        {42,0.50f,"12dB"},
        {42,0.75f,"12dB"},
        {42,1.00f,"24dB"},
    };
    bool displayMatch=true;
    for(const auto& item:displays) {
        p.parameters[9]->setValueNotifyingHost(0);
        p.parameters[size_t(item.parameter)]->setValueNotifyingHost(item.value);
        auto* control=find(*ui,"a1-param-"+juce::String(item.parameter));
        if(control)control->mouseEnter(event(*control,1,1,0));
        const bool matches=control && ui->getDescription().contains(" / "+juce::String(item.expected)+" / MIDI CC ");
        if(!matches)std::cerr<<"Display mismatch parameter "<<item.parameter<<" at "<<item.value<<": "<<ui->getDescription()<<" expected "<<item.expected<<'\n';
        displayMatch&=matches;
    }
    check(displayMatch,"all 25 original display types match 125 independently recovered examples");
    p.parameters[9]->setValueNotifyingHost(1);p.parameters[7]->setValueNotifyingHost(0.5f);
    if(auto* speed=find(*ui,"a1-param-7"))speed->mouseEnter(event(*speed,1,1,0));
    check(ui->getDescription().contains(" / 1/4. / MIDI CC "),"LFO speed readout switches to original dotted tempo divisions");
    p.setCurrentProgram(6);refresh();if(cutoff)cutoff->mouseEnter(event(*cutoff,16,16,0));
    auto image=ui->createComponentSnapshot(ui->getLocalBounds(),true,1.0f);
    const auto output=juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
    output.getParentDirectory().createDirectory();
    if(auto stream=output.createOutputStream()) {stream->setPosition(0);stream->truncate();juce::PNGImageFormat().writeImageToStream(image,*stream);}
    check(image.getWidth()==769 && image.getHeight()==426 && image.getPixelAt(20,20)!=image.getPixelAt(20,425),
          "restored editor renders the recovered textured panel");
    {
        const auto empty=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("a1-ui-missing-artwork","",false);
        empty.createDirectory();
        {
            a1::Processor missing(empty);std::unique_ptr<juce::AudioProcessorEditor> fallback(missing.createEditor());
            const auto view=fallback->createComponentSnapshot(fallback->getLocalBounds());
            check(view.isValid() && view.getPixelAt(0,0)==juce::Colour(0xff092034),"missing artwork produces a readable recovery message without crashing the host");
        }
        empty.deleteRecursively();
    }
    p.parameters[34]->removeListener(&listener);
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
