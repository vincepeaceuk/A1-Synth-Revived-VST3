#include "RenderPipeline.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace {
int failures=0,checks=0;
void check(bool condition,const char* label) {
    ++checks;std::cout<<(condition?"PASS: ":"FAIL: ")<<label<<'\n';if(!condition)++failures;
}
a1::RenderPipeline::Job job(int frames,int position) {
    a1::RenderPipeline::Job j{};j.request.frames=uint32_t(frames);
    j.request.sampleRate=44100;j.request.tempo=120;j.request.samplePosition=position;return j;
}
bool ramp(const a1::RenderPipeline::Job& j,a1::RenderPipeline::Result& r) {
    for(uint32_t i=0;i<j.request.frames;++i) {
        r.audio[0][i]=float(j.request.samplePosition+i+1);r.audio[1][i]=-r.audio[0][i];
    }
    return true;
}
}
int main() {
    {
        bool timed=true;
        for(int delay:{512,768}) {
            a1::RenderPipeline p(ramp,delay);float left[256]{},right[256]{};
            int position=0;
            for(int i=0;i<80;++i) {
                const int frames=i%3==0?64:i%3==1?192:256;
                timed&=p.process(job(frames,position),left,right,true);
                for(int sample=0;sample<frames;++sample) {
                    const float expected=position+sample<delay?0.0f:float(position+sample-delay+1);
                    timed&=left[sample]==expected && right[sample]==-expected;
                }
                position+=frames;
            }
            timed&=p.stats().missingFrames==0 && p.stats().outputDroppedFrames==0;
        }
        check(timed,"lower latency preserves exact stereo sample timing across variable blocks and FIFO wraparound");
    }
    {
        a1::RenderPipeline p(ramp,1024);float left[4096]{},right[4096]{};
        check(p.latency()==1024,"pipeline reports its fixed sample delay");
        bool correct=true;int position=0;
        for(int n:{64,192,128,256,112,144,256,512,256,64,192}) {
            correct&=p.process(job(n,position),left,right,true);
            for(int i=0;i<n;++i) {
                float expected=position+i<1024?0.0f:float(position+i-1024+1);
                correct&=left[i]==expected && right[i]==-expected;
            }
            position+=n;
        }
        check(correct,"offline and variable blocks preserve exact stereo timing and declared latency");
        check(p.stats().missingFrames==0,"offline export waits on its worker without missing audio");
    }
    {
        a1::RenderPipeline p(ramp,1024);float left[1]{},right[1]{};
        bool correct=true;
        for(int position=0;position<1600;++position) {
            correct&=p.process(job(1,position),left,right,true);
            const float expected=position<1024?0.0f:float(position-1024+1);
            correct&=left[0]==expected && right[0]==-expected;
        }
        check(correct,"one-frame offline jobs retain all audio throughout the initial latency");
        check(p.stats().missingFrames==0,"small offline jobs never lose due samples");
        check(p.stats().outputDroppedFrames==0,"output capacity is measured in samples rather than job count");
    }
    {
        a1::RenderPipeline p(ramp,1024);float left[4096]{},right[4096]{};
        check(!p.process(job(513,0),left,right,false),
              "realtime jobs exceeding half the latency are rejected without waiting");
        check(!p.failed(),"invalid realtime block size does not fail the worker");
        bool timed=p.process(job(4096,0),left,right,true);
        for(int i=0;i<4096;++i) {
            const float expected=i<1024?0.0f:float(i-1024+1);
            timed&=left[i]==expected && right[i]==-expected;
        }
        check(timed,"offline accepts large blocks and rejected realtime jobs do not advance time");
    }
    {
        a1::RenderPipeline p(ramp,12000);float left[4096]{},right[4096]{};
        bool correct=true;
        for(int position=0;position<40*4096;position+=4096) {
            correct&=p.process(job(4096,position),left,right,true);
            for(int i=0;i<4096;++i) {
                const float expected=position+i<12000?0.0f:float(position+i-12000+1);
                correct&=left[i]==expected && right[i]==-expected;
            }
        }
        check(correct,"sample FIFO grows with latency and preserves audio across repeated wraparound");
        check(p.stats().missingFrames==0 && p.stats().outputDroppedFrames==0,
              "large offline blocks retain every sample at a larger declared delay");
    }
    {
        std::atomic<bool> hold{true};
        a1::RenderPipeline p([&](const auto& j,auto& r) {
            while(hold.load())std::this_thread::sleep_for(50us);
            return ramp(j,r);
        },1024);
        float left[512]{},right[512]{};uint64_t accepted=0,rejected=0;
        for(int i=0;i<132;++i) {
            if(p.process(job(512,i*512),left,right,false))++accepted;else ++rejected;
        }
        check(rejected>0 && p.stats().inputDrops==rejected,
              "full input queue rejects excess jobs without waiting for a held worker");
        hold=false;
        for(int i=0;i<5000 && p.stats().rendered<accepted;++i)std::this_thread::sleep_for(100us);
        check(p.stats().rendered==accepted && p.stats().outputDroppedFrames>0,
              "full output queue counts dropped sample frames without blocking its worker");
        p.reset();bool correct=true;
        for(int position=0;position<=2048;position+=256) {
            correct&=p.process(job(256,position),left,right,true);
            for(int i=0;i<256;++i) {
                const float expected=position+i<1024?0.0f:float(position+i-1024+1);
                correct&=left[i]==expected && right[i]==-expected;
            }
        }
        check(correct,"reset clears an old full output queue before publishing new-epoch audio");
    }
    {
        std::atomic<bool> entered{false},hold{true};
        const auto caller=std::this_thread::get_id();std::atomic<bool> separate{false};
        a1::RenderPipeline p([&](const auto& j,auto& r) {
            separate=std::this_thread::get_id()!=caller;entered=true;
            while(hold.load())std::this_thread::sleep_for(100us);
            return ramp(j,r);
        },512);
        float left[256]{},right[256]{};
        auto call=std::async(std::launch::async,[&]{return p.process(job(256,0),left,right,false);});
        const bool returned=call.wait_for(20ms)==std::future_status::ready;
        check(returned,"audio submission returns while the helper is deliberately blocked");
        if(!returned)hold=false;
        (void)call.get();
        for(int i=0;i<100 && !entered.load();++i)std::this_thread::sleep_for(100us);
        check(entered.load() && separate.load(),"engine processing runs on a distinct worker thread");
        bool silent=true;
        for(int pos:{256,512,768}) {
            p.process(job(256,pos),left,right,false);
            for(float x:left)silent&=x==0;
        }
        check(silent && p.stats().missingFrames==512,"late helper audio becomes measured silence without blocking the host");
        hold=false;
        p.process(job(256,1024),left,right,true);
        check(left[0]==513 && right[255]==-768,"late blocks are discarded rather than replayed at the wrong time");
        check(p.stats().lateFrames>=512,"expired helper sample frames are counted");
    }
    {
        bool sounding=false;
        a1::RenderPipeline p([&](const auto& j,auto& r) {
            uint32_t event=0;
            for(uint32_t i=0;i<j.request.frames;++i) {
                while(event<j.request.midiCount && j.midi[event].frame==i) {
                    sounding=(j.midi[event].data[0]&0xf0)==0x90 && j.midi[event].data[2]!=0;++event;
                }
                r.audio[0][i]=r.audio[1][i]=sounding?0.25f:0.0f;
            }
            return true;
        },512);
        float left[256]{},right[256]{};bool timed=true;
        for(int position=0;position<1024;position+=256) {
            auto j=job(256,position);
            if(position==0) {j.request.midiCount=1;j.midi[0]={13,3,{0x90,60,100,0}};}
            if(position==256) {j.request.midiCount=1;j.midi[0]={91,3,{0x80,60,0,0}};}
            p.process(j,left,right,true);
            for(int i=0;i<256;++i)timed&=left[i]==((position+i>=525 && position+i<859)?0.25f:0.0f);
        }
        check(timed,"MIDI note-on and note-off offsets survive the asynchronous path");
        p.reset();p.process(job(256,0),left,right,true);
        check(std::all_of(std::begin(left),std::end(left),[](float x){return x==0;}),"reset discards audio from the previous processing epoch");
    }
    {
        std::vector<A1Process> requests;
        a1::RenderPipeline p([&](const auto& j,auto& r) {
            requests.push_back(j.request);return ramp(j,r);
        },512);
        float left[256]{},right[256]{};
        auto j=job(256,0);j.revisions[98]=uint64_t(7)<<32;
        p.process(j,left,right,true);
        j.request.parameters[98]=.2f;j.revisions[98]=(uint64_t(7)<<32)|std::bit_cast<uint32_t>(.2f);
        p.process(j,left,right,true);
        j.request.parameters[98]=.8f;j.revisions[98]=(uint64_t(7)<<32)|std::bit_cast<uint32_t>(.8f);
        p.process(j,left,right,true);
        j.request.parameters[98]=0;j.revisions[98]=uint64_t(8)<<32;
        p.process(j,left,right,true);p.process(j,left,right,true);
        p.reset();p.process(j,left,right,true);
        auto all=[](const auto& r) {return r.parameterChanges[0]==0xffffffffu && r.parameterChanges[1]==0xffffffffu
            && r.parameterChanges[2]==0xffffffffu && r.parameterChanges[3]==7u;};
        auto none=[](const auto& r) {return std::all_of(std::begin(r.parameterChanges),std::end(r.parameterChanges),[](auto x){return x==0;});};
        check(requests.size()==6 && std::all_of(requests.begin(),requests.end(),[](const auto& r){return r.reserved==A1_PROCESS_EXPLICIT_PARAMETERS;}),
              "worker explicitly distinguishes user edits from engine controller feedback");
        check(all(requests[0]) && none(requests[1]) && none(requests[2]),
              "readback float changes within one edit generation never become explicit setters");
        check(requests[3].parameterChanges[0]==0 && requests[3].parameterChanges[1]==0
              && requests[3].parameterChanges[2]==0 && requests[3].parameterChanges[3]==4u && none(requests[4]),
              "an explicit edit back to an old value applies once to its intended parameter");
        check(all(requests[5]),"a new processing epoch establishes a complete parameter snapshot");
    }
    {
        a1::RenderPipeline p([](const auto&,auto&){return false;},512);
        float left[256]{},right[256]{};
        check(!p.process(job(256,0),left,right,true) && p.failed(),"helper failure is observable and returns without an endless wait");
    }
    std::cout<<"Render pipeline checks: "<<checks<<", failures: "<<failures<<'\n';
    return failures?1:0;
}
