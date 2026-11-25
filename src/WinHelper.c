/* Original Windows x86 A1 engine helper, used only by the genuine Cubase path.
 * No original instructions are patched. Protocol traffic is binary; diagnostics
 * go to stderr. The native parent owns host verification and process lifetime.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Protocol.h"

/* Independent declarations of the public VST2 ABI, as in engine_probe.c. */
typedef struct Effect Effect;
typedef intptr_t (*Callback)(Effect *, int32_t, int32_t, intptr_t, void *, float);
struct Effect {
    int32_t magic;
    Callback dispatch;
    void (*process)(Effect *, float **, float **, int32_t);
    void (*setParameter)(Effect *, int32_t, float);
    float (*getParameter)(Effect *, int32_t);
    int32_t programs, parameters, inputs, outputs, flags;
    intptr_t reserved1, reserved2;
    int32_t latency, quality1, quality2;
    float ratio;
    void *object, *user;
    int32_t id, version;
    void (*replace)(Effect *, float **, float **, int32_t);
    void (*doubleReplace)(Effect *, double **, double **, int32_t);
    char future[56];
};
typedef struct {
    double samplePosition, sampleRate, nanoseconds, ppqPosition, tempo;
    double barStart, cycleStart, cycleEnd;
    int32_t numerator, denominator, smpteOffset, smpteRate, nextClock, flags;
} TimeInfo;
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    uint8_t data[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} MidiEvent;
typedef struct {
    int32_t count;
    intptr_t reserved;
    MidiEvent *events[A1_MAX_MIDI];
} EventList;

_Static_assert(sizeof(void *) == 4, "Build the helper for Windows x86");
_Static_assert(sizeof(Effect) == 144, "VST2 x86 effect ABI");
_Static_assert(sizeof(MidiEvent) == 32, "VST2 MIDI event ABI");
#define A1_CAN_REPLACING 0x10

/* Observed private host-identification ABI in this exact original A1 build. */
#define A1_CHALLENGE_RVA 0x26fc8u
#define A1_CHALLENGE_XOR 0x0b6840cau
#define A1_HOST_SIGNATURE_0 0xb38d823au
#define A1_HOST_SIGNATURE_1 0x0136728fu
#define A1_HOST_SIGNATURE_2 0xab34aa87u
typedef struct {
    uint32_t signature[3], reserved;
    uintptr_t vendor, challenge;
} LegacyIdentity;
_Static_assert(sizeof(LegacyIdentity) == 24, "Original x86 host query ABI");

typedef struct {
    A1Host host;
    HMODULE module;
    uintptr_t moduleBase;
    size_t moduleSize;
    Effect *effect;
    int authorized, opened, resumed, cacheValid, challengeReady, midiDiagnosticLogged;
    uintptr_t challengeBlock;
    uint32_t challengeSeed, program, blockSize;
    double sampleRate;
    TimeInfo clockInfo;
    /* Requests and actual engine values must remain separate: original MIDI CC
     * changes must not be overwritten by an unchanged frontend request. */
    float requestedParameters[A1_PARAMETERS];
    float actualParameters[A1_PARAMETERS];
    float audio[2][A1_MAX_FRAMES];
} HelperState;
static HelperState state;

/* Wine maps this relative priority to interactive latency/throughput QoS on
 * macOS. Only the render thread is raised; the process class and watchdog
 * remain normal. The loop sleeps on input between bounded render requests. */
static int configureRenderThread(void) {
    /* Match the normal x86 Windows/MSVC double-precision arithmetic contract.
     * MinGW starts at extended precision instead. The accelerated translator
     * uses binary64 arithmetic; retain the caller's rounding, masks and SSE
     * state rather than resetting the floating-point environment. */
    uint16_t control;
    __asm__ volatile("fnstcw %0" : "=m"(control));
    control = (uint16_t)((control & ~0x0300u) | 0x0200u);
    __asm__ volatile("fldcw %0" :: "m"(control));
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) != 0;
}

