#include "Processor.h"
#include "MidiSafety.h"
#include "PrefetchSupport.h"
#include <future>
#include <iostream>

struct Listener : juce::AudioProcessorParameter::Listener {
    int changes=0;
    void parameterValueChanged(int,float) override {++changes;}
    void parameterGestureChanged(int,bool) override {}
};
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    juce::ScopedJuceInitialiser_GUI init;
    a1::Processor p(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
    int failures=0;
    auto check=[&](bool ok,const char* label) {std::cout<<(ok?"PASS: ":"FAIL: ")<<label<<"\n";if(!ok)++failures;};
    check(p.getNumPrograms()>=128,"recovered factory catalog loads");
    check(p.getProgramName(1).contains("BassIndeed"),"factory sound names preserved");
    check(p.getParameters().size()==99,"all stored parameters exposed");
    check(p.getNumPrograms()==256,"the packaged instrument contains both complete factory banks");
    {
        bool exact=true;
        for(int program=0;program<p.getNumPrograms();++program) {
            p.setCurrentProgram(program);
            std::array<float,99> expected{};
            for(size_t i=0;i<99;++i)expected[i]=p.parameters[i]->getValue();
            juce::MemoryBlock state;p.getStateInformation(state);
            p.setCurrentProgram((program+1)%p.getNumPrograms());
            p.setStateInformation(state.getData(),int(state.getSize()));
            exact&=state.getSize()==404 && p.getCurrentProgram()==program;
            for(size_t i=0;i<99;++i)exact&=p.parameters[i]->getValue()==expected[i];
        }
        check(exact,"all 256 factory slots round-trip their selected program and all 99 exact stored values");
        p.setCurrentProgram(1);
    }
    check(p.parameters[98]->getValue()==1.0f,"factory sound starts with full MIDI controller volume");
    const auto pendingVolume=p.parameters[98]->snapshot();
    p.setCurrentProgram(1);
    p.parameters[98]->adopt(pendingVolume,0.0f);
    check(p.parameters[98]->getValue()==1.0f,"old MIDI reply cannot mute a freshly selected factory sound");
    Listener listener;p.parameters[34]->addListener(&listener);
    p.parameters[34]->setValueNotifyingHost(0.314159f);
    p.parameters[98]->setValueNotifyingHost(0.0f);
    juce::MemoryBlock saved;p.getStateInformation(saved);
    listener.changes=0;p.setCurrentProgram(3);
    check(listener.changes>0,"preset change notifies parameter listeners");
    check(p.parameters[98]->getValue()==1.0f,"selecting a factory sound resets MIDI controller volume");
    listener.changes=0;p.setStateInformation(saved.getData(),int(saved.getSize()));
    check(p.getCurrentProgram()==1 && p.parameters[34]->getValue()==0.314159f,"state restores exact parameter and program");
    check(listener.changes>0,"state restore notifies parameter listeners");
    check(p.parameters[98]->getValue()==0.0f,"saved intentional MIDI volume mute restores exactly");
    listener.changes=0;p.parameters[34]->adopt(p.parameters[34]->snapshot(),0.5f);
    check(p.parameters[34]->getValue()==0.5f && listener.changes>0,"original engine MIDI changes reach host listeners");
    const auto oldReply=p.parameters[34]->snapshot();
    p.parameters[34]->setValue(0.75f);p.parameters[34]->adopt(oldReply,0.25f);
    check(p.parameters[34]->getValue()==0.75f,"late helper response cannot overwrite a concurrent edit");
    p.parameters[34]->setValue(0.5f);const auto pendingValue=p.parameters[34]->snapshot();
    p.parameters[34]->setValue(0.75f);p.parameters[34]->setValue(0.5f);
    p.parameters[34]->adopt(pendingValue,0.25f);
    check(p.parameters[34]->getValue()==0.5f,"same-value edit cycle rejects an older MIDI reply");
    p.parameters[98]->setValue(1.0f);const auto beforeControllers=p.parameters[98]->snapshot();
    p.parameters[98]->adopt(beforeControllers,0.0f);
    p.parameters[98]->adopt(beforeControllers,0.5f);
    check(p.parameters[98]->getValue()==0.5f,"ordered engine readbacks share an edit generation without losing newer controllers");
    const auto before=p.parameters[34]->getValue();
    p.setStateInformation(saved.getData(),int(saved.getSize())-1);
    check(p.parameters[34]->getValue()==before,"truncated state leaves current sound unchanged");
    {
        juce::MemoryBlock baseline;p.getStateInformation(baseline);
        auto rejects=[&](juce::MemoryBlock changed,int size) {
            p.setStateInformation(changed.getData(),size);
            juce::MemoryBlock after;p.getStateInformation(after);
            return baseline==after;
        };
        bool unchanged=true;
        for(auto bad:std::array<int,3>{0,-1,0x7fffffff}) {
            auto changed=baseline;juce::MemoryOutputStream out(changed,true);out.setPosition(0);out.writeInt(bad);
            unchanged&=rejects(changed,int(changed.getSize()));
        }
        for(auto bad:std::array<int,3>{-1,p.getNumPrograms(),0x7fffffff}) {
            auto changed=baseline;juce::MemoryOutputStream out(changed,true);out.setPosition(4);out.writeInt(bad);
            unchanged&=rejects(changed,int(changed.getSize()));
        }
        for(float bad:std::array<float,4>{-0.001f,1.001f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
            auto changed=baseline;juce::MemoryOutputStream out(changed,true);out.setPosition(8+98*4);out.writeFloat(bad);
            unchanged&=rejects(changed,int(changed.getSize()));
        }
        auto trailing=baseline;trailing.append("x",1);unchanged&=rejects(trailing,int(trailing.getSize()));
        p.setStateInformation(nullptr,int(baseline.getSize()));juce::MemoryBlock after;p.getStateInformation(after);
        check(unchanged && after==baseline,"malformed project states reject atomically without partially changing any parameter");
    }
    p.setIHostApplication(nullptr);p.prepareToPlay(44100,256);
    check(p.getLatencySamples()>=512 && p.getLatencySamples()<=768,
          "normal Cubase blocks report a bounded lower-latency delay for live playing");
    for(const auto setting:std::array<std::pair<double,int>,5>{{{44100,64},{48000,128},{48000,256},{96000,512},{48000,2048}}}) {
        p.prepareToPlay(setting.first,setting.second);
        check(p.getLatencySamples()>=2*setting.second && p.getLatencySamples()<=std::max(768,2*setting.second),
              "latency preserves at least two host blocks without the old fixed 2048-frame floor");
    }
    p.prepareToPlay(44100,256);
    const Steinberg::TUID prefetchId=INLINE_UID(0x8AE54FDA,0xE93046B9,0xA28555BC,0xDC98E21E);
    void* extension=nullptr;
    const auto queried=p.queryIAudioProcessor(prefetchId,&extension);
    check(queried==Steinberg::kResultOk && extension,"processor exposes its no-prefetch scheduling contract");
    if(extension)static_cast<Steinberg::Vst::IPrefetchableSupport*>(extension)->release();
    juce::AudioBuffer<float> audio(2,256);for(int c=0;c<2;++c)for(int i=0;i<256;++i)audio.setSample(c,i,1);
    juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,48,uint8_t(100)),0);p.processBlock(audio,midi);
    check(audio.getMagnitude(0,256)==0,"generic process never starts the original engine");
    {
        std::unique_lock writer(p.stateMutex);
        auto callback=std::async(std::launch::async,[&] {
            juce::MidiBuffer change;change.addEvent(juce::MidiMessage::programChange(1,3),0);
            p.processBlock(audio,change);
        });
        const bool returned=callback.wait_for(std::chrono::milliseconds(20))==std::future_status::ready;
        check(returned,"MIDI program changes never wait for an editor holding the state lock");
        writer.unlock();callback.get();
    }
    p.setStateInformation(saved.getData(),int(saved.getSize()));
    {juce::MidiBuffer empty;p.processBlock(audio,empty);}
    check(p.getCurrentProgram()==1,"restoring a preset discards an older deferred MIDI program change");
    a1::MidiSafety safety;
    juce::MidiBuffer dense;
    for(int i=0;i<256;++i)dense.addEvent(juce::MidiMessage::controllerEvent(1,1,i%128),i);
    dense.addEvent(juce::MidiMessage::noteOff(1,60),255);
    const auto overflow=safety.makePacket(dense,0,256);
    check(overflow.overflow && overflow.count==32,"dense MIDI burst resets every channel instead of losing note releases");
    bool resetAll=true;
    for(unsigned c=0;c<16;++c) {
        const auto& sustain=overflow.events[2*c];const auto& notes=overflow.events[2*c+1];
        resetAll&=sustain.frame==0 && sustain.size==3 && sustain.data[0]==0xb0+c && sustain.data[1]==64 && sustain.data[2]==0;
        resetAll&=notes.frame==0 && notes.size==3 && notes.data[0]==0xb0+c && notes.data[1]==123 && notes.data[2]==0;
    }
    check(resetAll,"panic releases sustain and notes on all sixteen channels");
    safety.missedBlock();juce::MidiBuffer next;
    next.addEvent(juce::MidiMessage::noteOn(1,60,uint8_t(100)),5);
    auto recovered=safety.makePacket(next,0,256);
    check(!recovered.overflow && recovered.count==33 && recovered.events[32].frame==5 && recovered.events[32].data[0]==0x90,
          "a skipped block resets old voices before the next note");
    juce::MidiBuffer empty;
    check(safety.makePacket(empty,0,256).count==0,"recovery panic is consumed exactly once");
    juce::MidiBuffer boundary;
    for(int i=0;i<255;++i)boundary.addEvent(juce::MidiMessage::controllerEvent(1,1,i%128),i);
    boundary.addEvent(juce::MidiMessage::noteOff(1,60),255);
    const auto full=safety.makePacket(boundary,0,256);
    check(!full.overflow && full.count==256 && full.events[255].data[0]==0x80 && full.events[255].frame==255,
          "full supported packet retains its final note-off and timing");
    {
        a1::MidiSafety split;
        juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::controllerEvent(1,64,127),63);
        events.addEvent(juce::MidiMessage::noteOn(1,60,uint8_t(100)),64);
        events.addEvent(juce::MidiMessage::pitchWheel(1,9000),97);
        events.addEvent(juce::MidiMessage::noteOff(1,60),127);
        events.addEvent(juce::MidiMessage::controllerEvent(1,64,0),128);
        events.addEvent(juce::MidiMessage::programChange(1,4),100);
        const auto first=split.makePacket(events,64,64),second=split.makePacket(events,128,64);
        check(first.count==3 && first.events[0].frame==0 && first.events[1].frame==33 && first.events[2].frame==63
              && second.count==1 && second.events[0].frame==0 && second.events[0].data[1]==64 && second.events[0].data[2]==0,
              "split render blocks preserve note, pitch and sustain offsets exactly at both block boundaries");
    }
    p.parameters[34]->removeListener(&listener);
    return failures?1:0;
}
