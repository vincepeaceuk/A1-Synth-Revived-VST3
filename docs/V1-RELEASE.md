# A1 1.0 acceptance

> Engineering record: 1.0 release acceptance. See [V1-RELEASE.md](V1-RELEASE.md) for
> the completed local release and [PUBLIC-SOURCE-IMPORT.md](PUBLIC-SOURCE-IMPORT.md)
> for the standalone source checks. Historical commit IDs refer to the original
> Audio Plugins workspace. Paths under `local/` or `dist/` describe private
> evidence and packages, not public repository downloads.

Target: a finished local Apple Silicon VST3 for Cubase on this Mac. The user
asked to continue through version 1 on 24 November 2026. The accepted 0.2.1
bundle and package are the rollback baseline. CrossOver and the prepared
dedicated bottle remain required; another host or a native DSP rewrite is
outside this release.

## Required behaviour

- Reduce bridge latency at the normal 44.1/48 kHz, 256-frame buffer while
  keeping the audio callback nonblocking and BehindTheScenes chords stable.
  Start by testing 512 frames; allow up to 768 if measured jitter requires it.
- Preserve the original panel, 99 parameters, both banks, voices and effects.
  Existing projects must recall their selected program and exact stored values.
- Exercise representative bass, pad, lead, percussive and effect presets from
  both banks, note release/sustain, MIDI controllers, parameter edits, transport
  stop/start/seek, offline export and save/reopen in genuine Cubase.
- Qualify two simultaneous instruments and the supported rate/buffer matrix.
  Keep unsupported combinations explicit if their sustained throughput fails.
- Keep instance startup/teardown isolated and bounded, fail clearly on missing
  runtime, and retain runtime source pinning, host verification and signatures.
- Install the final build, repeat the important acceptance paths, verify the
  package after extraction, and supply concise use/installation notes.

## Progress and evidence

| Gate | Status | Evidence |
|---|---|---|
| 0.2.1 baseline | Accepted | CHORD-STABILITY.md |
| Latency regression checks | Passed | ProcessorTests.cpp, RenderPipelineTests.cpp and real Cubase measurements |
| Lower-latency chord endurance | Passed candidate | 674-second two-instance window, all failure counters zero |
| Presets, controls, sustain | Passed | Representative sounds from both banks; MIDI audition and verified nonzero Wave exports |
| Multiple instances and rate/buffer matrix | Passed stated scope | Two instruments; table below and matrix JSON files |
| Exact state recall | Passed final binary | Edited Bank 1 resonance 0.314159 survived a full Cubase restart and playback; recall-1.0.json |
| Regression suite and signatures | Passed | Thirteen CTest targets, 16.13 seconds; regression-recall-fix.txt |
| Final two-instrument export | Passed final binary | chords-export-final-1.0.json; stereo 24-bit PCM, 6.114 s, peak 0.573, nonzero finite audio |
| Installed 1.0 and extracted package | Passed | 36 installed/built/extracted bundle files match; 67 package files, CRC, hashes, modes and strict signatures verified |

Historical bit-identical audio remains unverified. Numerical fixture limits
from 0.2.1 remain applicable. This release qualifies the stated local workflow.

## Tested workflow

Genuine Cubase Pro 15.0.30.287 on this Apple Silicon Mac, macOS 26.6.2, with
the existing CrossOver 26.3 installation and dedicated prepared bottle. The
original DLL, both banks, XML and all 21 panel bitmaps remain unchanged. The
VST3 class IDs and bundle identifier retain existing project compatibility.
Projects may retain their earlier "A1 Revival Preview" instance labels.

| Rate / host buffer | Extra bridge delay | Observed continuous window | Result |
|---|---|---|---|
| 44.1 kHz / 256 | 768 frames / 17.4 ms | 674.18 s, two dense chord instruments | Zero failure counters |
| 44.1 kHz / 128 | 768 frames / 17.4 ms | 41.97 s, two instruments | Zero failure counters |
| 48 kHz / 256 | 768 frames / 16 ms | 94.16 s, two instruments | Zero failure counters |
| 48 kHz / 512 | 1,024 frames / 21.33 ms | 56.96 s, two instruments | Zero failure counters |

The 674-second qualification used the warmed 0.3.0 candidate, with sixteen
voices, quad chorus, live edits and recorded filter automation. The three
additional combinations used the first 1.0 build before the program-queue fixes.
Those fixes change program selection/state handling; the qualified normal render,
warm-up and buffering paths are unchanged. The exact final binary is installed
from commit `7d3da5a` and separately checked in Cubase for program changes,
edited-state recall and export. These windows are measured playback, not idle
callback counts. The main configuration was restored to 44.1 kHz / 256 samples.