static int accelerationReady(void) {
    char value[2] = {0};
    return GetEnvironmentVariableA("A1_X87_READY", value, sizeof(value)) == 1 && value[0] == '1';
}

static int fixedStringValid(const char *text, size_t capacity) {
    for (size_t i = 0; i < capacity; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!c) return i != 0;
        if (c < 0x20 || c > 0x7e) return 0;
    }
    return 0;
}

static int hostAccepted(const A1Host *host) {
    if (!fixedStringValid(host->product, sizeof(host->product)) ||
        !fixedStringValid(host->vendor, sizeof(host->vendor)) ||
        strcmp(host->vendor, "Steinberg")) return 0;
    return !strcmp(host->product, "Cubase") ||
           (!strncmp(host->product, "Cubase ", 7) && host->product[7] != '\0');
}

/* Every pointer supplied by the legacy binary is checked before copying. */
static int spanAccessible(const void *pointer, size_t size, int writable) {
    uintptr_t address = (uintptr_t)pointer;
    if (!address || !size || size > UINTPTR_MAX - address) return 0;
    uintptr_t end = address + size;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region;
        if (VirtualQuery((void *)address, &region, sizeof(region)) != sizeof(region) ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return 0;
        DWORD permissions = region.Protect & 0xff;
        DWORD allowed = writable
            ? PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY
            : PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
              PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (!(permissions & allowed)) return 0;
        uintptr_t base = (uintptr_t)region.BaseAddress;
        if (region.RegionSize > UINTPTR_MAX - base) return 0;
        uintptr_t next = base + region.RegionSize;
        if (next <= address) return 0;
        address = next < end ? next : end;
    }
    return 1;
}

static int readMemory(const void *source, void *destination, size_t bytes) {
    SIZE_T copied = 0;
    return spanAccessible(source, bytes, 0) &&
           ReadProcessMemory(GetCurrentProcess(), source, destination, bytes, &copied) &&
           copied == bytes;
}

static int writeMemory(void *destination, const void *source, size_t bytes) {
    SIZE_T copied = 0;
    return spanAccessible(destination, bytes, 1) &&
           WriteProcessMemory(GetCurrentProcess(), destination, source, bytes, &copied) &&
           copied == bytes;
}

static int moduleContains(uintptr_t address, size_t size) {
    return state.moduleBase && address >= state.moduleBase &&
           size <= state.moduleSize && address - state.moduleBase <= state.moduleSize - size;
}

static int writeHostString(void *destination, const char *text) {
    return writeMemory(destination, text, strlen(text) + 1);
}

static int capabilitySupported(const void *pointer) {
    char text[64];
    if (!pointer) return 0;
    for (size_t i = 0; i < sizeof(text); ++i) {
        if (!readMemory((const char *)pointer + i, &text[i], 1)) return 0;
        if (!text[i])
            return !strcmp(text, "sendVstEvents") || !strcmp(text, "sendVstMidiEvent") ||
                   !strcmp(text, "sendVstTimeInfo");
    }
    return 0;
}

static int legacyHostQuery(void *pointer) {
    LegacyIdentity request;
    uint32_t challenge[2];
    if (!state.authorized || !hostAccepted(&state.host) || !state.challengeReady ||
        !readMemory(pointer, &request, sizeof(request)) ||
        request.signature[0] != A1_HOST_SIGNATURE_0 ||
        request.signature[1] != A1_HOST_SIGNATURE_1 ||
        request.signature[2] != A1_HOST_SIGNATURE_2 ||
        request.challenge != state.challengeBlock ||
        !moduleContains(request.challenge, sizeof(challenge)) ||
        !readMemory((void *)request.challenge, challenge, sizeof(challenge)) ||
        challenge[1] != state.challengeSeed ||
        !spanAccessible((void *)request.vendor, sizeof("Steinberg"), 1) ||
        !spanAccessible((void *)request.challenge, sizeof(challenge), 1)) return 0;
    /* Reject overlap rather than letting a malformed vendor pointer corrupt the
     * challenge block. Both ranges were checked for overflow above. */
    if (request.vendor < request.challenge + sizeof(challenge) &&
        request.challenge < request.vendor + sizeof("Steinberg")) return 0;
    uint32_t response = challenge[1] ^ A1_CHALLENGE_XOR;
    if (!writeHostString((void *)request.vendor, state.host.vendor) ||
        !writeMemory((void *)(request.challenge + sizeof(uint32_t)), &response, sizeof(response)))
        return 0;
    state.challengeReady = 0;
    return 1;
}

