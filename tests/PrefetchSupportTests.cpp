#include "../src/PrefetchSupport.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <type_traits>

namespace {
int checks = 0;
void check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

// Keep the test's wire IDs independent of the implementation and external SDK
// iid definitions. This target must link using only the public SDK headers.
const Steinberg::TUID prefetchId =
    INLINE_UID(0x8AE54FDA, 0xE93046B9, 0xA28555BC, 0xDC98E21E);
const Steinberg::TUID unknownId =
    INLINE_UID(0x00000000, 0x00000000, 0xC0000000, 0x00000046);
const Steinberg::TUID unsupportedId =
    INLINE_UID(0x8AE54FDA, 0xE93046B9, 0xA28555BC, 0xDC98E21F);
}

int main() {
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    static_assert(!std::is_copy_constructible_v<a1::PrefetchSupport>);
    static_assert(!std::is_move_constructible_v<a1::PrefetchSupport>);

    auto* support = a1::PrefetchSupport::create();
    check(support != nullptr, "factory returns the initial owned reference");
    PrefetchableSupport value = kIsYetPrefetchable;
    check(support->getPrefetchableSupport(value) == kResultOk,
          "capability query succeeds");
    check(value == kIsNeverPrefetchable, "all instances reject prefetch");
    value = kIsNotYetPrefetchable;
    check(support->getPrefetchableSupport(value) == kResultOk
              && value == kIsNeverPrefetchable,
          "capability does not depend on its prior output value");

    void* object = support;
    check(support->queryInterface(unsupportedId, &object) == kNoInterface,
          "similar but unsupported interface is rejected");
    check(object == nullptr, "unsupported query clears the output");
    check(support->addRef() == 2, "failed query did not take a reference");
    check(support->release() == 1, "explicit reference can be released");
    check(support->queryInterface(prefetchId, nullptr) == kInvalidArgument,
          "null output argument is rejected");
    object = support;
    check(support->queryInterface(nullptr, &object) == kInvalidArgument,
          "null interface ID is rejected");
    check(object == nullptr, "invalid ID query clears the output");
    check(support->addRef() == 2, "invalid queries did not take references");
    check(support->release() == 1, "invalid queries preserve owner lifetime");

    object = nullptr;
    check(support->queryInterface(prefetchId, &object) == kResultOk,
          "prefetch interface can be queried");
    auto* acquired = static_cast<IPrefetchableSupport*>(object);
    check(acquired == static_cast<IPrefetchableSupport*>(support),
          "query returns the actual interface subobject");
    check(support->release() == 1,
          "queried reference keeps the object alive after owner release");
    value = kIsYetPrefetchable;
    check(acquired->getPrefetchableSupport(value) == kResultOk
              && value == kIsNeverPrefetchable,
          "acquired interface remains usable without the original owner");

    void* identity = nullptr;
    check(acquired->queryInterface(unknownId, &identity) == kResultOk,
          "canonical unknown identity is supported");
    auto* unknown = static_cast<FUnknown*>(identity);
    check(unknown == static_cast<FUnknown*>(acquired),
          "unknown identity is stable for the standalone object");
    void* roundTrip = nullptr;
    check(unknown->queryInterface(prefetchId, &roundTrip) == kResultOk,
          "unknown identity can query the prefetch interface again");
    check(roundTrip == acquired, "interface query round trip preserves identity");
    check(static_cast<IPrefetchableSupport*>(roundTrip)->release() == 2,
          "round-trip query owns a separate reference");
    check(unknown->release() == 1, "unknown query owns a separate reference");
    check(acquired->release() == 0, "final reference destroys the object");

    auto* concurrent = a1::PrefetchSupport::create();
    std::atomic<bool> badCount{false};
    std::array<std::thread, 4> threads;
    for (auto& thread : threads) {
        thread = std::thread([&] {
            for (int i = 0; i < 10000; ++i) {
                if (concurrent->addRef() < 2 || concurrent->release() < 1)
                    badCount.store(true, std::memory_order_relaxed);
            }
        });
    }
    for (auto& thread : threads)
        thread.join();
    check(!badCount.load(), "concurrent reference operations preserve owner");
    check(concurrent->addRef() == 2, "concurrent references leave no leaks");
    check(concurrent->release() == 1, "owner is the only remaining reference");
    check(concurrent->release() == 0, "concurrent fixture releases its owner");

    std::cout << "PASS: " << checks << " prefetch capability/lifetime checks\n";
}