Failure counters checked are missing/late frames, input/output queue drops,
callback deadline misses and unexpected block sizes. The candidate's lifetime
maximum callback times were 22.75 and 37.583 microseconds; render maxima were
5.62 and 7.02 ms. All counters remained zero in the stated successful windows.

Bank 2's recorded MIDI audition selects BassIndeed FH, HollowPad FH, Plucky FH,
ClassicSoloing FH, NoisePerc FH, Lightcycles T and BehindTheScenes FH. It includes
six-note chords, sustain and release, modulation, pitch bend and centring, MIDI
volume and all-notes-off. Bank 1's live chord audition selects FM Bass WMF,
Warm Pad WMF, Sync Lead JH, Xylopor WMF and Forbidden Planet WMF. The original
panel visibly changes its actual parameter values. The Cubase preset toolbar
can retain an earlier label after MIDI program changes; parameters and audio
were checked instead of relying on that label alone.

The actual-wrapper fixture selects all 256 slots and compares all 99 parameters;
it also restores custom state and exercises duplicate program echoes, invalid
values, normal parameter automation and nonblocking deferred selection. This
is exact state coverage, not exhaustive listening comparison of every sound.

## Final installed build and audio

The final native executable SHA256 is
`e36e93ecd968a74c9b9ba3d6048ccd6c98f9ffd35a837fe3eab8654666f3181b`.
The unchanged original DLL SHA256 is
`073727452a7072b1a711ac639b77710577d717d57e4ebd1ba55af949212b3cc4`.
`installed-1.0.0.json` records every installed bundle file. The final two A1
instances, their host filter automation, stop/start/seek and original-panel
readback passed at 44.1 kHz / 256 samples. Cubase's Audio Performance Monitor
displayed **No audio dropouts detected**. The main project was saved, closed
without a save prompt and reopened with sixteen voices and quad chorus intact.
Final per-instance failure counters remained zero; idle callback counts are
not presented as a new endurance duration.

Final exports from commit `7d3da5a` are stereo 44.1 kHz, 24-bit PCM:

| Export | Frames / duration | Peak / RMS | SHA256 |
|---|---|---|---|
| Final Chords and Automation | 269,641 / 6.1143 s | 0.57305 / 0.04606 | `b41cc3e1d672dfedb0646431a93f8bac3794f66209bf27ec9f9c58cd0f58e9f5` |
| Final MIDI | 1,284,681 / 29.1311 s | 0.30765 / 0.02231 | `68aaa20c357bccb73a9b5b58f29af2efa369d32e1f96fa221df1694802ab539b` |

The MIDI export has nonzero audio in every tested program segment and its
release tail. The chord export selected both instrument parts with automation
read enabled. Neither export clips. The separate Bank 1 effect export was
captured before the recall-echo fix and is labelled accordingly in its JSON;
the edited Bank 1 sound was subsequently verified after a full restart of the
final binary. Quick Audio Export was verified from the actual files; an earlier
floating-window export attempt that created no file is not acceptance evidence.

## Local release package

`dist/A1 Revival 1.0.0 macOS.zip` contains the installed bundle, concise use and
restore notes, three MIDI-only Cubase audition projects, captured Wave/MIDI
files, notices, the acceptance records and a SHA256 file manifest. A fresh
extraction matches the staging directory byte for byte and preserves all file
modes. All 36 extracted bundle files also match the installed final build.
The four release package contracts pass against the extracted bundle, including
ARM64/macOS declarations, stable identities, unchanged original assets and
strict bundle/runtime signatures. There are 67 regular files in the archive.
`local/release-1.0/package-verification-1.0.json` and the ZIP's adjacent SHA256
file record the final archive digest and extraction checks.

## TDD evidence

The user journeys above define the release guarantees. Record each new test's
executed RED and GREEN checkpoints here as implementation proceeds; retain
separate checkpoints until release verification is complete.

- `cb6d452` is the executed latency RED: the existing 2,048-frame floor fails
  five focused lower-latency assertions. `18c01b6` passes those assertions and
  exact sample timing at 512/768 frames with variable blocks and FIFO wraparound.
- Real Cubase rejected both un-warmed 512- and 768-frame candidates: each lost
  512 frames while first-note translation took 17.4–25.4 ms. Later observed
  playback did not grow those counters. These are retained failed experiments.
