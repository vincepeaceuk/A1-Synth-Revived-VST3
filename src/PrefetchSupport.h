#pragma once

#include "pluginterfaces/vst/ivstprefetchablesupport.h"

#include <atomic>
#include <cstring>

namespace a1 {

// The asynchronous render queue needs time to produce each block. Disable
// faster-than-realtime prefetch without declaring that offline export is
// unsupported. The owner and every successful query each hold one reference.
class PrefetchSupport final : public Steinberg::Vst::IPrefetchableSupport {
public:
    static PrefetchSupport* create() { return new PrefetchSupport; }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                                void** object) override {
        if (object == nullptr)
            return Steinberg::kInvalidArgument;
        *object = nullptr;
        if (iid == nullptr)
            return Steinberg::kInvalidArgument;

        // Inline IDs avoid linking SDK-owned static FUID definitions into the
        // plug-in and the standalone interface-contract test target.
        const Steinberg::TUID prefetchId =
            INLINE_UID(0x8AE54FDA, 0xE93046B9, 0xA28555BC, 0xDC98E21E);
        const Steinberg::TUID unknownId =
            INLINE_UID(0x00000000, 0x00000000, 0xC0000000, 0x00000046);
        if (std::memcmp(iid, prefetchId, sizeof(Steinberg::TUID)) == 0)
            *object = static_cast<Steinberg::Vst::IPrefetchableSupport*>(this);
        else if (std::memcmp(iid, unknownId, sizeof(Steinberg::TUID)) == 0)
            *object = static_cast<Steinberg::FUnknown*>(this);
        else
            return Steinberg::kNoInterface;

        addRef();
        return Steinberg::kResultOk;
    }

    Steinberg::uint32 PLUGIN_API addRef() override {
        return references.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    Steinberg::uint32 PLUGIN_API release() override {
        const auto remaining = references.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    Steinberg::tresult PLUGIN_API getPrefetchableSupport(
        Steinberg::Vst::PrefetchableSupport& support) override {
        support = Steinberg::Vst::kIsNeverPrefetchable;
        return Steinberg::kResultOk;
    }

    PrefetchSupport(const PrefetchSupport&) = delete;
    PrefetchSupport& operator=(const PrefetchSupport&) = delete;

private:
    PrefetchSupport() = default;
    ~PrefetchSupport() = default;

    std::atomic<Steinberg::uint32> references{1};
};

} // namespace a1
