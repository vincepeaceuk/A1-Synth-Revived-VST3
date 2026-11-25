# A1 0.1.1 performance acceptance — 24 November 2026

> Engineering record: 0.1.1 pipeline. See [V1-RELEASE.md](V1-RELEASE.md) for
> the completed local release and [PUBLIC-SOURCE-IMPORT.md](PUBLIC-SOURCE-IMPORT.md)
> for the standalone source checks. Historical commit IDs refer to the original
> Audio Plugins workspace. Paths under `local/` or `dist/` describe private
> evidence and packages, not public repository downloads.

This is the historical 0.1.1 scheduling/buffering record. The 0.2.1 update
retains that latency and adds faster original-engine maths translation for
BehindTheScenes chords; see [the chord stability record](CHORD-STABILITY.md).

## Reproduced problem

The installed 0.1.0 bridge drove Cubase 15's Real Time and Peak meters to their
limits during the four-note HollowPad loop at 44.1 kHz / 256 samples. Native
process samples showed Cubase audio threads inside `Bridge::transfer`/`poll`,
waiting for Windows processing. Low helper utilization while idle did not
establish acceptable scheduling during notes.

Baseline evidence is under `local/performance/baseline/`. These and later
measurements are from this Mac, Cubase 15.0.30.287, macOS 26.6.2 and CrossOver 26.3.

## Changes

- The Cubase callback submits preallocated jobs and consumes timestamped stereo
  frames. A separate worker owns all helper calls and socket waits.
- The reported delay is `max(2048, 2 * preparedMaximumBlock)` samples. At the
  tested 256-frame block, 2,048 samples equals 46.440 ms at 44.1 kHz. Live MIDI
  monitoring includes this delay plus device latency. Offline rendering uses
  the same delay and may wait on the worker.
- `IPrefetchableSupport` returns `kIsNeverPrefetchable`; accelerated background
  prefetch must not outrun the external helper. Actual Cubase queried the
  interface. Its global buffer/ASIO-Guard preferences were not changed.
