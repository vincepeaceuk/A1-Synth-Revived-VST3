/* Windows-only fixtures. Never loads or executes the original synthesizer. */
#define A1_HELPER_UNIT_TEST 1
#include "../src/WinHelper.c"

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)

static const char *warmupEffect(void);
static const char *prepareFreshEffect(Effect *(*entry)(Callback));

static unsigned setCalls, programCalls, midiCalls, renderCalls;
static unsigned accumulationCalls, silentReplacementCalls;
static unsigned mainsCalls;
static int heldNote;
static int mainsOn, rateChangedWhileOn;
static int nonfiniteAudio, invalidReadback;
static uint32_t warmupPrograms;
static unsigned warmupNoteOns, warmupNoteOffs;
static int warmupVoices, warmupChorus;
static unsigned fixtureCreations, fixtureOpens, fixtureCloses;
static int failSecondCreation;
static int32_t capturedFrames[A1_MAX_MIDI];
static uint8_t capturedMidi[A1_MAX_MIDI][4];
static float fixtureParameters[A1_PARAMETERS];
static float fixtureControllerValue;

static intptr_t fixtureDispatch(Effect *e, int32_t op, int32_t index,
                                intptr_t value, void *ptr, float option) {
    (void)e; (void)index; (void)option;
    if (op == 0) ++fixtureOpens;
    if (op == 1) ++fixtureCloses;
    if (op == 2) { ++programCalls; if (value >= 0 && value < 32) warmupPrograms |= 1u << value; }
    if (op == 10 && mainsOn) rateChangedWhileOn = 1;
    if (op == 12) { ++mainsCalls; mainsOn = value != 0; if (!mainsOn) heldNote = 0; }
    if (op == 25) {
        EventList *events = ptr;
        ++midiCalls;
        for (int32_t i = 0; i < events->count; ++i) {
            capturedFrames[i] = events->events[i]->deltaFrames;
            memcpy(capturedMidi[i], events->events[i]->data, 4);
            if (events->events[i]->data[0] == 0xb0) fixtureParameters[10] = fixtureControllerValue;
            if ((events->events[i]->data[0] & 0xf0) == 0x90) heldNote = 1;
            if ((events->events[i]->data[0] & 0xf0) == 0x80) heldNote = 0;
            if ((events->events[i]->data[0] & 0xf0) == 0x90) ++warmupNoteOns;
            if ((events->events[i]->data[0] & 0xf0) == 0x80) ++warmupNoteOffs;
        }
    }
    return 1;
}

static void fixtureSet(Effect *e, int32_t index, float value) {
    (void)e; fixtureParameters[index] = value; ++setCalls;
    if (index == 4 && value == 1) warmupVoices = 1;
    if (index == 54 && value == 1) warmupChorus = 1;
}

static float fixtureGet(Effect *e, int32_t index) {
    (void)e; return invalidReadback ? NAN : fixtureParameters[index];
}

static void fixtureRender(Effect *e, float **in, float **out, int32_t frames) {
    (void)e; CHECK(in == NULL); ++renderCalls;
    for (int32_t i = 0; i < frames; ++i) {
        out[0][i] = 0.25f;
        out[1][i] = -0.5f;
    }
    if (nonfiniteAudio) out[0][0] = INFINITY;
}

static void fixtureAccumulate(Effect *e, float **in, float **out, int32_t frames) {
    (void)e; (void)in; ++accumulationCalls;
    for (int32_t i = 0; i < frames; ++i) {
        out[0][i] += 0.75f;
        out[1][i] -= 0.25f;
    }
}

static void fixtureSilentReplacement(Effect *e, float **in, float **out, int32_t frames) {
    (void)e; (void)in; (void)out; (void)frames; ++silentReplacementCalls;
}

static void fixtureHeldRender(Effect *e, float **in, float **out, int32_t frames) {
    (void)e; (void)in;
    for (int32_t i = 0; i < frames; ++i) out[0][i] = out[1][i] = heldNote ? 0.25f : 0.0f;
}

static Effect fixtureEffect;