static intptr_t a1Host(Effect *effect, int32_t op, int32_t index,
                       intptr_t value, void *pointer, float option) {
    (void)effect; (void)option;
    switch (op) {
        case 1: { /* audioMasterVersion, with A1's optional private challenge. */
            uint32_t challenge[2];
            if (state.authorized && !index && !value &&
                (uintptr_t)pointer == state.moduleBase + A1_CHALLENGE_RVA &&
                moduleContains((uintptr_t)pointer, sizeof(challenge)) &&
                spanAccessible(pointer, sizeof(challenge), 1) &&
                readMemory(pointer, challenge, sizeof(challenge))) {
                state.challengeBlock = (uintptr_t)pointer;
                state.challengeSeed = challenge[1];
                state.challengeReady = 1;
            }
            return 2400;
        }
        case 2: return 0x33455353; /* Original VST ID: 3ESS. */
        case 6: return 1; /* MIDI events supplied by the real host. */
        case 7: return (intptr_t)&state.clockInfo;
        case 16: return (intptr_t)state.sampleRate;
        case 17: return state.blockSize;
        case 23: return 2; /* Realtime processing. */
        case 32: return state.authorized && writeHostString(pointer, state.host.vendor);
        case 33: return state.authorized && writeHostString(pointer, state.host.product);
        case 34: return 1;
        case 35: return !index && !value && legacyHostQuery(pointer);
        case 37: return capabilitySupported(pointer);
        case 38: return 1; /* English. */
        default: return 0;
    }
}

static unsigned midiBytes(uint8_t status) {
    if (status < 0x80) return 0;
    if (status < 0xf0) return (status & 0xe0) == 0xc0 ? 2 : 3;
    switch (status) {
        case 0xf1: case 0xf3: return 2;
        case 0xf2: return 3;
        case 0xf6: case 0xf8: case 0xfa: case 0xfb: case 0xfc: case 0xfe: case 0xff: return 1;
        default: return 0; /* SysEx needs a different VST2 event structure. */
    }
}

static int processPayloadValid(const A1Process *process, const A1Midi *midi,
                               uint32_t bytes, uint32_t programs) {
    if (!process->frames || process->frames > A1_MAX_FRAMES ||
        process->midiCount > A1_MAX_MIDI || !programs || process->program >= 512 ||
        (process->reserved & ~A1_PROCESS_EXPLICIT_PARAMETERS) ||
        bytes != sizeof(*process) + process->midiCount * sizeof(A1Midi) ||
        !isfinite(process->sampleRate) || process->sampleRate < 8000 || process->sampleRate > 384000 ||
        !isfinite(process->tempo) || process->tempo <= 0 || process->tempo > 1000 ||
        !isfinite(process->samplePosition)) return 0;
    if (process->parameterChanges[3] & ~7u) return 0; /* Only parameters 96..98. */
    if (!(process->reserved & A1_PROCESS_EXPLICIT_PARAMETERS)) {
        for (unsigned i = 0; i < 4; ++i) if (process->parameterChanges[i]) return 0;
    }
    for (unsigned i = 0; i < A1_PARAMETERS; ++i)
        if (!isfinite(process->parameters[i]) || process->parameters[i] < 0 ||
            process->parameters[i] > 1) return 0;
    for (unsigned i = 0; i < process->midiCount; ++i) {
        if (midi[i].frame >= process->frames || midi[i].size != midiBytes(midi[i].data[0]) ||
            !midi[i].size || midi[i].size > 3 || (i && midi[i].frame < midi[i - 1].frame)) return 0;
        for (unsigned j = 1; j < midi[i].size; ++j) if (midi[i].data[j] > 127) return 0;
    }
    return 1;
}

