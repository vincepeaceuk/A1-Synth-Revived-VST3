#pragma once
#include <JuceHeader.h>
#include "Protocol.h"
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(__APPLE__)
#include <pthread.h>
#endif

namespace a1 {
class TimingHistogram {
public:
    void record(uint64_t ns) noexcept {
        const auto bin=std::min<unsigned>(31,std::bit_width(ns));
        bins[bin].fetch_add(1,std::memory_order_relaxed);
        count.fetch_add(1,std::memory_order_relaxed);
        auto old=maximum.load(std::memory_order_relaxed);
        while(old<ns && !maximum.compare_exchange_weak(old,ns,std::memory_order_relaxed)) {}
    }
    double percentileUs(double quantile) const noexcept {
        const auto n=count.load(std::memory_order_relaxed);if(!n)return 0;
        const auto target=uint64_t(std::ceil(double(n)*quantile));uint64_t total=0;
        for(unsigned i=0;i<32;++i) {
            total+=bins[i].load(std::memory_order_relaxed);
            if(total>=target)return double(uint64_t(1)<<i)/1000.0;
        }
        return double(maximum.load(std::memory_order_relaxed))/1000.0;
    }
    double maxUs() const noexcept {return double(maximum.load(std::memory_order_relaxed))/1000.0;}
private:
    std::array<std::atomic<uint64_t>,32> bins{};
    std::atomic<uint64_t> count{0},maximum{0};
};

// The single audio producer only touches bounded, preallocated SPSC queues.
// Every engine call, socket operation and wait belongs to the worker. Results
// have sample/epoch tags so a late response can never shift subsequent music.
class RenderPipeline {
public:
    struct Job {
        A1Process request{};
        std::array<A1Midi,A1_MAX_MIDI> midi{};
        std::array<uint64_t,A1_PARAMETERS> revisions{};
    };
    struct Result {
        float actual[A1_PARAMETERS]{};
        float audio[2][A1_MAX_FRAMES]{};
    };
    using Renderer=std::function<bool(const Job&,Result&)>;
    using Completion=std::function<void(const Job&,const Result&)>;
    struct Stats {
        uint64_t submitted,rendered,missingFrames,lateFrames,inputDrops,outputDroppedFrames;
        // Input depth counts render jobs; output depth counts stereo sample frames.
        int inputDepth,outputDepth;
        double callbackP50Us,callbackP95Us,callbackP99Us,callbackMaxUs,renderP99Us,renderMaxUs;
    };
    explicit RenderPipeline(Renderer render,int delay,Completion complete={})
      : renderer(std::move(render)),completion(std::move(complete)),delaySamples(std::max(1,delay)),
        storage(std::make_unique<Storage>(capacityForDelay(delaySamples))),
        output(int(storage->samples.size())),worker([this]{run();}) {}
    ~RenderPipeline() {stopping.store(true,std::memory_order_release);worker.join();}
    RenderPipeline(const RenderPipeline&)=delete;
    RenderPipeline& operator=(const RenderPipeline&)=delete;
    int latency() const noexcept {return delaySamples;}
    bool failed() const noexcept {return failure.load(std::memory_order_acquire);}
    // Host lifecycle/reset calls are serialized with process by the processor.
    // Do not reset FIFO indices while the worker may own a slot.
    void reset() noexcept {currentEpoch.fetch_add(1,std::memory_order_acq_rel);position=0;}
    Stats stats() const noexcept {
        return {submitted.load(),rendered.load(),missingFrames.load(),lateFrames.load(),inputDrops.load(),outputDroppedFrames.load(),
            input.getNumReady(),output.getNumReady(),callbackTimes.percentileUs(.5),callbackTimes.percentileUs(.95),
            callbackTimes.percentileUs(.99),callbackTimes.maxUs(),renderTimes.percentileUs(.99),renderTimes.maxUs()};
    }
    bool process(const Job& job,float* left,float* right,bool nonRealtime=false) noexcept {
        const auto started=Clock::now();
        struct Timer {
            TimingHistogram& histogram;Clock::time_point start;bool enabled;
            ~Timer() {if(enabled)histogram.record(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count()));}
        } timer{callbackTimes,started,!nonRealtime};
        const auto frames=job.request.frames;
        if(!frames || frames>A1_MAX_FRAMES || job.request.midiCount>A1_MAX_MIDI || !left || !right)return false;
        std::fill_n(left,frames,0.0f);std::fill_n(right,frames,0.0f);
        // A realtime job must be submitted at least one maximum render quantum
        // before any of its audio is due. Offline can wait for larger jobs.
        if(!nonRealtime && frames>uint32_t(delaySamples/2))return false;
        const auto first=position;position+=frames;
        if(failed() || stopping.load(std::memory_order_acquire))return false;
        const auto epoch=currentEpoch.load(std::memory_order_acquire);
        // Clear obsolete epochs/expired samples before an offline wait, so an
        // old full queue cannot cause the new epoch's first result to be lost.
        consume(first,0,epoch,left,right);
        const auto ticket=++nextTicket;
        const auto deadline=started+std::chrono::seconds(1);
        bool accepted=false;
        do {
            int a=0,n=0,b=0,m=0;input.prepareToWrite(1,a,n,b,m);
            if(n) {
                auto& slot=storage->jobs[size_t(a)];slot.job=job;slot.first=first;slot.epoch=epoch;slot.ticket=ticket;
                input.finishedWrite(1);submitted.fetch_add(1,std::memory_order_relaxed);accepted=true;break;
            }
            if(!nonRealtime)break;
            if(failed() || Clock::now()>=deadline) {failure.store(true,std::memory_order_release);return false;}
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        } while(true);
        if(!accepted)inputDrops.fetch_add(1,std::memory_order_relaxed);
        if(nonRealtime && accepted) {
            while(completedTicket.load(std::memory_order_acquire)<ticket) {
                if(failed() || Clock::now()>=deadline) {failure.store(true,std::memory_order_release);return false;}
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        consume(first,frames,epoch,left,right);
        return accepted && !failed();
    }
private:
    using Clock=std::chrono::steady_clock;
    static constexpr int slots=129;
    struct Input {Job job;uint64_t first=0,epoch=0,ticket=0;};
    struct Frame {uint64_t sample=0,epoch=0;float left=0,right=0;};
    struct Storage {
        explicit Storage(int capacity):samples(size_t(capacity)) {}
        std::array<Input,slots> jobs;
        std::vector<Frame> samples;
    };
    static int capacityForDelay(int delay) {
        const auto capacity=std::max<int64_t>(16384,2*int64_t(delay)+A1_MAX_FRAMES+1);
        if(capacity>std::numeric_limits<int>::max())throw std::length_error("A1 render delay is too large");
        return int(capacity);
    }
    void consume(uint64_t first,uint32_t frames,uint64_t epoch,float* left,float* right) noexcept {
        uint32_t cursor=first<uint64_t(delaySamples)?uint32_t(std::min<uint64_t>(frames,uint64_t(delaySamples)-first)):0;
        uint64_t wanted=first+cursor>=uint64_t(delaySamples)?first+cursor-uint64_t(delaySamples):0;
        uint64_t discarded=0,missing=0;
        // Take a bounded snapshot: the worker may publish while we consume.
        int budget=output.getNumReady();
        while(budget>0) {
            int a=0,n=0,b=0,m=0;output.prepareToRead(budget,a,n,b,m);
            if(!n)break;
            int used=0;
            for(;used<n;++used) {
                const auto& sample=storage->samples[size_t(a+used)];
                if(sample.epoch!=epoch || sample.sample<wanted) {++discarded;continue;}
                if(cursor==frames)break;
                if(sample.sample>wanted) {
                    const auto gap=uint32_t(std::min<uint64_t>(frames-cursor,sample.sample-wanted));
                    missing+=gap;cursor+=gap;wanted+=gap;
                    if(cursor==frames)break;
                }
                left[cursor]=sample.left;right[cursor]=sample.right;
                ++cursor;++wanted;
            }
            if(used) {output.finishedRead(used);budget-=used;}
            if(used<n)break;
        }
        missing+=frames-cursor;
        if(discarded)lateFrames.fetch_add(discarded,std::memory_order_relaxed);
        if(missing)missingFrames.fetch_add(missing,std::memory_order_relaxed);
    }
    void publish(const Input& item,const Result& result) noexcept {
        const auto frames=item.job.request.frames;
        int a=0,n=0,b=0,m=0;output.prepareToWrite(int(frames),a,n,b,m);
        auto write=[&](int start,int count,int offset) {
            for(int i=0;i<count;++i) {
                auto& sample=storage->samples[size_t(start+i)];
                sample.sample=item.first+uint32_t(offset+i);sample.epoch=item.epoch;
                sample.left=result.audio[0][offset+i];sample.right=result.audio[1][offset+i];
            }
        };
        write(a,n,0);write(b,m,n);
        if(n+m)output.finishedWrite(n+m);
        if(uint32_t(n+m)<frames)outputDroppedFrames.fetch_add(frames-uint32_t(n+m),std::memory_order_relaxed);
    }
    void run() noexcept {
        #if defined(__APPLE__)
        pthread_setname_np("A1 render worker");
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
        #endif
        auto item=std::make_unique<Input>();auto result=std::make_unique<Result>();
        std::array<uint32_t,A1_PARAMETERS> appliedGenerations{};uint64_t appliedEpoch=0;
        while(!stopping.load(std::memory_order_acquire)) {
            int a=0,n=0,b=0,m=0;input.prepareToRead(1,a,n,b,m);
            if(!n) {std::this_thread::sleep_for(std::chrono::microseconds(200));continue;}
            *item=storage->jobs[size_t(a)];input.finishedRead(1);
            if(item->epoch!=currentEpoch.load(std::memory_order_acquire)) {
                completedTicket.store(item->ticket,std::memory_order_release);continue;
            }
            // Readback changes the float bits within an edit generation. Only
            // explicit host/UI/preset edits may become original-engine setters;
            // queued snapshots of earlier MIDI feedback must not undo newer CCs.
            item->job.request.reserved=A1_PROCESS_EXPLICIT_PARAMETERS;
            std::fill(std::begin(item->job.request.parameterChanges),std::end(item->job.request.parameterChanges),0u);
            for(size_t i=0;i<A1_PARAMETERS;++i) {
                const auto generation=uint32_t(item->job.revisions[i]>>32);
                if(appliedEpoch!=item->epoch || appliedGenerations[i]!=generation)
                    item->job.request.parameterChanges[i/32]|=uint32_t(1)<<(i%32);
                appliedGenerations[i]=generation;
            }
            appliedEpoch=item->epoch;
            const auto start=Clock::now();
            if(!renderer(item->job,*result)) {failure.store(true,std::memory_order_release);break;}
            renderTimes.record(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count()));
            if(item->epoch==currentEpoch.load(std::memory_order_acquire)) {
                if(completion)completion(item->job,*result);
                publish(*item,*result);
                rendered.fetch_add(1,std::memory_order_relaxed);
            }
            completedTicket.store(item->ticket,std::memory_order_release);
        }
    }
    Renderer renderer;Completion completion;const int delaySamples;
    std::unique_ptr<Storage> storage;
    juce::AbstractFifo input{slots},output;
    std::atomic<bool> stopping{false},failure{false};
    std::atomic<uint64_t> currentEpoch{1},completedTicket{0};
    uint64_t position=0,nextTicket=0;
    std::atomic<uint64_t> submitted{0},rendered{0},missingFrames{0},lateFrames{0},inputDrops{0},outputDroppedFrames{0};
    TimingHistogram callbackTimes,renderTimes;
    std::thread worker;
};
}
