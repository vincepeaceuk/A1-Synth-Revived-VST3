#include "Processor.h"
#include A1_VST3_WRAPPER_FILE
#include <future>
#include <iostream>

namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;
struct Queue final : IParamValueQueue {
    ParamID id=0; double value=0;
    tresult PLUGIN_API queryInterface(const TUID,void** out) override {*out=nullptr;return kNoInterface;}
    uint32 PLUGIN_API addRef() override {return 1;}
    uint32 PLUGIN_API release() override {return 1;}
    ParamID PLUGIN_API getParameterId() override {return id;}
    int32 PLUGIN_API getPointCount() override {return 1;}
    tresult PLUGIN_API getPoint(int32 i,int32& offset,ParamValue& out) override {
        if(i!=0)return kInvalidArgument;offset=0;out=value;return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32,ParamValue,int32&) override {return kNotImplemented;}
};
struct Changes final : IParameterChanges {
    Queue queue;
    tresult PLUGIN_API queryInterface(const TUID,void** out) override {*out=nullptr;return kNoInterface;}
    uint32 PLUGIN_API addRef() override {return 1;}
    uint32 PLUGIN_API release() override {return 1;}
    int32 PLUGIN_API getParameterCount() override {return 1;}
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override {return i==0?&queue:nullptr;}
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&,int32&) override {return nullptr;}
};
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    juce::JuceVST3Component component({},{});
    auto& processor=static_cast<a1::Processor&>(component.getPluginInstance());
    int failures=0;
    const auto check=[&](bool ok,const char* message) {
        std::cout<<(ok?"PASS: ":"FAIL: ")<<message<<'\n';failures+=!ok;
    };
    check(processor.getNumPrograms()==256,"wrapper fixture loads the actual packaged factory banks");
    if(processor.getNumPrograms()!=256)return 2;
    check(processor.status().contains("Cubase"),"generic wrapper fixture never authorizes the original engine");
    Changes changes;
    changes.queue.id=component.comPluginInstance->getProgramParamID();
    juce::AudioBuffer<float> audio(2,256);
    const auto render=[&] {juce::MidiBuffer midi;processor.processBlock(audio,midi);};
    a1::Processor reference;
    bool exact=true;
    for(int program=0;program<256;++program) {
        changes.queue.value=double(program)/255;
        component.processParameterChanges(changes);render();reference.setCurrentProgram(program);
        exact&=processor.getCurrentProgram()==program;
        for(size_t i=0;i<99;++i)exact&=processor.parameters[i]->getValue()==reference.parameters[i]->getValue();
    }
    check(exact,"VST3 program queues select every factory slot and all 99 values in both banks");
    for(size_t i=0;i<99;++i)processor.parameters[i]->setValue(float(i+1)/101.0f);
    juce::MemoryBlock saved;processor.getStateInformation(saved);
    processor.setCurrentProgram(1);
    component.setStateInformation(saved.getData(),int(saved.getSize()));
    changes.queue.value=255.0/255;
    auto recall=std::async(std::launch::async,[&]{component.processParameterChanges(changes);});
    recall.get();render();
    juce::MemoryBlock recalled;processor.getStateInformation(recalled);
    check(recalled==saved,"a host program echo after component state restore preserves all 99 edited values");
    component.processParameterChanges(changes);render();
    processor.getStateInformation(recalled);
    check(recalled==saved,"repeated program echoes on the message thread also preserve edited state");
    changes.queue.value=6.0/255;
    {
        std::unique_lock writer(processor.stateMutex);
        auto callback=std::async(std::launch::async,[&]{component.processParameterChanges(changes);});
        const auto ready=callback.wait_for(std::chrono::milliseconds(20))==std::future_status::ready;
        check(ready,"VST3 program queues never wait for an editor holding the state lock");
        writer.unlock();callback.get();
    }
    render();check(processor.getCurrentProgram()==6,"deferred VST3 program selection applies before the next render");
    {
        std::unique_lock writer(processor.stateMutex);
        auto callback=std::async(std::launch::async,[&]{
            changes.queue.value=9.0/255;component.processParameterChanges(changes);
            changes.queue.value=6.0/255;component.processParameterChanges(changes);
        });
        callback.get();writer.unlock();render();
        check(processor.getCurrentProgram()==6,"the last program request wins while editor contention defers application");
    }
    bool rejected=true;
    for(const auto invalid:{-0.1,1.1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        changes.queue.value=invalid;component.processParameterChanges(changes);render();
        rejected&=processor.getCurrentProgram()==6;
    }
    check(rejected,"invalid normalized program values leave the selected sound unchanged");
    changes.queue.id=component.comPluginInstance->getVSTParamIDForIndex(34);changes.queue.value=.375;
    component.processParameterChanges(changes);
    check(processor.parameters[34]->getValue()==.375f,"ordinary VST3 parameter automation still reaches the same parameter ID");
    return failures?1:0;
}