static const char *processAudio(const A1Process *process, const A1Midi *midi) {
    Effect *effect = state.effect;
    /* VST2 blockSize is allocated capacity. Shorter render calls must not
     * suspend the synth: A1's suspend method releases every held voice. */
    if (state.sampleRate != process->sampleRate || state.blockSize < process->frames) {
        if (state.resumed) effect->dispatch(effect, 12, 0, 0, NULL, 0);
        state.resumed = 0;
        state.sampleRate = process->sampleRate;
        if (state.blockSize < process->frames) state.blockSize = process->frames;
        effect->dispatch(effect, 10, 0, 0, NULL, (float)state.sampleRate);
        effect->dispatch(effect, 11, 0, state.blockSize, NULL, 0);
        effect->dispatch(effect, 12, 0, 1, NULL, 0);
        state.resumed = 1;
    }
    state.clockInfo.samplePosition = process->samplePosition;
    state.clockInfo.sampleRate = process->sampleRate;
    state.clockInfo.tempo = process->tempo;
    state.clockInfo.ppqPosition = process->samplePosition / process->sampleRate * process->tempo / 60;
    state.clockInfo.barStart = floor(state.clockInfo.ppqPosition / 4) * 4;
    state.clockInfo.numerator = state.clockInfo.denominator = 4;
    state.clockInfo.flags = (1 << 9) | (1 << 10) | (1 << 11) | (1 << 13);
    if (state.program != process->program) {
        effect->dispatch(effect, 2, 0, process->program % (uint32_t)effect->programs, NULL, 0);
        state.program = process->program;
        state.cacheValid = 0;
    }
    const int explicitParameters = (process->reserved & A1_PROCESS_EXPLICIT_PARAMETERS) != 0;
    for (unsigned i = 0; i < A1_PARAMETERS; ++i) {
        /* Readback snapshots can lag behind newer controller events queued on
         * this thread. Only an explicit edit may set their parameter again. */
        const int changed = explicitParameters
            ? (process->parameterChanges[i / 32] & (1u << (i % 32))) != 0
            : state.requestedParameters[i] != process->parameters[i];
        if (!state.cacheValid || changed) {
            effect->setParameter(effect, (int32_t)i, process->parameters[i]);
            state.requestedParameters[i] = process->parameters[i];
        }
    }
    state.cacheValid = 1;
    if (process->midiCount) {
        MidiEvent events[A1_MAX_MIDI];
        EventList list;
        memset(&list, 0, sizeof(list)); memset(events, 0, sizeof(events));
        list.count = (int32_t)process->midiCount;
        for (unsigned i = 0; i < process->midiCount; ++i) {
            events[i].type = 1; events[i].byteSize = sizeof(MidiEvent);
            events[i].deltaFrames = (int32_t)midi[i].frame;
            memcpy(events[i].data, midi[i].data, midi[i].size);
            list.events[i] = &events[i];
        }
        effect->dispatch(effect, 25, 0, 0, &list, 0);
    }
    memset(state.audio[0], 0, process->frames * sizeof(float));
    memset(state.audio[1], 0, process->frames * sizeof(float));
    float *channels[2] = {state.audio[0], state.audio[1]};
    /* Old VST SDKs install a non-null processReplacing trampoline even when
     * the instrument only implements accumulating process(). Respect the
     * advertised flag; a pointer by itself does not establish support. */
    if (effect->flags & A1_CAN_REPLACING) {
        if (!effect->replace) return "Original A1 replacing callback is missing";
        effect->replace(effect, NULL, channels, (int32_t)process->frames);
    } else {
        if (!effect->process) return "Original A1 accumulating callback is missing";
        effect->process(effect, NULL, channels, (int32_t)process->frames);
    }
    for (unsigned channel = 0; channel < 2; ++channel)
        for (unsigned i = 0; i < process->frames; ++i)
            if (!isfinite(state.audio[channel][i])) return "Original A1 returned nonfinite audio";
    for (unsigned i = 0; i < A1_PARAMETERS; ++i) {
        float value = effect->getParameter(effect, (int32_t)i);
        if (!isfinite(value) || value < 0 || value > 1)
            return "Original A1 returned an invalid parameter value";
        state.actualParameters[i] = value;
    }
    if (process->midiCount && !state.midiDiagnosticLogged) {
        double peak = 0;
        for (unsigned channel = 0; channel < 2; ++channel)
            for (unsigned i = 0; i < process->frames; ++i) {
                double sample = fabs((double)state.audio[channel][i]);
                if (sample > peak) peak = sample;
            }
        fprintf(stderr, "A1 helper: first MIDI block frames=%u program=%u events=%u "
                "first=%02x %02x %02x frame=%u rate=%.0f tempo=%.3f "
                "amp51=%.9g voices4=%.9g controller98=%.9g peak=%.9g\n",
                process->frames, process->program, process->midiCount,
                midi[0].data[0], midi[0].data[1], midi[0].data[2], midi[0].frame,
                process->sampleRate, process->tempo,
                (double)state.actualParameters[51], (double)state.actualParameters[4],
                (double)state.actualParameters[98], peak);
        fflush(stderr); state.midiDiagnosticLogged = 1;
    }
    return NULL;
}

