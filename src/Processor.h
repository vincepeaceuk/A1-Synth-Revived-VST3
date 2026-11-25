#pragma once
#include <JuceHeader.h>
#include "Bank.h"
#include "Bridge.h"
#include "MidiSafety.h"
#include "RenderPipeline.h"
#include "PrefetchSupport.h"
#include <atomic>
#include <mutex>
#include <shared_mutex>

namespace a1 {
class Parameter final : public juce::AudioProcessorParameterWithID {
public:
    struct Snapshot { uint64_t token; float value; };
    Parameter(int index,const juce::String& name,float initial)
      : AudioProcessorParameterWithID(juce::ParameterID("a1_"+juce::String(index),1),name),state(pack(initial,0)),initial(initial) {}
    Snapshot snapshot() const {
        const auto token=state.load();return {token,std::bit_cast<float>(uint32_t(token))};
    }
    float getValue() const override {return snapshot().value;}
    void setValue(float v) override {
        if(!std::isfinite(v))return;v=juce::jlimit(0.0f,1.0f,v);
        auto old=state.load();
        while(!state.compare_exchange_weak(old,pack(v,uint32_t(old>>32)+1))) {}
    }
    float getDefaultValue() const override {return initial;}
    juce::String getLabel() const override {return {};}
    juce::String getText(float v,int length) const override {return juce::String(v,4).substring(0,length);}
    float getValueForText(const juce::String& text) const override {return juce::jlimit(0.0f,1.0f,text.getFloatValue());}
    void notifyChanged() {sendValueChangedMessageToListeners(getValue());}
    bool adopt(Snapshot expected,float actual,bool notify=true) {
        if(!std::isfinite(actual) || actual<0 || actual>1)return false;
        auto current=state.load();const auto generation=uint32_t(expected.token>>32);
        // Engine readbacks are ordered on one worker and do not represent new
        // user edits. Preserve that edit generation across consecutive replies.
        while(uint32_t(current>>32)==generation) {
            if(std::bit_cast<float>(uint32_t(current))==actual)return false;
            if(state.compare_exchange_weak(current,pack(actual,generation))) {
                if(notify)notifyChanged();return true;
            }
        }
        return false;
    }
private:
    static uint64_t pack(float value,uint32_t generation) {
        return (uint64_t(generation)<<32)|std::bit_cast<uint32_t>(value);
    }
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::atomic<uint64_t> state;
    float initial;
};
class Processor final : public juce::AudioProcessor,public juce::VST3ClientExtensions,private juce::Timer {
public:
    explicit Processor(juce::File resourceDirectory={});
    ~Processor() override;
    const juce::String getName() const override {return JucePlugin_Name;}
    bool acceptsMidi() const override {return true;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override {return 30.0;}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override {return int(programs.size());}
    int getCurrentProgram() override {
        const auto requested=pendingProgram.load(std::memory_order_acquire);
        return requested>=0?requested:selected.load();
    }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    juce::VST3ClientExtensions* getVST3ClientExtensions() override {return this;}
    void setIHostApplication(Steinberg::FUnknown*) override;
    int32_t queryIAudioProcessor(const Steinberg::TUID,void**) override;
    juce::String status() const;
    std::array<Parameter*,99> parameters{};
    std::array<juce::String,99> parameterNames{};
    std::vector<int> visible;
    juce::MidiKeyboardState keyboard;
    std::atomic<float> peak{0};
private:
    void startEngine();
    void applyPendingProgram();
    void timerCallback() override;
    juce::File resources;
    std::vector<Program> programs;
    std::atomic<int> selected{1};
    mutable std::shared_mutex engineMutex;
    mutable std::mutex stateMutex;
    std::unique_ptr<Bridge> engine;
    std::unique_ptr<RenderPipeline> pipeline;
    RenderPipeline::Job cachedRequest;
    PrefetchSupport* prefetch=PrefetchSupport::create();
    std::array<std::atomic<bool>,99> pendingNotifications{};
    std::atomic<int> pendingProgram{-1};
    std::atomic<bool> pendingProgramNotification{false};
    MidiSafety midiSafety;
    std::atomic<bool> midiOverflow{false};
    TimingHistogram callbackTiming;
    std::atomic<uint64_t> callbacks{0},deadlineMisses{0},unexpectedBlocks{0},prefetchQueries{0};
    juce::File performanceLog;
    int maximumBlock=256,bufferingDelay=768,timerTicks=0;
    bool approved=false,prepared=false,bankValid=false;
    double rate=44100,position=0;
    juce::String hostName,problem="Open this preview in Cubase to start A1";
};
}
