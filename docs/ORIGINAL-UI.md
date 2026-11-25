# Original A1 interface restoration — 0.2.0

> Engineering record: 0.2.0 panel. See [V1-RELEASE.md](V1-RELEASE.md) for
> the completed local release and [PUBLIC-SOURCE-IMPORT.md](PUBLIC-SOURCE-IMPORT.md)
> for the standalone source checks. Historical commit IDs refer to the original
> Audio Plugins workspace. Paths under `local/` or `dist/` describe private
> evidence and packages, not public repository downloads.

The 0.2.0 editor replaces the temporary normalized-slider grid with the original
769 × 426 A1 panel. The accepted 0.1.1 audio pipeline and 2,048-sample minimum
delay remain in place. Lower latency is deferred at the user's request.

## Recovery and implementation

All 21 original bitmap resources (128–148) were recovered from the original
Windows A1 image and converted losslessly to PNG. The Windows and recovered
Mac backgrounds have identical RGB pixels across all 327,594 pixels. The Mac
PICT sprite decoder was incomplete, so the working resource set uses the
Windows sprites, including their original 5-pixel octave handle and wheel strip.
White (#FFFFFF) is the original transparent sprite colour.

Static analysis recovered the 99-entry parameter descriptor table, its geometry
table, original control rectangles and selected-state regions. The restored
surface has 57 parameter controls, including pitch/mod wheels, Mod Wheel Cutoff
and Filter Slope. Knobs use the original 63-frame artwork. Original panel labels,
fonts, colours and logos remain in the background artwork. Live values use a
native Arial readout in the recovered rectangles.

The envelope graphs use the recovered 59-pixel time scale, sustain geometry,
linear attack and exponential decay/release. Dragging a stage edits its original
parameter and brackets the edit with host automation gestures. The original
61-key range is MIDI 36–96, using the recovered upper/lower hit rectangles and
played-key artwork. Mouse release and closing the editor release owned notes;
incoming MIDI notes remain owned by the host. The native keyboard also supports
glissando, releasing its previous note before starting the next.

The information display reconstructs all 25 original display types, including
frequency-table interpolation, nonlinear modulation/detune, envelope seconds,
tempo divisions, oscillator footage and dB values. Ctrl. means MIDI controller
number. Factory presets remain in Cubase's toolbar, matching the original
division between host toolbar and instrument face.

The original credits image opens from either logo. Click or Escape dismisses
it. Double-click and Command-click reset are Mac usability additions alongside
the observed original Control-click reset. Shift retains the original 1.5×
finer drag distance; Option selects circular movement. Legacy Mac modifier
translation and original credits dismissal were not fully recovered. The
Windows CRT's exact half-way decimal rounding was not executed or established.

Private research and provenance are in `local/ui-restoration/research/`, notably
`README.md`, `original-control-table.json`, `ui-behavior.json`,
`parameter-display-examples.json` and `windows-bitmaps/manifest.json`.
The original DLL was not patched. Recovered artwork and binaries remain outside
Git under ignored `local/` and `dist/` directories.

## Local verification

Eight CTest targets pass. The editor fixture has 46 checks, including all 57
parameter bindings and edit isolation, balanced automation gestures, original
knob movement, default reset, waveform/slope/buttons, envelope-stage dragging,
wheel return/retention, white/black/high-key input, MIDI highlights, editor-close
note release, preset refresh, saved-value preservation, credits and missing-art
recovery. All 125 independent examples across the 25 display types pass.

The console fixture services successive timer turns explicitly; it does not
assume that a single sleep advances the host's message loop. The initial RED
checks preceded the surface implementation; a second RED stage exposed the
original envelope scale, keyboard range and value readout requirements.

LLVM editor-only coverage: 98.59% lines, 94.92% regions, 100% functions and
86.09% branches. This is editor coverage, not whole-plugin coverage. The final
release is rebuilt with coverage disabled and contains no LLVM profile symbols.
The editor compiles with `-Wall -Wextra -Werror`; no standalone linter is
configured. Build, test, coverage, screenshot, hash and signature records are
under `local/ui-restoration/verification/`.

The helper, original DLL, both banks, XML and VST3 class identities match the
accepted 0.1.1 bundle. All 99 parameter IDs and the existing 404-byte state format
are retained. No new dependency, network endpoint or security permission was
introduced.

## Cubase acceptance

The final installed ARM64 build was exercised in Cubase 15.0.30.287 on macOS
26.6.2 with CrossOver 26.3, at 44.1 kHz / 256 samples. The 769 × 426 editor
renders correctly at Retina scale. Native cutoff and amplitude-attack edits
update the sprites, information display and envelope graph. Cutoff and attack
saved at normalized 0.5 both recalled at 0.5 after a full Cubase restart. The
native credits button opens the original image; Escape returns to the panel.

A final 80.349-second HollowPad playback window with the editor open covered
13,763 callback submissions, with zero missing/late frames, input/output drops
or callback deadline misses. Callback p99 upper bound was 16.384 µs;
maximum 26.125 µs; worker maximum 6495.833 µs. Cubase's monitor
showed **No audio dropouts detected**, with Real Time/Peak around 1–3% of scale.
An earlier UI candidate also passed a separate live window; that record is
kept separately from the final binary's acceptance evidence.

The final audition is
`local/ui-restoration/verification/A1 Original UI HollowPad Test.cpr`, copied
from the accepted 0.1.1 package. Its four single notes use Bank 2 / HollowPad FH.
The two temporary test edits were restored to the audition values and saved.
Cubase is left stopped, with the original A1 interface visible.

A separate Save As fixture inherited a different project state: a chorded
phrase using Bank 2 / BehindTheScenes FH. It exceeded sustained helper
throughput and accumulated missing frames. That is a **failed performance
case**, not accepted stability evidence. This UI release retains the earlier
latency/engine and does not resolve the heavier project's throughput limit.
No claim of all-preset or high-polyphony performance is made.

Records are `cubase-final-live.json`, `cubase-hollowpad-initial-live.json` and
`cubase-heavy-preset.json` under the verification folder. Native UI observations
are retained in this task's computer-use trace; `editor.png` is the automated
editor render. The original user projects, the 0.1.1 package and installed
bundle rollbacks remain preserved.

Final native binary SHA-256:
`78d786a66c71c4b094983ba605d49eeb4279262f152ea6ebdd73b253a84296b9`.
The installed bundle's 31 files exactly match the signed build. Its helper
SHA-256 remains
`4578b9e69d60a06eda7f27ea67903e45493571a27b857f7c0a264f0d6b50e839`.
Code checkpoint: `cfe4dd0`.