/* Translate first-use DSP paths before audio deadlines apply. The caller owns
 * a disposable effect and must close it and create a fresh real instance after
 * this function; no warm-up audio or parameter state may reach the client. */
static const char *warmupEffect(void) {
    Effect *effect = state.effect;
    if (!effect || effect->programs < 1) return "A1 warm-up has no effect";
    const int diagnostic = state.midiDiagnosticLogged;
    state.midiDiagnosticLogged = 1;
    A1Process process;
    memset(&process, 0, sizeof(process));
    process.frames = 256; process.tempo = 120;
    process.reserved = A1_PROCESS_EXPLICIT_PARAMETERS;
    for (unsigned i = 0; i < 4; ++i) process.parameterChanges[i] = i == 3 ? 7u : UINT32_MAX;
    const double rates[] = {44100, 48000};
    for (unsigned rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
        process.sampleRate = rates[rate];
        for (unsigned program = 0; program < (unsigned)effect->programs && program < 128; ++program) {
            effect->dispatch(effect, 2, 0, program, NULL, 0);
            state.program = process.program = program; state.cacheValid = 0;
            for (unsigned i = 0; i < A1_PARAMETERS; ++i) {
                const float value = effect->getParameter(effect, (int32_t)i);
                if (!isfinite(value) || value < 0 || value > 1)
                    return "A1 warm-up returned an invalid factory parameter";
                process.parameters[i] = value;
            }
            process.parameters[4] = 1; /* All sixteen voices. */
            process.parameters[51] = 0.5f;
            process.parameters[54] = process.parameters[55] = 1; /* Quad chorus. */
            process.parameters[98] = 1;
            for (unsigned block = 0; block < 4; ++block) {
                A1Midi events[16]; memset(events, 0, sizeof(events));
                if (block == 0 || block == 2) {
                    process.midiCount = 16;
                    for (unsigned note = 0; note < 16; ++note) {
                        events[note].size = 3;
                        events[note].data[0] = block == 0 ? 0x90 : 0x80;
                        events[note].data[1] = (uint8_t)(36 + 3 * note);
                        events[note].data[2] = block == 0 ? 100 : 0;
                    }
                } else {
                    process.midiCount = 1; events[0].size = 3;
                    events[0].data[0] = 0xb0; events[0].data[1] = 1;
                    events[0].data[2] = block == 1 ? 96 : 0;
                }
                const char *error = processAudio(&process, events);
                if (error) return error;
                process.samplePosition += process.frames;
            }
        }
    }
    effect->dispatch(effect, 12, 0, 0, NULL, 0); state.resumed = 0;
    state.midiDiagnosticLogged = diagnostic;
    return NULL;
}

