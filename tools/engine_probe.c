/* A small, independent VST2 ABI probe. It never runs an installer.
 * This is an offline investigation tool, not an audio plug-in or bridge.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
    unsigned char data[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} MidiEvent;
typedef struct { int32_t count; intptr_t reserved; MidiEvent *events[2]; } Events;

_Static_assert(sizeof(void *) == 4, "Build the probe for Windows x86");
_Static_assert(sizeof(Effect) == 144, "VST2 x86 effect ABI");
_Static_assert(sizeof(MidiEvent) == 32, "VST2 MIDI event ABI");

static TimeInfo clockInfo = {0, 44100, 0, 0, 120, 0, 0, 0, 4, 4, 0, 0, 0,
                            (1 << 9) | (1 << 10) | (1 << 13)};
static uintptr_t moduleBase;

static intptr_t host(Effect *effect, int32_t op, int32_t index,
                     intptr_t value, void *ptr, float option) {
    static unsigned calls = 0;
    if (++calls <= 64) fprintf(stderr, "host_callback op=%d index=%d value=%ld ptr=%p option=%g\n",
                               op, index, (long) value, ptr, (double) option);
    if (op == 35 && ptr) {
        unsigned char bytes[64];
        SIZE_T count = 0;
        if (ReadProcessMemory(GetCurrentProcess(), ptr, bytes, sizeof(bytes), &count)) {
            fprintf(stderr, "vendor_query caller_rva=%08lx bytes=",
                    (unsigned long) ((uintptr_t) __builtin_return_address(0) - moduleBase));
            for (SIZE_T i = 0; i < count; ++i) fprintf(stderr, "%02x", bytes[i]);
            fprintf(stderr, "\n");
        }
    }
    (void) effect; (void) index; (void) value; (void) option;
    switch (op) {
        case 1: return 2400; /* audioMasterVersion */
        case 6: return 1;    /* audioMasterWantMidi */
        case 7: return (intptr_t) &clockInfo;
        case 16: return 44100;
        case 17: return 256;
        case 23: return 4;   /* offline process level */
        case 32: if (ptr) strcpy((char *) ptr, "Local Research"); return 1;
        case 33: if (ptr) strcpy((char *) ptr, "A1 Engine Probe"); return 1;
        case 34: return 1;
        case 37:
            if (!ptr) return 0;
            return !strcmp(ptr, "sendVstEvents") ||
                   !strcmp(ptr, "sendVstMidiEvent") ||
                   !strcmp(ptr, "sendVstTimeInfo");
        case 38: return 1;
        default: return 0;
    }
}

static void midi(Effect *e, unsigned char status, unsigned char note,
                 unsigned char velocity) {
    MidiEvent event = {1, 32, 0, 0, 0, 0, {status, note, velocity, 0}, 0, 0, 0, 0};
    Events events = {1, 0, {&event, NULL}};
    e->dispatch(e, 25, 0, 0, &events, 0);
}

static uint32_t be32(const unsigned char *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

/* This bank has old zero byteSize fields. Walk by parameter count, with bounds. */
static int loadProgram(Effect *e, const char *path, unsigned slot) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("bank"); return 0; }
    unsigned char bank[65536];
    size_t size = fread(bank, 1, sizeof(bank), f);
    int tooLarge = fgetc(f) != EOF;
    int badRead = ferror(f);
    fclose(f);
    if (tooLarge || badRead || size < 156 || memcmp(bank, "CcnK", 4) ||
        memcmp(bank + 8, "FxBk", 4) || be32(bank + 16) != (uint32_t) e->id ||
        slot >= be32(bank + 24)) return 0;
    size_t offset = 156;
    for (unsigned i = 0; i <= slot; ++i) {
        if (offset > size || size - offset < 56 ||
            memcmp(bank + offset, "CcnK", 4) ||
            memcmp(bank + offset + 8, "FxCk", 4)) return 0;
        uint32_t count = be32(bank + offset + 24);
        if (count != (uint32_t) e->parameters || count > (size - offset - 56) / 4)
            return 0;
        if (i == slot) {
            char name[29] = {0};
            memcpy(name, bank + offset + 28, 28);
            printf("factory_slot=%u factory_name=%.28s\n", slot, name);
            for (uint32_t j = 0; j < count; ++j) {
                uint32_t bits = be32(bank + offset + 56 + 4 * j);
                float v;
                memcpy(&v, &bits, 4);
                if (!isfinite(v) || v < 0 || v > 1) return 0;
                e->setParameter(e, (int32_t) j, v);
            }
            return 1;
        }
        offset += 56 + 4 * count;
    }
    return 0;
}

static void u16(FILE *f, uint16_t n) { fwrite(&n, 2, 1, f); }
static void u32(FILE *f, uint32_t n) { fwrite(&n, 4, 1, f); }

