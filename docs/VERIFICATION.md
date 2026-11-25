# A1 Revival Preview verification — 24 November 2026

> Engineering record: verification chronology. See [V1-RELEASE.md](V1-RELEASE.md) for
> the completed local release and [PUBLIC-SOURCE-IMPORT.md](PUBLIC-SOURCE-IMPORT.md)
> for the standalone source checks. Historical commit IDs refer to the original
> Audio Plugins workspace. Paths under `local/` or `dist/` describe private
> evidence and packages, not public repository downloads.

## Initial sound and recall result (0.1.0)

**Verified:** original A1 sound generation inside real Cubase 15 on this Apple
Silicon Mac (macOS 26.6.2), through the installed ARM64 VST3 frontend and
original Windows x86 DSP helper in CrossOver 26.3. Cubase version is
15.0.30.287. A temporary native editor is supplied.

The acceptance project is `local/cubase-smoke/A1 Cubase Bridge Test.cpr`
(paths here are relative to `a1/`). It has a four-note MIDI
phrase routed to Bank 2 / HollowPad FH, 44.1 kHz, 256-frame realtime blocks,
120 BPM and Stereo Out. The engine reports 99 parameters, 128 original
programs and flags `0x119`, selecting the implemented replacing renderer.

Cubase Quick Audio Export produced:

| Property | Observed value |
|---|---|
| File | `local/cubase-smoke/Mixdown/A1 Original Engine - Final Preview.wav` |
| Encoding | stereo, 24-bit PCM, 44,100 Hz |
| Length | 278,601 frames / 6.317482993 seconds |
| Peak | 0.3097474575 / -10.1798 dBFS |
| RMS | 0.0526726500 |
| Nonzero samples | 555,598 |
| SHA-256 | `18adcd68ae8206664291c741aa2539e6c61609bc78f4f54fa21784071f5e8cb0` |

PCM was decoded and checked; output is finite and nonzero. The project was
saved, closed, reopened and played again. A new helper instance received the
saved program 6 and hidden MIDI volume 1, and returned nonzero audio. This
demonstrates recall and sound generation, not bit-exact comparison with an
independent historical installation.

The final installed build was loaded after restarting Cubase and the Wave
export was repeated after the final regression fixes. Each of the first four
seconds contains nonzero musical output (RMS 0.0494, 0.0569, 0.0687, 0.0748).

Evidence: `local/evidence/final-audio-verification.json` and per-instance
`~/Library/Logs/A1RevivalPreview/engine-*.log`. Original DLL integrity is checked
against `073727452a7072b1a711ac639b77710577d717d57e4ebd1ba55af949212b3cc4`.

## Initial regression checks (0.1.0)

All four CTest targets passed: host context, original-bank parsing, processor
state/MIDI (20 checks), and bounded child shutdown (10 checks). The Windows
helper fixture passed 113 checks. Native bundle signature verification and
original DLL/XML/both-bank hash checks passed; the installed Mach-O is ARM64.

Focused failing checks were recorded before implementation fixes, then rerun
successfully:

- Genuine Cubase host interface and process identity are required. Generic
  test hosts cannot start the original engine.
- Legacy banks accept the original zero `byteSize` fields while rejecting
  truncation, wrong IDs, wrong parameter counts, NaN, out-of-range values
  and trailing data.
- Preset changes and state restoration notify the host; parameter values
  and intentional saved MIDI-volume mutes restore exactly.
- Factory MIDI volume initializes to unity. The original bank's stored zero
  is retained in the raw asset and normalized only in the factory catalog.
- Versioned parameter snapshots reject stale engine readback, including a
  preset reset to the same value and an intervening edit cycle back to the
  same float value.
- A full 256-event packet preserves its last note-off and sample offset.
  Oversized bursts explicitly reset sustain and notes instead of silently
  losing release messages. Skipped blocks schedule the same recovery before
  the next successful packet.
- Helper fixtures check binary message bounds, MIDI validation, memory ranges,
  legacy identity query validation, parameter caching, actual MIDI readback,
  processing capability flags and finite audio.
- A fixed maximum engine block capacity preserves held notes when actual render
  blocks shrink; a real sample-rate change still suspends and reconfigures.
- A TERM-ignoring owned child is killed and reaped within the bounded teardown
  deadline; an unrelated child remains alive. No blocking child wait is used
  in the plugin destructor.

The original generic-host probe remains rejected. Its exit 12 is expected
evidence of that restriction and is not counted as successful audio rendering.

## Local safety and scope