static int executablePointer(const void *pointer) {
    MEMORY_BASIC_INFORMATION region;
    return pointer && VirtualQuery(pointer, &region, sizeof(region)) == sizeof(region) &&
           region.State == MEM_COMMIT && !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           (region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                              PAGE_EXECUTE_WRITECOPY));
}

/* Preserve the loaded module and translator while replacing only its effect. */
static void disposeEffect(void) {
    if (state.effect && state.opened) {
        if (state.resumed) state.effect->dispatch(state.effect, 12, 0, 0, NULL, 0);
        state.effect->dispatch(state.effect, 1, 0, 0, NULL, 0);
    }
    state.effect = NULL; state.opened = state.resumed = state.cacheValid = 0;
    state.midiDiagnosticLogged = 0;
}

static const char *openEffect(Effect *(*entry)(Callback)) {
    state.sampleRate = 44100; state.blockSize = A1_MAX_FRAMES;
    state.program = UINT32_MAX; state.cacheValid = state.resumed = state.opened = 0;
    memset(&state.clockInfo, 0, sizeof(state.clockInfo));
    state.clockInfo.sampleRate = 44100; state.clockInfo.tempo = 120;
    state.clockInfo.numerator = state.clockInfo.denominator = 4;
    state.clockInfo.flags = (1 << 9) | (1 << 10) | (1 << 13);
    memset(state.requestedParameters, 0, sizeof(state.requestedParameters));
    memset(state.actualParameters, 0, sizeof(state.actualParameters));
    memset(state.audio, 0, sizeof(state.audio));
    Effect *effect = entry(a1Host);
    Effect description;
    if (!readMemory(effect, &description, sizeof(description)) ||
        description.magic != 0x56737450 || description.id != 0x33455353 ||
        description.parameters != A1_PARAMETERS || description.inputs != 0 || description.outputs != 2 ||
        description.programs < 1 || description.programs > 4096 ||
        !executablePointer((void *)description.dispatch) ||
        !executablePointer((void *)description.setParameter) ||
        !executablePointer((void *)description.getParameter) ||
        !executablePointer((description.flags & A1_CAN_REPLACING)
                               ? (void *)description.replace : (void *)description.process))
        return "Original A1 returned an unexpected VST interface";
    state.effect = effect;
    effect->dispatch(effect, 0, 0, 0, NULL, 0); state.opened = 1;
    effect->dispatch(effect, 10, 0, 0, NULL, 44100);
    effect->dispatch(effect, 11, 0, state.blockSize, NULL, 0);
    effect->dispatch(effect, 2, 0, 0, NULL, 0);
    effect->dispatch(effect, 12, 0, 1, NULL, 0); state.resumed = 1;
    return NULL;
}

static const char *prepareFreshEffect(Effect *(*entry)(Callback)) {
    const DWORD started = GetTickCount();
    const char *error = openEffect(entry);
    if (error) return error;
    error = warmupEffect();
    disposeEffect();
    if (error) return error;
    error = openEffect(entry);
    if (!error) {
        fprintf(stderr, "A1 helper: discarded-instance DSP warm-up ready in %lu ms\n",
                (unsigned long)(GetTickCount() - started)); fflush(stderr);
    }
    return error;
}

#ifndef A1_HELPER_UNIT_TEST
static HANDLE watchdogStop;
static volatile LONG watchdogStarted, watchdogLimit;

static void deadline(unsigned milliseconds) {
    InterlockedExchange(&watchdogStarted, (LONG)GetTickCount());
    InterlockedExchange(&watchdogLimit, (LONG)milliseconds);
}

static void deadlineDone(void) { InterlockedExchange(&watchdogLimit, 0); }