static void resetFixture(void) {
    memset(&state, 0, sizeof(state));
    memset(&fixtureEffect, 0, sizeof(fixtureEffect));
    fixtureEffect.magic = 0x56737450;
    fixtureEffect.id = 0x33455353;
    fixtureEffect.dispatch = fixtureDispatch;
    fixtureEffect.setParameter = fixtureSet;
    fixtureEffect.getParameter = fixtureGet;
    fixtureEffect.replace = fixtureRender;
    fixtureEffect.programs = 128;
    fixtureEffect.parameters = A1_PARAMETERS;
    fixtureEffect.outputs = 2;
    fixtureEffect.flags = 0x10; /* This fixture explicitly advertises replacing. */
    state.effect = &fixtureEffect;
    state.sampleRate = 44100;
    state.blockSize = 256;
    state.resumed = 1;
    state.program = 0;
    strcpy(state.host.product, "Cubase 15");
    strcpy(state.host.vendor, "Steinberg");
    state.authorized = 1;
    memset(fixtureParameters, 0, sizeof(fixtureParameters));
    setCalls = programCalls = midiCalls = renderCalls = 0;
    accumulationCalls = silentReplacementCalls = 0;
    mainsCalls = 0; heldNote = 0;
    mainsOn = 1; rateChangedWhileOn = 0;
    nonfiniteAudio = invalidReadback = 0;
    warmupPrograms = 0; warmupNoteOns = warmupNoteOffs = 0;
    warmupVoices = warmupChorus = 0;
    fixtureCreations = fixtureOpens = fixtureCloses = 0; failSecondCreation = 0;
    fixtureControllerValue = 0.875f;
}

static A1Process validProcess(void);

static void testAdvertisedProcessingMode(void) {
    resetFixture();
    A1Process p = validProcess();
    fixtureEffect.flags = 0;
    fixtureEffect.process = fixtureAccumulate;
    fixtureEffect.replace = fixtureSilentReplacement; /* Non-null SDK stub. */
    state.audio[0][0] = 9.0f; state.audio[1][0] = 9.0f;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(accumulationCalls == 1 && silentReplacementCalls == 0);
    CHECK(state.audio[0][0] == 0.75f && state.audio[1][255] == -0.25f);
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(state.audio[0][0] == 0.75f && state.audio[1][255] == -0.25f);
    fixtureEffect.flags = 0x10;
    fixtureEffect.replace = fixtureRender;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(accumulationCalls == 2 && renderCalls == 1);
    CHECK(state.audio[0][0] == 0.25f && state.audio[1][255] == -0.5f);
}

static void testVariableFramesPreserveHeldNotes(void) {
    resetFixture();
    state.blockSize = A1_MAX_FRAMES; /* Allocated VST2 capacity, not actual frames. */
    fixtureEffect.replace = fixtureHeldRender;
    A1Process p = validProcess(); p.frames = 512; p.midiCount = 1;
    A1Midi note = {0, 3, {0x90, 48, 100, 0}};
    CHECK(processAudio(&p, &note) == NULL);
    CHECK(heldNote && state.audio[0][511] == 0.25f);
    p.frames = 128; p.midiCount = 0;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(heldNote && state.audio[0][127] == 0.25f);
    p.frames = A1_MAX_FRAMES;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(heldNote && state.audio[1][A1_MAX_FRAMES - 1] == 0.25f);
    CHECK(mainsCalls == 0 && state.blockSize == A1_MAX_FRAMES);
    p.sampleRate = 48000;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(mainsCalls == 2 && !rateChangedWhileOn && state.blockSize == A1_MAX_FRAMES);
}

static A1Process validProcess(void) {
    A1Process p;
    memset(&p, 0, sizeof(p));
    p.frames = 256;
    p.sampleRate = 44100;
    p.tempo = 123;
    p.samplePosition = 44100;
    for (unsigned i = 0; i < A1_PARAMETERS; ++i) p.parameters[i] = 0.5f;
    return p;
}

static void testHostValidation(void) {
    A1Host h = {{0}, {0}};
    strcpy(h.product, "Cubase"); strcpy(h.vendor, "Steinberg");
    CHECK(hostAccepted(&h));
    strcpy(h.product, "Cubase 15"); CHECK(hostAccepted(&h));
    strcpy(h.product, "CubaseFake"); CHECK(!hostAccepted(&h));
    strcpy(h.product, "REAPER"); CHECK(!hostAccepted(&h));
    strcpy(h.product, "Cubase"); strcpy(h.vendor, "Other");
    CHECK(!hostAccepted(&h));
    strcpy(h.vendor, "Steinberg"); memset(h.product, 'A', 64);
    CHECK(!hostAccepted(&h));
    strcpy(h.product, "Cubase\n15"); CHECK(!hostAccepted(&h));
}

