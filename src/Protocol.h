#pragma once
#include <stdint.h>

#define A1_MAGIC 0x41315256u
#define A1_PARAMETERS 99u
#define A1_MAX_FRAMES 4096u
#define A1_MAX_MIDI 256u
#define A1_HELLO 1u
#define A1_PROCESS 2u
#define A1_QUIT 3u
#define A1_OK 0u
#define A1_ERROR 1u
#define A1_PROCESS_EXPLICIT_PARAMETERS 1u

/* Private little-endian pipe protocol. No pointers cross the boundary. */
#pragma pack(push, 1)
typedef struct { uint32_t magic, command, bytes, status; } A1Header;
typedef struct { char product[64], vendor[64]; } A1Host;
typedef struct {
    uint32_t parameters, programs, outputs, version;
    char names[A1_PARAMETERS][32];
    char labels[A1_PARAMETERS][16];
} A1Metadata;
typedef struct {
    uint32_t frames, midiCount, program, reserved;
    double sampleRate, tempo, samplePosition;
    float parameters[A1_PARAMETERS];
    /* Explicit mode: bit i marks a user edit to parameter i. Unmarked
     * snapshots may contain older engine readbacks and must not be setters.
     * Legacy mode (reserved==0) requires an empty mask. */
    uint32_t parameterChanges[4];
} A1Process;
typedef struct { uint32_t frame, size; uint8_t data[4]; } A1Midi;
#pragma pack(pop)
/* PROCESS reply: 99 actual parameter floats, then planar left/right audio. */

#ifdef __cplusplus
static_assert(sizeof(A1Header) == 16);
static_assert(sizeof(A1Host) == 128);
static_assert(sizeof(A1Metadata) == 4768);
static_assert(sizeof(A1Process) == 452);
static_assert(sizeof(A1Midi) == 12);
#else
_Static_assert(sizeof(A1Header) == 16, "header wire size");
_Static_assert(sizeof(A1Host) == 128, "host wire size");
_Static_assert(sizeof(A1Metadata) == 4768, "metadata wire size");
_Static_assert(sizeof(A1Process) == 452, "process wire size");
_Static_assert(sizeof(A1Midi) == 12, "MIDI wire size");
#endif