static DWORD WINAPI watchdog(void *unused) {
    (void)unused;
    while (WaitForSingleObject(watchdogStop, 100) == WAIT_TIMEOUT) {
        LONG limit = InterlockedCompareExchange(&watchdogLimit, 0, 0);
        DWORD started = (DWORD)InterlockedCompareExchange(&watchdogStarted, 0, 0);
        if (limit > 0 && (DWORD)(GetTickCount() - started) >= (DWORD)limit) {
            fprintf(stderr, "A1 helper operation timed out after %ld ms\n", (long)limit);
            fflush(stderr); ExitProcess(124);
        }
    }
    return 0;
}

/* 1 = complete, 0 = clean EOF before any byte, -1 = error/truncation. */
static int readExact(void *destination, uint32_t bytes) {
    unsigned char *cursor = destination;
    uint32_t done = 0;
    while (done < bytes) {
        DWORD count = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), cursor + done, bytes - done, &count, NULL)) {
            if (!done && GetLastError() == ERROR_BROKEN_PIPE) return 0;
            return -1;
        }
        if (!count) return done ? -1 : 0;
        done += count;
    }
    return 1;
}

static int writeExact(const void *source, uint32_t bytes) {
    const unsigned char *cursor = source;
    uint32_t done = 0;
    while (done < bytes) {
        DWORD count = 0;
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), cursor + done, bytes - done, &count, NULL) ||
            !count) return 0;
        done += count;
    }
    return 1;
}

static int reply(uint32_t command, uint32_t status, const void *body, uint32_t bytes) {
    A1Header header = {A1_MAGIC, command, bytes, status};
    return writeExact(&header, sizeof(header)) && (!bytes || writeExact(body, bytes));
}

static int errorReply(uint32_t command, const char *message) {
    fprintf(stderr, "A1 helper: %s\n", message); fflush(stderr);
    return reply(command, A1_ERROR, message, (uint32_t)strlen(message));
}

static void closeEffect(void) {
    disposeEffect();
    if (state.module) FreeLibrary(state.module);
    state.module = NULL;
}