- The helper rendering thread requests `THREAD_PRIORITY_HIGHEST` within the
  unchanged normal process class. The watchdog retains normal priority. The
  helper reported priority 2, success 1, process_class 32 in Cubase. Upstream
  [Wine maps this priority to interactive latency/throughput QoS](https://raw.githubusercontent.com/wine-mirror/wine/master/server/thread.c);
  precise installed CrossOver scheduling is established by observations, not
  assumed equivalent to upstream implementation.
- Worker transport has a 500 ms total transaction budget, allowing a short OS
  stall to recover without permanently stopping the instrument. The callback
  continues immediately. If audio really arrives late, missing frames are
  counted and expired output is discarded instead of shifting the music.
- Parameter readbacks and program notifications reach the host on the message
  thread. Edit generations become explicit parameter dirty bits, so an older
  queued MIDI readback cannot become a setter that undoes a newer controller.
  Explicit edits back to an older float still apply. State restoration clears
  an older deferred MIDI program change.
- One-second JSONL diagnostics record callback timing, deadline misses, queue
  overflow, late/missing frames and worker timing outside the audio callback.

An intermediate asynchronous build with normal helper priority removed the
Cubase overload but still missed audio during 35–40 ms helper responses. It is
not the accepted package. Profiling the live translated helper also triggered
its former 50 ms transport deadline; intrusive sampling was excluded from the
accepted uninterrupted playback runs. A later 1,024-sample build passed steady
playback but rapid preset changes produced one 256-sample gap with a 24.3 ms
worker response. The final reserve was increased to 2,048 samples; that earlier
run is retained as a rejected intermediate result.

## Accepted Cubase result

The final installed build played continuously from 18:18:15 to 18:27:17 UTC,
including editor/control use and repeated factory changes through Landscape,
beAngeled and HollowPad. A separate fixed 180-second window also passed.
The extended interval covers 93,410 callback submissions at 44.1 kHz /
256 samples. Cubase's Real Time and Peak bars were approximately 1–3% of scale,
and its expanded monitor displayed **No audio dropouts detected**. These are
local observations, not a promise for every session.

| Measurement | Extended live run |
|---|---:|
| Missing audio frames | 0 |
| Late/discarded frames | 0 |
| Input jobs / output frames dropped | 0 / 0 |
| Callback deadline misses | 0 |
| Callback p99 upper histogram bound | 16.384 µs |
| Callback maximum | 22.833 µs |
| Worker render maximum | 21423.958 µs |
| Reported buffering delay | 2,048 samples / 46.440 ms |

No audio-driver buffer or global ASIO-Guard preference was changed. Live
keyboard response includes the added delay; playback/export can use the
reported delay for host compensation. Cubase's post-export realtime path
was also played again and retained zero missing frames/drop counters.

Final Quick Audio Export: `local/cubase-smoke/Mixdown/A1 Performance 0.1.1 - Stable Final.wav`.
Stereo 24-bit PCM, 44,100 Hz, 278,601 frames /
6.317482993 seconds, peak -10.4952 dBFS,
RMS 0.0515444507. Each of the first four seconds has nonzero musical
output (RMS 0.04336, 0.05967, 0.06357, 0.07677).
The fixture tests establish exact buffer/MIDI offsets; the real export
checks finite, non-clipping musical output, not historical bit equivalence.

| Accepted file | SHA-256 |
|---|---|
| Native ARM64 binary | `ce9aad6d074cf51a981b7b3984651cac83046ef5fe4972fb11eef061f18d0bd7` |
| Windows helper | `4578b9e69d60a06eda7f27ea67903e45493571a27b857f7c0a264f0d6b50e839` |
| Original DLL | `073727452a7072b1a711ac639b77710577d717d57e4ebd1ba55af949212b3cc4` |
| Final Wave | `cdc3242a95a45a188857b29a4064c8e22ec6cc7237c21ea2d7bada425e802aa7` |

Strict bundle signature verification and comparison of every installed file
against the final build passed. Both proprietary banks and the XML retain
their original hashes. Evidence, test logs and hashes are retained under
`local/performance/0.1.1/` and per-instance logs in
`~/Library/Logs/A1RevivalPreview/`. The accepted 9-minute interval is
`stable-final-live-extended.json`; earlier rejected/intermediate reports are
retained separately and do not describe the packaged binary.

## Regression checks

Seven CTest targets pass: host context, banks, processor state/MIDI 25 checks,
owned-child shutdown 10 checks, render pipeline 26 checks, prefetch COM/interface
contract 28 checks, and delayed helper recovery 10 checks. Focused RED
checkpoints precede fixes for blocking callbacks, tiny offline jobs, FIFO
capacity/reset, prefetch lifetime, thread priority, delayed recovery and
controller/state races.

The pipeline fixture passes `-Wall -Wextra -Werror` and UBSan. The earlier
22-check FIFO revision measured 93.1% line / 74.5% branch coverage for that header;
that historical percentage is not whole-plugin or current-revision coverage.
ASan could not initialize its allocator on this Mac and is not counted as
verified. No standalone linter is configured.

Fixtures cover exact stereo ramp delay across varying blocks; 1-frame and 4,096-
frame offline jobs; queue wrap and overflow; reset with a full output queue;
a held worker while realtime submission returns; exact MIDI note-on/off
sample offsets; stale controller feedback; explicit same-value edits; real
helper timeout/reaping bounds; and rejection outside genuine Cubase. The Windows helper fixture passes
157 checks, including explicit MIDI feedback and parameter-mask validation.
Synthetic fixtures never load the original DLL or present a fabricated identity
to it.

## Scope

Acceptance applies to one instance and the recorded factory sounds at the
measured rate/buffer on this Mac. Extended dense polyphony, automation,
multiple instances, other rates/buffers and all factory sounds remain open.
The sound engine is still original Windows x86 code through CrossOver; this
change does not make it native ARM64 DSP or recreate the original graphics.
No original DLL instructions or proprietary factory assets were changed.
