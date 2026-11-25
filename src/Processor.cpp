#include "Processor.h"
#include "Editor.h"
#include "HostContext.h"
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <dlfcn.h>

namespace a1 {
namespace {
void resourceAnchor() {}
juce::File findResources() {
    Dl_info info{};
    if(dladdr(reinterpret_cast<void*>(&resourceAnchor),&info) && info.dli_fname)
        return juce::File(info.dli_fname).getParentDirectory().getParentDirectory().getChildFile("Resources");
    return {};
}

}
Processor::Processor(juce::File directory) : AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)) {
    resources=directory.isDirectory()?directory:findResources();
    for(int i=0;i<99;++i)parameterNames[size_t(i)]="A1 Parameter "+juce::String(i+1);
    if(auto xml=juce::XmlDocument::parse(resources.getChildFile("A1.vstxml"))) {
        auto add=[&](int id,const juce::String& name) {
            if(id<0 || id>=99)return;
            parameterNames[size_t(id)]=name;
            if(std::find(visible.begin(),visible.end(),id)==visible.end())visible.push_back(id);
        };
        for(auto* item:xml->getChildIterator()) {
            if(item->hasTagName("Param"))add(item->getIntAttribute("id"),item->getStringAttribute("name"));
            if(!item->hasTagName("Group"))continue;
            const auto group=item->getStringAttribute("name");
            if(item->getStringAttribute("template")=="Envelope") {
                int offset=item->getStringAttribute("values").fromFirstOccurrenceOf("=",false,false).getIntValue();
                const char* parts[]={"Attack","Decay","Sustain","Release"};
                for(int i=0;i<4;++i)add(offset+i,group+" "+parts[i]);
            }
            for(auto* param:item->getChildIterator())if(param->hasTagName("Param"))
                add(param->getIntAttribute("id"),group+" "+param->getStringAttribute("name"));
        }
    }
    try {
        for(const auto& name:{"A1_Default_Bank2.fxb","A1_Default_Bank1.fxb"}) {
            const auto file=resources.getChildFile(name);
            if(!file.existsAsFile())continue;
            juce::MemoryBlock bytes;if(!file.loadFileAsData(bytes))throw std::runtime_error("Cannot read A1 factory bank");
            auto bank=parseBank({static_cast<const uint8_t*>(bytes.getData()),bytes.getSize()});
            for(auto& p:bank) {
                // Original factory banks leave MIDI CC7's hidden slot at zero.
                // A1's controller default is unity; applying the stored zero
                // explicitly would mute every voice. Only factory selection
                // resets it: restored states and live CC7 retain their values.
                p.parameters[98]=1.0f;
                p.name=std::string(std::string_view(name)=="A1_Default_Bank2.fxb"?"Bank 2 / ":"Bank 1 / ")+p.name;
                programs.push_back(std::move(p));
            }
        }
        bankValid=!programs.empty();
    } catch(const std::exception& e) {problem=e.what();programs.clear();}
    if(programs.empty())programs.push_back({"Factory data unavailable",{}});
    selected.store(programs.size()>1?1:0);
    for(int i=0;i<99;++i) {
        auto* p=new Parameter(i,parameterNames[size_t(i)],programs[size_t(selected.load())].parameters[size_t(i)]);
        parameters[size_t(i)]=p;addParameter(p);
    }
    if(visible.empty())for(int i=4;i<=58;++i)visible.push_back(i);
    cachedRequest.request.program=uint32_t(selected.load());
    for(size_t i=0;i<99;++i) {
        const auto snapshot=parameters[i]->snapshot();cachedRequest.request.parameters[i]=snapshot.value;cachedRequest.revisions[i]=snapshot.token;
    }
    setLatencySamples(bufferingDelay);startTimerHz(10);
}
Processor::~Processor() {
    stopTimer();std::lock_guard lock(engineMutex);pipeline.reset();engine.reset();prefetch->release();
}
bool Processor::isBusesLayoutSupported(const BusesLayout& b) const {
    return b.getMainInputChannelSet().isDisabled() && b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
}
void Processor::setIHostApplication(Steinberg::FUnknown* context) {
    Steinberg::Vst::IHostApplication* host=nullptr;
    const Steinberg::TUID iid=INLINE_UID(0x58E595CC,0xDB2D4969,0x8B6AAF8C,0x36A664E5);
    Steinberg::Vst::String128 name{};
    bool found=context && context->queryInterface(iid,reinterpret_cast<void**>(&host))==Steinberg::kResultOk && host;
    bool named=found && host->getName(name)==Steinberg::kResultOk;name[127]=0;
    if(host)host->release();
    const juce::String reported=juce::String(juce::CharPointer_UTF16(reinterpret_cast<const juce::CharPointer_UTF16::CharType*>(name)));
    const bool allowed=allowCubaseContext(juce::PluginHostType().isCubase(),named,reported.toStdString());
    std::lock_guard lock(engineMutex);approved=allowed;hostName=reported;
    if(!approved) {pipeline.reset();engine.reset();problem="This preview runs the original A1 inside Cubase";}
    else startEngine();
}
int32_t Processor::queryIAudioProcessor(const Steinberg::TUID iid,void** object) {
    if(!object)return Steinberg::kInvalidArgument;*object=nullptr;
    const Steinberg::TUID prefetchId=INLINE_UID(0x8AE54FDA,0xE93046B9,0xA28555BC,0xDC98E21E);
    // JUCE must keep its own FUnknown and all other built-in interfaces.
    if(iid && std::memcmp(iid,prefetchId,sizeof(Steinberg::TUID))==0) {
        prefetchQueries.fetch_add(1,std::memory_order_relaxed);return prefetch->queryInterface(iid,object);
    }
    return Steinberg::kNoInterface;
}
void Processor::startEngine() {
    if(!approved || !prepared || !bankValid)return;
    if(!engine) {
        auto root=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
        auto bottles=root.getChildFile("Application Support/A1RevivalPreview/bottles");
        auto logs=root.getChildFile("Logs/A1RevivalPreview");logs.createDirectory();
        const auto name="engine-"+juce::Uuid().toString();const auto log=logs.getChildFile(name+".log");
        performanceLog=logs.getChildFile(name+".performance.jsonl");
        engine=std::make_unique<Bridge>(resources.getFullPathName().toStdString(),bottles.getFullPathName().toStdString(),
            log.getFullPathName().toStdString(),hostName.toStdString());
    }
    if(engine->ready() && !pipeline) {
        auto* bridge=engine.get();
        pipeline=std::make_unique<RenderPipeline>([bridge](const auto& job,auto& result) {
            return bridge->process(job.request,job.midi.data(),result.actual,result.audio[0],result.audio[1]);
        },bufferingDelay,[this](const auto& job,const auto& result) {
            for(size_t i=0;i<99;++i)if(parameters[i]->adopt({job.revisions[i],job.request.parameters[i]},result.actual[i],false))
                pendingNotifications[i].store(true,std::memory_order_release);
        });
        performanceLog.appendText("{\"version\":\""+juce::String(JucePlugin_VersionString)+"\",\"sample_rate\":"+juce::String(rate)+",\"maximum_block\":"+juce::String(maximumBlock)
            +",\"latency_frames\":"+juce::String(bufferingDelay)+"}\n");
    }
    problem=engine->ready()?"Original A1 engine ready / "+juce::String(1000.0*bufferingDelay/rate,1)+" ms latency":juce::String(engine->error());
}
void Processor::prepareToPlay(double sampleRate,int block) {
    {
        std::lock_guard lock(engineMutex);
        if(!std::isfinite(sampleRate) || sampleRate<8000 || sampleRate>384000) {problem="Unsupported sample rate";return;}
        pipeline.reset();rate=sampleRate;maximumBlock=juce::jlimit(1,16384,block);bufferingDelay=std::max(768,2*maximumBlock);
        position=0;prepared=true;midiSafety.missedBlock();
        if(engine && !engine->ready())engine.reset();startEngine();
    }
    setLatencySamples(bufferingDelay);
}
void Processor::releaseResources() {
    std::lock_guard lock(engineMutex);keyboard.allNotesOff(0);midiSafety.missedBlock();if(pipeline)pipeline->reset();
}
juce::String Processor::status() const {
    std::shared_lock lock(engineMutex);
    auto text=engine && !engine->ready()?juce::String(engine->error()):problem;
    if(pipeline && pipeline->failed())text="Original A1 worker stopped responding";
    if(midiOverflow.load())text+=" / MIDI burst limit reached; notes reset";
    return text;
}
const juce::String Processor::getProgramName(int p) {
    return juce::isPositiveAndBelow(p,int(programs.size()))?juce::String::fromUTF8(programs[size_t(p)].name.c_str()):juce::String();
}
void Processor::setCurrentProgram(int p) {
    if(!juce::isPositiveAndBelow(p,int(programs.size())))return;
    auto* messages=juce::MessageManager::getInstanceWithoutCreating();
    if(!messages || !messages->isThisTheMessageThread()) {
        pendingProgram.store(p,std::memory_order_release);return;
    }
    {std::lock_guard lock(stateMutex);pendingProgram.store(-1);selected.store(p);for(int i=0;i<99;++i)parameters[size_t(i)]->setValue(programs[size_t(p)].parameters[size_t(i)]);}
    for(auto* parameter:parameters)parameter->notifyChanged();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withProgramChanged(true));
}
void Processor::applyPendingProgram() {
    auto p=pendingProgram.load(std::memory_order_acquire);if(!juce::isPositiveAndBelow(p,int(programs.size())))return;
    std::unique_lock lock(stateMutex,std::try_to_lock);if(!lock)return;
    if(!pendingProgram.compare_exchange_strong(p,-1))return;
    selected.store(p);
    for(size_t i=0;i<99;++i) {parameters[i]->setValue(programs[size_t(p)].parameters[i]);pendingNotifications[i].store(true);}
    pendingProgramNotification.store(true);
}
void Processor::timerCallback() {
    for(size_t i=0;i<99;++i)if(pendingNotifications[i].exchange(false,std::memory_order_acq_rel))parameters[i]->notifyChanged();
    if(pendingProgramNotification.exchange(false))updateHostDisplay(juce::AudioProcessor::ChangeDetails().withProgramChanged(true));
    if(++timerTicks%10)return;
    std::shared_lock lock(engineMutex,std::try_to_lock);if(!lock || !pipeline || performanceLog==juce::File())return;
    const auto s=pipeline->stats();
    performanceLog.appendText("{\"callbacks\":"+juce::String(callbacks.load())+",\"callback_p50_us\":"+juce::String(callbackTiming.percentileUs(.5),3)
        +",\"callback_p95_us\":"+juce::String(callbackTiming.percentileUs(.95),3)+",\"callback_p99_us\":"+juce::String(callbackTiming.percentileUs(.99),3)
        +",\"callback_max_us\":"+juce::String(callbackTiming.maxUs(),3)+",\"deadline_misses\":"+juce::String(deadlineMisses.load())
        +",\"unexpected_blocks\":"+juce::String(unexpectedBlocks.load())+",\"prefetch_queries\":"+juce::String(prefetchQueries.load())
        +",\"submitted\":"+juce::String(s.submitted)+",\"rendered\":"+juce::String(s.rendered)
        +",\"missing_frames\":"+juce::String(s.missingFrames)+",\"input_drops\":"+juce::String(s.inputDrops)
        +",\"late_frames\":"+juce::String(s.lateFrames)+",\"output_dropped_frames\":"+juce::String(s.outputDroppedFrames)
        +",\"input_depth\":"+juce::String(s.inputDepth)+",\"output_depth\":"+juce::String(s.outputDepth)
        +",\"render_p99_us\":"+juce::String(s.renderP99Us,3)+",\"render_max_us\":"+juce::String(s.renderMaxUs,3)+"}\n");
}
void Processor::processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi) {
    const auto started=std::chrono::steady_clock::now();
    struct CallbackTimer {
        Processor& owner;std::chrono::steady_clock::time_point start;bool realtime;double deadline=0;
        ~CallbackTimer() {
            if(!realtime)return;
            const auto ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
            owner.callbackTiming.record(ns);owner.callbacks.fetch_add(1,std::memory_order_relaxed);
            if(deadline>0 && double(ns)>deadline)owner.deadlineMisses.fetch_add(1,std::memory_order_relaxed);
        }
    } callbackTimer{*this,started,!isNonRealtime()};
    juce::ScopedNoDenormals noDenormals;
    keyboard.processNextMidiBuffer(midi,0,audio.getNumSamples(),true);
    for(const auto item:midi)if(item.getMessage().isProgramChange())pendingProgram.store(item.getMessage().getProgramChangeNumber());
    applyPendingProgram();
    audio.clear();peak.store(0);
    std::shared_lock lock(engineMutex,std::try_to_lock);
    if(!lock || !engine || !engine->ready() || !pipeline || pipeline->failed() || audio.getNumChannels()<2) {
        midiSafety.missedBlock();position+=audio.getNumSamples();midi.clear();return;
    }
    if(!isNonRealtime() && audio.getNumSamples()>maximumBlock) {
        unexpectedBlocks.fetch_add(1);midiSafety.missedBlock();midi.clear();return;
    }
    callbackTimer.deadline=1.0e9*audio.getNumSamples()/rate;
    if(audio.getNumSamples()==0) {if(!midi.isEmpty())midiSafety.missedBlock();midi.clear();return;}
    auto& request=cachedRequest.request;
    request.sampleRate=rate;request.tempo=120;request.samplePosition=position;
    if(auto* playhead=getPlayHead())if(auto p=playhead->getPosition()) {
        if(auto bpm=p->getBpm())request.tempo=*bpm;
        if(auto sample=p->getTimeInSamples())request.samplePosition=double(*sample);
    }
    float maximum=0;
    for(int start=0;start<audio.getNumSamples();start+=int(A1_MAX_FRAMES)) {
        {std::unique_lock state(stateMutex,std::try_to_lock);if(state) {
            request.program=uint32_t(selected.load());
            for(size_t i=0;i<99;++i) {const auto snapshot=parameters[i]->snapshot();cachedRequest.revisions[i]=snapshot.token;request.parameters[i]=snapshot.value;}}}
        request.frames=uint32_t(juce::jmin(int(A1_MAX_FRAMES),audio.getNumSamples()-start));
        const auto packet=midiSafety.makePacket(midi,start,int(request.frames));request.midiCount=packet.count;
        std::copy_n(packet.events.data(),packet.count,cachedRequest.midi.data());
        if(packet.overflow)midiOverflow.store(true);
        if(!pipeline->process(cachedRequest,audio.getWritePointer(0,start),audio.getWritePointer(1,start),isNonRealtime())) {
            midiSafety.missedBlock();break;
        }
        for(int c=0;c<2;++c)for(uint32_t i=0;i<request.frames;++i) {
            float& v=audio.getWritePointer(c,start)[i];if(!std::isfinite(v))v=0;maximum=juce::jmax(maximum,std::abs(v));
        }
        request.samplePosition+=request.frames;
    }
    position+=audio.getNumSamples();peak.store(maximum);midi.clear();
}
void Processor::getStateInformation(juce::MemoryBlock& destination) {
    std::lock_guard lock(stateMutex);juce::MemoryOutputStream out(destination,false);
    out.writeInt(0x41315231);out.writeInt(selected.load());for(auto* p:parameters)out.writeFloat(p->getValue());
}
void Processor::setStateInformation(const void* data,int size) {
    if(!data || size!=404)return;juce::MemoryInputStream in(data,size_t(size),false);
    if(in.readInt()!=0x41315231)return;int p=in.readInt();if(!juce::isPositiveAndBelow(p,int(programs.size())))return;
    std::array<float,99> values{};for(auto& v:values) {v=in.readFloat();if(!std::isfinite(v) || v<0 || v>1)return;}
    {std::lock_guard lock(stateMutex);pendingProgram.store(-1);selected.store(p);for(size_t i=0;i<99;++i)parameters[i]->setValue(values[i]);}
    for(auto* parameter:parameters)parameter->notifyChanged();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withProgramChanged(true));
}
juce::AudioProcessorEditor* Processor::createEditor() {return new Editor(*this,resources);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {return new a1::Processor();}