/* Snapshot the loader-unpacked image for read-only analysis, not execution. */
static int inspectImage(HMODULE module, const char *path) {
    const unsigned char *base = (const unsigned char *) module;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *) base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096)
        return 0;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *) (base + dos->e_lfanew);
    uint32_t size = nt->OptionalHeader.SizeOfImage;
    if (nt->Signature != IMAGE_NT_SIGNATURE || size == 0 || size > 16 * 1024 * 1024)
        return 0;
    unsigned char *image = calloc(1, size);
    if (!image) return 0;
    uint32_t copied = 0;
    for (uint32_t offset = 0; offset < size; offset += 4096) {
        SIZE_T count = 0;
        uint32_t wanted = size - offset < 4096 ? size - offset : 4096;
        if (ReadProcessMemory(GetCurrentProcess(), base + offset, image + offset, wanted, &count))
            copied += (uint32_t) count;
    }
    FILE *f = fopen(path, "wbx");
    if (!f) { free(image); return 0; }
    int ok = fwrite(image, 1, size, f) == size;
    if (fclose(f)) ok = 0;
    free(image);
    printf("analysis_snapshot base=%p size=%u readable_bytes=%u\n", module, size, copied);
    return ok;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    int inspect = argc == 4 && !strcmp(argv[1], "--inspect");
    if (!inspect && argc != 5) {
        fprintf(stderr, "usage: engine_probe.exe DLL BANK SLOT NEW_OUTPUT.wav\n"
                        "       engine_probe.exe --inspect DLL NEW_MAPPED_IMAGE.bin\n");
        return 2;
    }
    char *end = NULL;
    unsigned long slot = 0;
    if (!inspect) {
        slot = strtoul(argv[3], &end, 10);
        if (!*argv[3] || *end || slot > 127) return 2;
    }
    printf("loading_dll\n");
    HMODULE module = LoadLibraryExA(argv[inspect ? 2 : 1], NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) { printf("load_failed win32_error=%lu\n", GetLastError()); return 3; }
    moduleBase = (uintptr_t) module;
    printf("dll_loaded\n");
    if (inspect) {
        int ok = inspectImage(module, argv[3]);
        FreeLibrary(module);
        return ok ? 0 : 10;
    }
    typedef Effect *(*Entry)(Callback);
    Entry entry = (Entry) (void *) GetProcAddress(module, "main");
    if (!entry) { printf("no_vst_entry\n"); FreeLibrary(module); return 4; }
    printf("vst_entry_found\n");
    Effect *e = entry(host);
    if (!e) { printf("vst_entry_returned_null\n"); FreeLibrary(module); return 5; }
    if (e->magic != 0x56737450 || !e->dispatch || !e->setParameter ||
        (!e->replace && !e->process) || e->outputs != 2 || e->inputs != 0 ||
        e->parameters != 99) {
        printf("unexpected_effect_abi magic=%08x\n", (unsigned) e->magic);
        return 6;
    }
    printf("effect_created id=%08x version=%d programs=%d parameters=%d inputs=%d outputs=%d flags=%08x\n",
           (unsigned) e->id, e->version, e->programs, e->parameters, e->inputs,
           e->outputs, (unsigned) e->flags);
    e->dispatch(e, 0, 0, 0, NULL, 0);
    e->dispatch(e, 10, 0, 0, NULL, 44100);
    e->dispatch(e, 11, 0, 256, NULL, 0);
    e->dispatch(e, 2, 0, 0, NULL, 0);
    if (!loadProgram(e, argv[2], (unsigned) slot)) {
        printf("invalid_or_incompatible_factory_bank\n");
        e->dispatch(e, 1, 0, 0, NULL, 0); FreeLibrary(module); return 7;
    }
    e->dispatch(e, 12, 0, 1, NULL, 0);
    FILE *wave = fopen(argv[4], "wbx");
    if (!wave) {
        perror("output"); e->dispatch(e, 12, 0, 0, NULL, 0);
        e->dispatch(e, 1, 0, 0, NULL, 0); FreeLibrary(module); return 8;
    }
    const uint32_t total = 44100 * 3;
    const uint32_t dataBytes = total * 2 * 4;
    fwrite("RIFF", 4, 1, wave); u32(wave, 36 + dataBytes);
    fwrite("WAVEfmt ", 8, 1, wave); u32(wave, 16); u16(wave, 3); u16(wave, 2);
    u32(wave, 44100); u32(wave, 44100 * 8); u16(wave, 8); u16(wave, 32);
    fwrite("data", 4, 1, wave); u32(wave, dataBytes);
    float left[256], right[256], interleaved[512];
    float *channels[2] = {left, right};
    double peak = 0, squares = 0;
    uint32_t badSamples = 0;
    LARGE_INTEGER start, finish, frequency;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
    midi(e, 0x90, 48, 100);
    int released = 0;
    for (uint32_t position = 0; position < total;) {
        uint32_t frames = total - position < 256 ? total - position : 256;
        if (!released && position + frames > 88200) frames = 88200 - position;
        if (!frames) { midi(e, 0x80, 48, 0); released = 1; continue; }
        memset(left, 0, sizeof(left)); memset(right, 0, sizeof(right));
        clockInfo.samplePosition = position;
        clockInfo.ppqPosition = position / 44100.0 * 2;
        if (e->replace) e->replace(e, NULL, channels, (int32_t) frames);
        else e->process(e, NULL, channels, (int32_t) frames);
        for (uint32_t i = 0; i < frames; ++i) {
            interleaved[2 * i] = left[i]; interleaved[2 * i + 1] = right[i];
            for (int c = 0; c < 2; ++c) {
                double s = channels[c][i];
                if (!isfinite(s)) { ++badSamples; continue; }
                if (fabs(s) > peak) peak = fabs(s);
                squares += s * s;
            }
        }
        if (fwrite(interleaved, sizeof(float) * 2, frames, wave) != frames) {
            badSamples = UINT32_MAX; break;
        }
        position += frames;
    }
    QueryPerformanceCounter(&finish);
    int ioFailed = ferror(wave);
    if (fclose(wave)) ioFailed = 1;
    e->dispatch(e, 12, 0, 0, NULL, 0);
    e->dispatch(e, 1, 0, 0, NULL, 0);
    FreeLibrary(module);
    printf("rendered_frames=%u peak=%.9g rms=%.9g nonfinite=%u elapsed_ms=%.3f\n",
           total, peak, sqrt(squares / (total * 2)), badSamples,
           (finish.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart);
    return ioFailed || badSamples || peak <= 1e-7 ? 9 : 0;
}