static void testProcessValidation(void) {
    A1Process p = validProcess();
    A1Midi midi[A1_MAX_MIDI]; memset(midi, 0, sizeof(midi));
    CHECK(processPayloadValid(&p, midi, sizeof(p), 128));
    CHECK(!processPayloadValid(&p, midi, sizeof(p) - 1, 128));
    CHECK(!processPayloadValid(&p, midi, sizeof(p) + 1, 128));
    p.frames = 0; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p.frames = A1_MAX_FRAMES; CHECK(processPayloadValid(&p, midi, sizeof(p), 128));
    ++p.frames; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.midiCount = A1_MAX_MIDI + 1;
    CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.program = 255; CHECK(processPayloadValid(&p, midi, sizeof(p), 128));
    p.program = 512; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.sampleRate = NAN; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.sampleRate = 0; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.tempo = INFINITY; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.samplePosition = -INFINITY; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.parameters[98] = NAN; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.parameters[0] = -0.01f; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.parameters[0] = 1.01f; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.reserved = 2; CHECK(!processPayloadValid(&p, midi, sizeof(p), 128));
    p = validProcess(); p.midiCount = A1_MAX_MIDI;
    for (unsigned i = 0; i < A1_MAX_MIDI; ++i) {
        midi[i].frame = i; midi[i].size = 3;
        midi[i].data[0] = 0x90; midi[i].data[1] = 48; midi[i].data[2] = 100;
    }
    uint32_t bytes = sizeof(p) + sizeof(midi);
    CHECK(processPayloadValid(&p, midi, bytes, 128));
    midi[255].frame = p.frames; CHECK(!processPayloadValid(&p, midi, bytes, 128));
    midi[255].frame = 255; midi[0].size = 4;
    CHECK(!processPayloadValid(&p, midi, bytes, 128));
    midi[0].size = 3; midi[0].data[0] = 0x70;
    CHECK(!processPayloadValid(&p, midi, bytes, 128));
    midi[0].data[0] = 0x90; midi[0].data[1] = 128;
    CHECK(!processPayloadValid(&p, midi, bytes, 128));
}

