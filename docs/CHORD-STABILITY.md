# BehindTheScenes chord stability — 0.2.1

> Engineering record: 0.2.1 chord stability. See [V1-RELEASE.md](V1-RELEASE.md) for
> the completed local release and [PUBLIC-SOURCE-IMPORT.md](PUBLIC-SOURCE-IMPORT.md)
> for the standalone source checks. Historical commit IDs refer to the original
> Audio Plugins workspace. Paths under `local/` or `dist/` describe private
> evidence and packages, not public repository downloads.

**Installed and accepted on this Mac on 24 November 2026.** Continuous chord
playback ran for 10 minutes 55 seconds with no bridge drops; the user confirmed
“Sounds clean and stable.” The interval included BehindTheScenes and subsequent
user preset auditioning. It is not a frozen-preset endurance measurement for
the entire interval. Cubase also reported “No audio dropouts detected.”

The release retains the original editor, all stored parameters, the original
Windows A1 DLL, all voices, and the existing 2,048-sample buffering at a
256-sample host block. Latency reduction is a later task.

## Observed failure

Bank 2 / BehindTheScenes FH chords at 44.1 kHz / 256 samples exceeded the
original engine's sustained throughput. Private phase measurements showed
original DSP reaching about 12–13 ms per block against a 5.8 ms audio interval.
The worker queue filled, audio arrived late, and repeated MIDI safety resets
added further work. The failure reproduced with the editor closed. Changing
x87 precision alone did not resolve it.

## Engine change

