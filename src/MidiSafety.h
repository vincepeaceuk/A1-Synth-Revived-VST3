#pragma once
#include <JuceHeader.h>
#include "Protocol.h"
#include <array>
#include <atomic>

namespace a1 {
// A missed note-off must never become an indefinitely held original voice.
// Oversized bursts lose one musical block explicitly and reset old voices;
// regular packets preserve every event and its sample offset.
class MidiSafety {
public:
    struct Packet {
        std::array<A1Midi,A1_MAX_MIDI> events{};
        uint32_t count=0;
        bool overflow=false;
    };
    void missedBlock() {resetRequired.store(true);}
    Packet makePacket(const juce::MidiBuffer& midi,int start,int frames) {
        Packet result;
        if(frames<=0)return result;
        const bool reset=resetRequired.exchange(false);
        uint32_t count=reset?32u:0u;
        auto eligible=[&](const auto& item) {
            return item.samplePosition>=start && item.samplePosition-start<frames
                && item.numBytes>0 && item.numBytes<=3 && !item.getMessage().isProgramChange();
        };
        for(const auto item:midi)if(eligible(item) && ++count>A1_MAX_MIDI) {
            result.overflow=true;break;
        }
        if(reset || result.overflow) {
            for(unsigned channel=0;channel<16;++channel) {
                result.events[result.count++]={0,3,{uint8_t(0xb0+channel),64,0,0}};
                result.events[result.count++]={0,3,{uint8_t(0xb0+channel),123,0,0}};
            }
        }
        if(!result.overflow)for(const auto item:midi)if(eligible(item)) {
            auto& event=result.events[result.count++];
            event.frame=uint32_t(item.samplePosition-start);event.size=uint32_t(item.numBytes);
            std::memcpy(event.data,item.data,size_t(item.numBytes));
        }
        return result;
    }
private:
    std::atomic<bool> resetRequired{false};
};
}