static void testGuardedLegacyIdentity(void) {
    resetFixture();
    char hostText[64] = {0};
    CHECK(a1Host(NULL, 32, 0, 0, hostText, 0) == 1);
    CHECK(!strcmp(hostText, "Steinberg"));
    CHECK(a1Host(NULL, 33, 0, 0, hostText, 0) == 1);
    CHECK(!strcmp(hostText, "Cubase 15"));
    CHECK(a1Host(NULL, 32, 0, 0, (void *)(uintptr_t)1, 0) == 0);
    CHECK(a1Host(NULL, 37, 0, 0, "sendVstMidiEvent", 0) == 1);
    CHECK(a1Host(NULL, 37, 0, 0, "unsupportedCapability", 0) == 0);
    CHECK(a1Host(NULL, 37, 0, 0, (void *)(uintptr_t)1, 0) == 0);
    unsigned char *module = VirtualAlloc(NULL, 0x30000,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(module != NULL);
    state.moduleBase = (uintptr_t)module; state.moduleSize = 0x30000;
    uint32_t *block = (uint32_t *)(module + A1_CHALLENGE_RVA);
    block[0] = 0x12345678u; block[1] = 0x89abcdefu;
    char vendor[64] = {0};
    LegacyIdentity request = {{A1_HOST_SIGNATURE_0, A1_HOST_SIGNATURE_1,
                               A1_HOST_SIGNATURE_2}, 0, (uintptr_t)vendor,
                              (uintptr_t)block};
    CHECK(a1Host(NULL, 35, 0, 0, &request, 0) == 0);
    CHECK(block[1] == 0x89abcdefu);
    CHECK(a1Host(NULL, 1, 0, 0, block, 0) == 2400);
    CHECK(a1Host(NULL, 35, 0, 0, &request, 0) == 1);
    CHECK(!strcmp(vendor, "Steinberg"));
    CHECK(block[0] == 0x12345678u);
    CHECK(block[1] == (0x89abcdefu ^ A1_CHALLENGE_XOR));
    CHECK(a1Host(NULL, 35, 0, 0, &request, 0) == 0);
    CHECK(a1Host(NULL, 35, 0, 0, (void *)(uintptr_t)1, 0) == 0);
    block[1] = 7;
    CHECK(a1Host(NULL, 1, 0, 0, block, 0) == 2400);
    request.signature[2] ^= 1;
    CHECK(a1Host(NULL, 35, 0, 0, &request, 0) == 0);
    CHECK(block[1] == 7);
    request.signature[2] ^= 1;
    unsigned char *readOnly = VirtualAlloc(NULL, 4096,
                                          MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(readOnly != NULL);
    DWORD old;
    CHECK(VirtualProtect(readOnly, 4096, PAGE_READONLY, &old));
    request.vendor = (uintptr_t)readOnly;
    CHECK(a1Host(NULL, 35, 0, 0, &request, 0) == 0);
    CHECK(block[1] == 7);
    request.vendor = (uintptr_t)vendor;
    state.authorized = 0;
    CHECK(a1Host(NULL, 35, 0, 0, &request, 0) == 0);
    CHECK(block[1] == 7);
    CHECK(!spanAccessible((void *)(uintptr_t)1, 4, 0));
    CHECK(!spanAccessible((void *)(uintptr_t)(UINTPTR_MAX - 2), 8, 0));
    VirtualFree(readOnly, 0, MEM_RELEASE); VirtualFree(module, 0, MEM_RELEASE);
}

static void testProcessing(void) {
    resetFixture();
    A1Process p = validProcess();
    A1Midi events[2] = {{0, 3, {0x90, 48, 100, 0}},
                        {255, 3, {0x80, 48, 0, 0}}};
    p.midiCount = 2;
    CHECK(processAudio(&p, events) == NULL);
    CHECK(setCalls == 99 && renderCalls == 1 && midiCalls == 1);
    CHECK(capturedFrames[0] == 0 && capturedFrames[1] == 255);
    CHECK(capturedMidi[0][0] == 0x90 && capturedMidi[1][0] == 0x80);
    CHECK(state.audio[0][255] == 0.25f && state.audio[1][255] == -0.5f);
    CHECK(state.clockInfo.samplePosition == 44100 && state.clockInfo.tempo == 123);
    CHECK(processAudio(&p, events) == NULL); CHECK(setCalls == 99);
    p.parameters[52] = 0.75f;
    CHECK(processAudio(&p, events) == NULL); CHECK(setCalls == 100);
    p.program = 1;
    CHECK(processAudio(&p, events) == NULL);
    CHECK(programCalls == 1 && setCalls == 199);
    p.program = 129;
    CHECK(processAudio(&p, events) == NULL);
    CHECK(programCalls == 2 && setCalls == 298 && state.program == 129);
    p.sampleRate = 48000; p.frames = 512;
    CHECK(processAudio(&p, events) == NULL);
    CHECK(state.sampleRate == 48000 && state.blockSize == 512);
    CHECK(!rateChangedWhileOn && mainsOn);
    A1Midi cc = {0, 3, {0xb0, 1, 100, 0}};
    p.midiCount = 1;
    CHECK(processAudio(&p, &cc) == NULL);
    CHECK(state.actualParameters[10] == 0.875f);
    unsigned previousCalls = setCalls;
    p.midiCount = 0;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(setCalls == previousCalls && state.actualParameters[10] == 0.875f);
    A1Midi burst[A1_MAX_MIDI];
    p.frames = A1_MAX_FRAMES; p.midiCount = A1_MAX_MIDI;
    for (unsigned i = 0; i < A1_MAX_MIDI; ++i) {
        burst[i] = (A1Midi){i, 3, {0x90, 48, 100, 0}};
    }
    burst[A1_MAX_MIDI - 1].frame = A1_MAX_FRAMES - 1;
    CHECK(processAudio(&p, burst) == NULL);
    CHECK(capturedFrames[A1_MAX_MIDI - 1] == A1_MAX_FRAMES - 1);
    CHECK(state.audio[0][A1_MAX_FRAMES - 1] == 0.25f);
    nonfiniteAudio = 1;
    CHECK(processAudio(&p, burst) != NULL);
    nonfiniteAudio = 0; invalidReadback = 1;
    CHECK(processAudio(&p, burst) != NULL);
}

static void testExplicitParameterChanges(void) {
    resetFixture();
    A1Process p = validProcess();
    p.reserved = A1_PROCESS_EXPLICIT_PARAMETERS;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(setCalls == A1_PARAMETERS); /* Initial engine state always applies all. */

    A1Midi cc = {0, 3, {0xb0, 1, 32, 0}};
    p.midiCount = 1; fixtureControllerValue = 0.25f;
    CHECK(processAudio(&p, &cc) == NULL);
    float olderReadback = state.actualParameters[10];
    CHECK(olderReadback == 0.25f);
    fixtureControllerValue = 0.75f;
    CHECK(processAudio(&p, &cc) == NULL);
    CHECK(state.actualParameters[10] == 0.75f);

    /* A later queued job may contain the older readback in the same edit
     * generation. Without a dirty bit it must not become a setter. */
    p.midiCount = 0; p.parameters[10] = olderReadback;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(state.actualParameters[10] == 0.75f && setCalls == A1_PARAMETERS);
    p.parameters[10] = 0.875f;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(state.actualParameters[10] == 0.75f && setCalls == A1_PARAMETERS);

    /* A real edit to the *old requested* value must still reach the engine. */
    p.parameters[10] = 0.5f; p.parameterChanges[0] = 1u << 10;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(state.actualParameters[10] == 0.5f && setCalls == A1_PARAMETERS + 1);
    memset(p.parameterChanges, 0, sizeof(p.parameterChanges));
    const unsigned boundary[] = {31, 32, 63, 64, 95, 96, 98};
    for (unsigned i = 0; i < sizeof(boundary) / sizeof(boundary[0]); ++i) {
        unsigned index = boundary[i];
        p.parameterChanges[index / 32] |= 1u << (index % 32);
        p.parameters[index] = 0.625f;
    }
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(setCalls == A1_PARAMETERS + 8);
    for (unsigned i = 0; i < sizeof(boundary) / sizeof(boundary[0]); ++i)
        CHECK(state.actualParameters[boundary[i]] == 0.625f);

    memset(p.parameterChanges, 0, sizeof(p.parameterChanges));
    p.program = 1;
    const unsigned beforeProgram = setCalls;
    CHECK(processAudio(&p, NULL) == NULL);
    CHECK(programCalls == 1 && setCalls == beforeProgram + A1_PARAMETERS);
    CHECK(state.actualParameters[10] == 0.5f && state.actualParameters[98] == 0.625f);
}

static void testExplicitMaskValidation(void) {
    A1Process p = validProcess();
    p.reserved = A1_PROCESS_EXPLICIT_PARAMETERS;
    CHECK(processPayloadValid(&p, NULL, sizeof(p), 128));
    p.parameterChanges[0] = p.parameterChanges[1] = p.parameterChanges[2] = UINT32_MAX;
    p.parameterChanges[3] = 7;
    CHECK(processPayloadValid(&p, NULL, sizeof(p), 128));
    p.parameterChanges[3] = 8;
    CHECK(!processPayloadValid(&p, NULL, sizeof(p), 128));
    p.parameterChanges[3] = 0x80000000u;
    CHECK(!processPayloadValid(&p, NULL, sizeof(p), 128));
    p = validProcess(); p.parameterChanges[0] = 1;
    CHECK(!processPayloadValid(&p, NULL, sizeof(p), 128));
    p = validProcess(); p.parameterChanges[3] = 4;
    CHECK(!processPayloadValid(&p, NULL, sizeof(p), 128));
    p = validProcess(); p.reserved = A1_PROCESS_EXPLICIT_PARAMETERS | 2;
    CHECK(!processPayloadValid(&p, NULL, sizeof(p), 128));
    p.reserved = A1_PROCESS_EXPLICIT_PARAMETERS | 0x80000000u;
    CHECK(!processPayloadValid(&p, NULL, sizeof(p), 128));
}

static void testAccelerationReadiness(void) {
    char previous[32] = {0};
    DWORD length = GetEnvironmentVariableA("A1_X87_READY", previous, sizeof(previous));
    CHECK(length < sizeof(previous));
    CHECK(SetEnvironmentVariableA("A1_X87_READY", NULL));
    CHECK(!accelerationReady());
    CHECK(SetEnvironmentVariableA("A1_X87_READY", "0"));
    CHECK(!accelerationReady());
    CHECK(SetEnvironmentVariableA("A1_X87_READY", "11"));
    CHECK(!accelerationReady());
    CHECK(SetEnvironmentVariableA("A1_X87_READY", "1"));
    CHECK(accelerationReady());
    CHECK(SetEnvironmentVariableA("A1_X87_READY", length ? previous : NULL));
}

static void testWindowsPrecision(void) {
    uint16_t original, observed;
    uint32_t originalMxcsr, observedMxcsr;
    __asm__ volatile("fnstcw %0" : "=m"(original));
    __asm__ volatile("stmxcsr %0" : "=m"(originalMxcsr));
    const uint16_t controls[] = { 0x037f, 0x0f7f };
    for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        __asm__ volatile("fldcw %0" :: "m"(controls[i]));
        CHECK(configureRenderThread());
        __asm__ volatile("fnstcw %0" : "=m"(observed));
        __asm__ volatile("stmxcsr %0" : "=m"(observedMxcsr));
        CHECK((observed & 0x0300u) == 0x0200u);
        CHECK((observed & ~0x0300u) == (controls[i] & ~0x0300u));
        CHECK(observedMxcsr == originalMxcsr);
    }
    __asm__ volatile("fldcw %0" :: "m"(original));
}

static void testDiscardedStartupWarmup(void) {
    resetFixture(); fixtureEffect.programs = 3;
    CHECK(warmupEffect() == NULL);
    CHECK((warmupPrograms & 7u) == 7u);
    CHECK(renderCalls >= 12 && renderCalls <= 96);
    CHECK(warmupNoteOns >= 48 && warmupNoteOffs >= 48);
    CHECK(warmupVoices && warmupChorus);
    CHECK(!mainsOn && !state.resumed && !heldNote);
    resetFixture(); nonfiniteAudio = 1;
    CHECK(warmupEffect() != NULL);
    resetFixture(); invalidReadback = 1;
    CHECK(warmupEffect() != NULL);
}

static Effect freshFixtureEffects[2];
static Effect *fixtureCreate(Callback callback) {
    CHECK(callback == a1Host);
    CHECK(state.effect == NULL);
    if (failSecondCreation && fixtureCreations == 1) return NULL;
    CHECK(fixtureCreations < 2);
    Effect *effect = &freshFixtureEffects[fixtureCreations++];
    *effect = fixtureEffect;
    for (unsigned i = 0; i < A1_PARAMETERS; ++i) fixtureParameters[i] = 0.25f;
    heldNote = 0;
    mainsOn = 0;
    return effect;
}

static void testFreshInstanceAfterWarmup(void) {
    resetFixture(); fixtureEffect.programs = 3; state.effect = NULL;
    state.module = (HMODULE)(uintptr_t)0x1234;
    CHECK(prepareFreshEffect(fixtureCreate) == NULL);
    CHECK(fixtureCreations == 2 && fixtureOpens == 2 && fixtureCloses == 1);
    CHECK(state.effect == &freshFixtureEffects[1] && state.opened && state.resumed);
    CHECK(!heldNote && !state.cacheValid && !state.midiDiagnosticLogged);
    CHECK(state.sampleRate == 44100 && state.blockSize == A1_MAX_FRAMES);
    CHECK(state.program == UINT32_MAX && fixtureParameters[4] == 0.25f && fixtureParameters[98] == 0.25f);
    CHECK(state.authorized && !strcmp(state.host.product, "Cubase 15") && state.module == (HMODULE)(uintptr_t)0x1234);
    CHECK(state.audio[0][0] == 0 && state.audio[1][A1_MAX_FRAMES - 1] == 0);
    resetFixture(); fixtureEffect.programs = 1; state.effect = NULL; failSecondCreation = 1;
    CHECK(prepareFreshEffect(fixtureCreate) != NULL);
    CHECK(fixtureCreations == 1 && fixtureCloses == 1 && !state.effect && !state.opened);
    resetFixture(); state.effect = NULL; nonfiniteAudio = 1;
    CHECK(prepareFreshEffect(fixtureCreate) != NULL);
    CHECK(fixtureCreations == 1 && fixtureCloses == 1 && !state.effect && !state.opened);
}

int main(void) {
    const int previousPriority = GetThreadPriority(GetCurrentThread());
    const DWORD previousClass = GetPriorityClass(GetCurrentProcess());
    CHECK(configureRenderThread());
    CHECK(GetThreadPriority(GetCurrentThread()) == THREAD_PRIORITY_HIGHEST);
    CHECK(GetPriorityClass(GetCurrentProcess()) == previousClass);
    testWindowsPrecision();
    testDiscardedStartupWarmup();
    testFreshInstanceAfterWarmup();
    testAccelerationReadiness();
    CHECK(SetThreadPriority(GetCurrentThread(), previousPriority));
    testHostValidation(); testProcessValidation();
    testGuardedLegacyIdentity(); testProcessing();
    testAdvertisedProcessingMode();
    testVariableFramesPreserveHeldNotes();
    testExplicitParameterChanges();
    testExplicitMaskValidation();
    printf("WinHelper fixture checks passed: %u\n", checks);
    return 0;
}