- `5bc1096` and `ae6def7` are executed compile-time RED checkpoints for missing
  startup warm-up and fresh-instance preparation. The subsequent fix passes
  the strict Windows fixture and all eleven CTest targets. The fixtures check
  bounded program/voice/controller rendering, invalid/nonfinite output,
  temporary-effect disposal, fresh parameters/clock/audio and preserved module
  and host context. No original DLL is loaded by these unit fixtures.
- The editor's lifecycle assertion now checks preservation of its processor's
  configured delay, so it does not force an obsolete 2,048-frame policy.
- `5fc86b1` adds passing characterizations for exact 99-parameter state recall
  across all 256 factory slots, atomic malformed-state rejection and MIDI
  block boundaries. Its release-package RED fails on the Preview display name
  and missing minimum macOS declaration. The production fix retains both
  existing VST3 class IDs, declares macOS 14 consistently, and names the final
  instrument A1 Revival.
- The final Release build passes all twelve CTest targets, including all four
  package contracts. The installed 1.0.0 bundle matches all 36 built files.
  The 768-frame floor is the measured GREEN policy; the failed 512-frame
  experiments remain in the evidence directory.
- `c44adb3` reproduces a Cubase program-change failure in the actual JUCE VST3
  processor queue: the wrapper updated its internal integer without selecting
  the instrument's sound. The A1-only generated wrapper adapts that one queue
  branch, with the upstream source hash pinned and unchanged-source validation.
  Off-message-thread program selections are deferred atomically; the audio
  callback never takes the editor's state lock. All 256 factory slots and 99
  values, ordinary parameter automation, invalid values and deferred selection
  pass through this wrapper fixture. All thirteen CTest targets pass in 9.78 s.
- The final Cubase reopen check caught a same-program host echo resetting an
  edited Bank 1 resonance to its factory value. The saved CPR contains the
  intended edited float. A new actual-wrapper RED restores an edited component
  state, sends the matching program from a worker thread, and reproduces the
  loss of all custom values; the other seven wrapper checks continue to pass.
- The GREEN follows JUCE's controller semantics: an unchanged program number
  updates the host parameter cache without selecting the factory sound again.
  Program readback includes the latest deferred request, so an editor holding
  the state lock cannot cause an older queued choice to win. Worker-thread and
  message-thread echoes preserve all 99 edited values; deferred 9-to-6 requests
  finish on 6. All thirteen targets pass in 16.13 seconds after rebuilding.

## Startup warm-up candidate

Before acknowledging the engine as ready, one disposable original effect runs
all 128 programs at 44.1/48 kHz with sixteen voices and quad chorus. Every sample
is discarded. The helper suspends and closes only that effect, retains the
loaded DLL/translator, then validates and opens a fresh real effect. Warm-up
failure aborts initialization. The existing startup watchdog still applies.

A separate static review of this exact DLL found the same-module lifecycle
supported: effect destruction frees instance DSP allocations without unloading
module code or its shared tables. The warmed candidate subsequently passed
genuine Cubase playback. Original random streams advance during reconstruction;
the new instance restores musical state, not the untouched first instance's
random phases/noise. The original editor destructor also abandons bitmap
pointers, so exactly one disposable effect is used per helper, with no original
editor opened. Its bounded extra allocation ends with the helper process.

Static evidence and failed candidates remain private under
`local/release-1.0/`. The warmed 512-frame candidate still accumulated 2,304
missing/late frames despite a clean listening response, and was rejected.
The warmed 768-frame candidate passed 674 seconds of continuous two-instance
chord playback at 44.1 kHz / 256 samples, with all missing/late frames, queue
drops and deadline misses at zero. The extra bridge delay falls from the
accepted 0.2.1 release's 46.4 ms to 17.4 ms, a 62.5% reduction. Live monitoring
still includes this delay plus the interface latency. Each instance takes
approximately 5–6 seconds to warm up.

## Scope and recovery

This is a local compatibility release for the prepared Mac, not a portable
installer for other Macs. CrossOver and the prepared dedicated bottle remain
required. The ZIP does not include or provision the bottle. The frontend is
native ARM64; the DSP is the unchanged Windows x86 binary. No AU is supplied.
Unlisted rate/buffer combinations, 96 kHz, 64-sample hardware buffers, arbitrary
instance counts and heavy competing workloads remain unqualified.

The accepted 0.2.1 ZIP and bundle remain available for rollback. Earlier failed
candidates, detailed logs, source pin checks, static lifecycle review and the
RED/GREEN evidence remain under ignored `local/release-1.0/`. No original
installer or licensing helper was launched, and host verification remains
enabled. Translation numerical limits and warm-up random-phase differences
remain as stated in README.md and CHORD-STABILITY.md.