Original vendor archives and payloads remain private under ignored `local/`.
No original installer or licensing helper was launched. The running preview
uses the original hashed DLL; the derived PE is only a disassembly reference.
The helper uses a dedicated bottle, per-instance process connection and logs;
no other CrossOver bottle is stopped by normal preview operation.

Unrelated user projects were preserved separately during the local tests.

## Current interface update (0.2.0)

The original A1 artwork and control surface are restored. Native Cubase checks
passed for rendering, knob/envelope updates, credits and saved-value recall
after a full restart. Eight CTest targets pass; the editor fixture has 46 checks
and 125 recovered display examples. The final HollowPad playback window had no
missing frames or Cubase dropouts. The chorded BehindTheScenes fixture remains
a failed sustained-throughput case. See [the UI acceptance record](ORIGINAL-UI.md)
for that release's exact binary, measurements, test scope and remaining limits.

## Chord rendering update (0.2.1)

The original Win32 engine now uses a pinned cooperative x87 translator and a
private prepared Wine loader. The actual engine retains the directly owned
process identity; acceleration is required before the original DLL loads.
The Cubase host guard, original panel, factory assets, stored parameters and
2,048-sample latency are retained. Ten CTest targets and 175 Windows helper
fixture checks pass. Startup tests cover byte-exact duplex sockets, independent
instances, unavailable acceleration, early-close teardown and source pinning.

The installed release passed a 10-minute-55-second continuous chord window at
44.1 kHz / 256 samples, including BehindTheScenes and user preset auditioning.
All missing/late frames, queue drops and deadline-miss counters remained zero;
the user confirmed clean, stable sound. Cubase reported no audio dropouts.
Save/reopen, note release and a finite, nonzero four-second stereo float Wave
export also passed. Exact measurements, binary hashes and the numerical
scope are recorded in [CHORD-STABILITY.md](CHORD-STABILITY.md).

## Performance update (0.1.1)

The initial synchronous bridge reproduced the user's Cubase real-time overload.
Version 0.1.1 moves helper communication off the audio callback, reports a
fixed buffering delay, disables ahead-of-time prefetch for this external
engine, and raises only the helper rendering thread's scheduling priority.
The original DLL and both factory banks remain unchanged.

See [the performance acceptance record](PERFORMANCE.md) for current measurements,
regression checks, installed binary hashes and the revised export. The original
Wave and initial test counts above remain historical 0.1.0 evidence.

## Version 1.0 update

The final instrument is A1 Revival 1.0.0, with its existing bundle identifier
and both VST3 class IDs retained. The original panel, DLL, 256 factory slots and
99 stored parameters remain present. A disposable startup warm-up allows the
normal bridge delay to fall from 2,048 to 768 samples: 17.4 ms at 44.1 kHz, a
62.5% reduction. Sixteen voices and quad chorus remain enabled in the dense
BehindTheScenes chord fixture.

The warmed candidate passed 674 seconds of continuous two-instance playback
with recorded filter automation and zero missing/late frames, queue drops or
callback deadline misses. Additional two-instance smoke windows passed at
44.1 kHz / 128, 48 kHz / 256 and 48 kHz / 512. These shorter matrix checks are
not endurance qualifications. The final binary separately passes original-panel
parameter edits, MIDI program changes/controllers, edited preset recall after a
full Cubase restart and actual Wave export. See [V1-RELEASE.md](V1-RELEASE.md)
for the precise build associated with each acceptance window and its evidence.

All thirteen CTest targets pass after the final recall fix (16.13 seconds).
The actual VST3 wrapper fixture selects all 256 slots, checks all 99 parameters,
and verifies that host program echoes preserve restored edits. The dedicated
helper contracts cover 267 checks; the editor retains its 46 checks and 125
display examples. Strict bundle/runtime signatures and unchanged payload hashes
pass. There is no whole-plugin coverage percentage or separate native linter.

## Limits

The renderer is original Windows x86 A1 code, not native ARM64 DSP. CrossOver
and the prepared local bottle remain required. No AU was built. Exhaustive
preset/control equivalence, broader automation stress, arbitrary instance counts,
scheduler pressure and unlisted rate/buffer combinations remain unverified.
The current audio callback consumes buffered results without waiting
for IPC; the worker has a bounded transport deadline. A working local preview is
not a portable installer for another Mac. The version 1 local qualification is
limited to the stated hardware, prepared runtime and test windows. Focused pipeline coverage is recorded in
PERFORMANCE.md and editor coverage in ORIGINAL-UI.md; no whole-plugin coverage percentage or separate native linter
is configured; C/C++ compiler checks, focused fixtures, asset
validation, signature verification and real Cubase acceptance provide the
recorded checks.