static const char *initialize(const char *path, A1Metadata *metadata) {
    if (!accelerationReady()) return "The accelerated A1 engine did not start";
    fprintf(stderr, "A1 helper: verified Cubase context: %.63s\n", state.host.product);
    state.sampleRate = 44100; state.blockSize = A1_MAX_FRAMES;
    state.clockInfo.sampleRate = 44100; state.clockInfo.tempo = 120;
    state.clockInfo.numerator = state.clockInfo.denominator = 4;
    state.clockInfo.flags = (1 << 9) | (1 << 10) | (1 << 13);
    state.module = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!state.module) return "Unable to load the original A1 DLL";
    state.moduleBase = (uintptr_t)state.module;
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    if (!readMemory(state.module, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 || dos.e_lfanew > 4096 ||
        !readMemory((unsigned char *)state.module + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.OptionalHeader.SizeOfImage < A1_CHALLENGE_RVA + 8 ||
        nt.OptionalHeader.SizeOfImage > 16 * 1024 * 1024)
        return "Original A1 has an unexpected loaded image";
    state.moduleSize = nt.OptionalHeader.SizeOfImage;
    typedef Effect *(*Entry)(Callback);
    Entry entry = (Entry)(void *)GetProcAddress(state.module, "main");
    if (!entry || !executablePointer((void *)entry)) return "Original A1 entry point is missing";
    fprintf(stderr, "A1 helper: entering original engine\n"); fflush(stderr);
    const char *error = prepareFreshEffect(entry);
    if (error) return error;
    Effect *effect = state.effect;
    memset(metadata, 0, sizeof(*metadata));
    metadata->parameters = A1_PARAMETERS; metadata->programs = (uint32_t)effect->programs;
    metadata->outputs = 2; metadata->version = (uint32_t)effect->version;
    for (unsigned i = 0; i < A1_PARAMETERS; ++i) {
        char name[128] = {0}, label[128] = {0};
        effect->dispatch(effect, 8, (int32_t)i, 0, name, 0);
        effect->dispatch(effect, 6, (int32_t)i, 0, label, 0);
        name[sizeof(name) - 1] = label[sizeof(label) - 1] = '\0';
        snprintf(metadata->names[i], sizeof(metadata->names[i]), "%s", name);
        snprintf(metadata->labels[i], sizeof(metadata->labels[i]), "%s", label);
    }
    fprintf(stderr, "A1 helper: original flags=0x%08x processing=%s\n",
            (unsigned)effect->flags, (effect->flags & A1_CAN_REPLACING)
                ? "processReplacing" : "accumulating process");
    fprintf(stderr, "A1 helper: original engine ready (%u parameters, %u programs)\n",
            metadata->parameters, metadata->programs); fflush(stderr);
    return NULL;
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc != 2) { fprintf(stderr, "usage: WinHelper.exe ORIGINAL_A1.dll\n"); return 2; }
    watchdogStop = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!watchdogStop) return 3;
    HANDLE supervisor = CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    if (!supervisor) { CloseHandle(watchdogStop); return 3; }
    const int prioritySet = configureRenderThread();
    fprintf(stderr, "A1 helper: render priority=%d configured=%d process_class=%lu\n",
            GetThreadPriority(GetCurrentThread()), prioritySet,
            (unsigned long)GetPriorityClass(GetCurrentProcess()));
    int result = 0, greeted = 0;
    for (;;) {
        A1Header header;
        int read = readExact(&header, sizeof(header));
        if (read <= 0) { result = read < 0 ? 4 : 0; break; }
        deadline(greeted ? 5000 : 15000);
        if (header.magic != A1_MAGIC || header.status != A1_OK) {
            errorReply(header.command, "Invalid bridge message header"); result = 4; break;
        }
        if (!greeted) {
            A1Host host;
            if (header.command != A1_HELLO || header.bytes != sizeof(host) ||
                readExact(&host, sizeof(host)) != 1 || !hostAccepted(&host)) {
                errorReply(header.command, "A verified Cubase host context is required"); result = 5; break;
            }
            state.host = host; state.authorized = 1;
            A1Metadata metadata;
            const char *error = initialize(argv[1], &metadata);
            if (error) { errorReply(A1_HELLO, error); result = 6; break; }
            if (!reply(A1_HELLO, A1_OK, &metadata, sizeof(metadata))) { result = 7; break; }
            greeted = 1; deadlineDone(); continue;
        }
        if (header.command == A1_QUIT && header.bytes == 0) {
            closeEffect(); reply(A1_QUIT, A1_OK, NULL, 0); deadlineDone(); break;
        }
        if (header.command != A1_PROCESS || header.bytes < sizeof(A1Process) ||
            header.bytes > sizeof(A1Process) + A1_MAX_MIDI * sizeof(A1Midi)) {
            errorReply(header.command, "Invalid bridge command or payload size"); result = 4; break;
        }
        struct { A1Process process; A1Midi midi[A1_MAX_MIDI]; } input;
        if (readExact(&input, header.bytes) != 1 ||
            !processPayloadValid(&input.process, input.midi, header.bytes, (uint32_t)state.effect->programs)) {
            errorReply(A1_PROCESS, "Invalid audio or MIDI payload"); result = 4; break;
        }
        const char *error = processAudio(&input.process, input.midi);
        if (error) { errorReply(A1_PROCESS, error); result = 8; break; }
        uint32_t channelBytes = input.process.frames * sizeof(float);
        A1Header response = {A1_MAGIC, A1_PROCESS,
                             sizeof(state.actualParameters) + 2 * channelBytes, A1_OK};
        if (!writeExact(&response, sizeof(response)) ||
            !writeExact(state.actualParameters, sizeof(state.actualParameters)) ||
            !writeExact(state.audio[0], channelBytes) || !writeExact(state.audio[1], channelBytes)) {
            result = 7; break;
        }
        deadlineDone();
    }
    deadline(5000); closeEffect(); deadlineDone();
    SetEvent(watchdogStop); WaitForSingleObject(supervisor, 1000);
    CloseHandle(supervisor); CloseHandle(watchdogStop);
    return result;
}
#endif