The original Win32 engine runs through a pinned build of
[x87sidecar](https://github.com/athei/x87sidecar), using cooperative attachment
only. The helper shares its own task/thread ports; no debugger authorization
or system Rosetta change is used. A private copy of the installed prepared
Wine loader keeps the actual synth in the directly owned process. CrossOver
remains required; its installed loader and ntdll hashes must match this build.

The helper uses the normal Windows x86 53-bit significand setting, preserving
rounding, exception masks and MXCSR. The translator uses binary64 arithmetic.
FMA contraction and the unrelated opcode-decoder hook are disabled. Original
DLL instructions and assets are unchanged.

Startup requires a successful hook before loading A1. Binary protocol stdout
stays separate from diagnostics. Per-instance sockets/services are isolated;
closing during bootstrap cancels the owned target and sidecar. A source patch
adds that cancellation, validates the cooperative target, and supports the
installed compiler's atomics. It does not change the translator's DSP maths.

## Cubase acceptance

Tested in Cubase 15.0.30.287, macOS 26.6.2, CrossOver 26.3, on a Mac14,9 with
10 CPU cores and 32 GiB memory. The project used 44.1 kHz, a 256-sample buffer,
120 BPM and a two-bar chord loop. BehindTheScenes began with 16 voices and
quad chorus enabled. The original panel and credits were exercised during
playback. No profiler ran during the accepted interval.

| Measurement | Observed result |
|---|---|
| Fixed window, UTC | 20:39:11–20:50:06 on 24 November 2026 |
| Wall time / rendered audio | 655.599 s / 655.273 s |
| Audio callback count in window | 112,881 |
| Missing / late / output-dropped frames | 0 / 0 / 0 |
| Input drops / callback deadline misses / unexpected blocks | 0 / 0 / 0 |
| Callback p99 histogram upper bound / maximum | 16.384 µs / 36.167 µs |
| Worker render maximum | 11,997.625 µs |
| Reported bridge latency | 2,048 samples, 46.4 ms at 44.1 kHz |

The worker's coarse p99 histogram upper bound was 8,388.608 µs; that is not a
precise percentile below the 5.8 ms host interval. Buffering absorbs occasional
long renders. The accepted failure counters above remained zero since instance
startup. Host track gain was adjusted during auditioning; the synth was not
simplified to obtain this result.

The saved project `local/performance/chords/A1 BehindTheScenes 0.2.1.cpr` was
reopened in Cubase. A new accelerated helper verified the genuine Cubase host
and received program 10, the saved amplifier value, 16 voices and hidden MIDI
volume 1. Playback and an offline Wave export completed with all failure
counters still zero. After Stop, the output meters returned to silence.
The user's separate preset audition was saved and restored as
`User audition preserved after chord test.cpr`; the original chord fixture
was not overwritten.

| Export property | Observed value |
|---|---|
| File | `local/performance/chords/Mixdown/A1 BehindTheScenes 0.2.1.wav` |
| Encoding | stereo, 32-bit float Wave, 44,100 Hz |
| Length | 176,400 frames / 4.0 seconds |
| Finite / nonzero samples | all finite / 352,800 |
| Peak / RMS | 0.9403790832 / 0.1061663266 |
| SHA-256 | `800fb110cc86e0c0c48d325214a5adb6d2d141b6fb9436d9704a16585f6c5a47` |

Each second contains nonzero musical output. This verifies the current render
and recall path, not an independent historical audio comparison.

## Regression and numerical checks

- Ten CTest targets passed, including seven accelerated startup/transport/
  teardown cases and two source-pin rejection checks. The existing
  editor/state/MIDI/render tests pass.
- The strict Windows helper fixture passed 175 checks.
- Fifteen upstream numerical fixtures passed under stock Rosetta and under
  the cooperative translator (30 runs). These are bounded fixture checks,
  not proof of complete x87 equivalence or original A1 audio equivalence.
- Five unprofiled Windows maths-loop pairs retained exactly the same fixture
  value. Median compute time was 91.900 ms stock versus 29.059 ms accelerated;
  every accelerated ready PID matched the actual target PID. This synthetic
  3.16× result is not a whole-instrument speed claim.
- The bundle and its four native runtime components pass strict signature
  verification. Original A1 DLL SHA256 remains
  `073727452a7072b1a711ac639b77710577d717d57e4ebd1ba55af949212b3cc4`.

The numerical fixtures expose limits despite passing: transcendental values
can differ by 1–2 ULP, signed zero/status bits can differ, and the upstream
FTZ/DAZ signal-storm fixture reports occasional subnormal flushing. Passing
these tests does not establish bit-identical historical sound. Broader preset,
automation, multiple-instrument and rate/buffer acceptance remains open.

Private recordings, benchmark inputs, failed launch variants and detailed
logs are retained under `local/performance/chords/`, including
`accepted-endurance-0.2.1.json`, its engine/performance logs,
`export-0.2.1.json`, `recall-0.2.1.json` and the user's listening confirmation.

## Installed build

The installed VST3 and packaged copy match the tested Release build. All 36
bundle files are recorded in `installed-0.2.1-candidate.json`.

| Component | SHA-256 |
|---|---|
| Native ARM64 frontend | `e0e9ea28da532addbf97c56fcef5db61a3b873c7c70a8ebc380d83675e25ebd5` |
| Win32 helper | `0668f214e26710703f3d4b93d95645835a1007f2198e2d5b1860d641acda1afb` |
| Original A1 DLL | `073727452a7072b1a711ac639b77710577d717d57e4ebd1ba55af949212b3cc4` |

The stock 0.2.0 bundle is preserved under `local/backups/`, and its existing
zip is retained beside the 0.2.1 local package. This package uses the existing
dedicated bottle on this Mac; it does not provision another computer.

## Rebuild

The ordinary CMake build prepares the pinned source and applies the reviewed
`patches/x87sidecar-a1.patch`. It refuses source changes outside that exact
patch, including staged edits and untracked source. The translator executable
and its MIT licence are embedded. Proprietary engines and the local Wine copy
remain ignored build/distribution assets.

Use macOS 14 or later, the existing CrossOver installation and the existing
dedicated A1 bottle. After a CrossOver or Rosetta update, rebuild and rerun the
startup and Cubase acceptance checks before relying on the rebuilt preview.
