# A1 Synth Revived

**A1 Revival 1.0.0 · Apple Silicon macOS · Cubase VST3 instrument**

A restoration of Steinberg's discontinued A1 Analog Synth Unit for a modern
Apple Silicon Mac. It preserves the original Windows sound engine, classic
panel, both factory banks, voices and effects behind a native macOS VST3 front
end.

**This repository contains the restoration source, tests and build tools.**
It does not contain an installable plug-in download, the original A1 engine,
factory banks, panel artwork or a CrossOver bottle. Building requires your own
compatible original files and a prepared CrossOver environment. GitHub's
**Download ZIP** downloads source, not a ready-to-install instrument.

## What has been restored

- A native ARM64 VST3 front end for genuine Cubase, with one isolated original
  Windows x86 sound engine per instrument instance through CrossOver.
- The original 769 × 426 panel, recovered knob and switch graphics, bitmap
  labels, envelope controls, amplifier sliders and five-octave keyboard.
- Both recovered factory banks: **256 program slots**, including category
  headers, with **99 stored parameters** retained in project state.
- MIDI input, sustain, modulation, pitch bend, volume, note recovery, parameter
  automation, program selection and independent instance state.
- Nonblocking audio callbacks, bounded helper communication and teardown,
  pinned translation-runtime sources and original-payload integrity checks.
- A discarded startup warm-up that reduces the normal bridge delay from
  2,048 to **768 samples**, while retaining sixteen voices and quad chorus in
  the dense BehindTheScenes chord test.
- Regression coverage for Cubase program changes and exact edited-state recall,
  including the host repeating the current program number after loading a project.

The macOS interface is native ARM64; the DSP remains the original Windows
binary running through CrossOver and its translation environment. This is an
independent compatibility restoration, not an ARM64 recompilation of A1's DSP
or an official Steinberg release. The original DLL is not patched. Actual
Cubase process/interface verification and the original host restriction remain
in place; generic test hosts cannot start the original engine.

## Requirements and compatibility

| Item | Requirement / verified scope |
| --- | --- |
| Computer | Apple Silicon Mac; qualified locally on an M2 Pro Mac |
| macOS | Build minimum 14.0; local musical acceptance on macOS 26.6.2 |
| DAW | Genuine Cubase; local acceptance used Cubase Pro 15.0.30.287 |
| Runtime | CrossOver 26.3 at its standard `/Applications/CrossOver.app` location, Rosetta 2 and the prepared dedicated A1 bottle |
| Original inputs | Matching Windows `a1.dll`, XML, Bank 2, original Mac Bank 1 and 21 recovered original UI PNGs |
| Build tools | Xcode or Command Line Tools, CMake 3.22+, Ninja, Git, Python 3.11+ and Zig 0.15.2 |
| Format | macOS VST3 instrument only; no AU, AAX, Intel Mac or Windows release is supplied |
| Signing | Local ad hoc signatures; no Developer ID signing or notarisation |

The bridge checks the installed CrossOver runtime against the build's recorded
hashes. Rebuild after updating CrossOver and repeat startup and musical checks
after a runtime or macOS update. Other hosts, Macs and operating-system versions
are not covered by the local acceptance record.

## Build from source

1. Clone this repository and fetch the pinned JUCE source:

   ```sh
   git clone https://github.com/vincepeaceuk/A1-Synth-Revived-VST3.git
   cd A1-Synth-Revived-VST3
   python3 tools/bootstrap.py
   ```

   JUCE is pinned to **9.0.5**, commit
   `d60b7a78030661574a615b586d9325ba7a7fa7dd`. The bootstrap refuses an
   unexpected existing revision. The A1 VST3 adaptation also checks the exact
   upstream wrapper source hash before generating its reviewed copy.

2. Supply your original A1 files, the matching UI artwork and Zig toolchain
   under the ignored `local/` directory. Follow the layout, hashes and commands
   in [BUILD-INPUTS.md](docs/BUILD-INPUTS.md). For the original assets:

   ```sh
   shasum -a 256 -c docs/ORIGINAL-ASSETS.sha256
   ```

   Original files are recovered without running obsolete installer scripts.
   This repository does not download or redistribute those commercial inputs.
   The [investigation record](docs/INVESTIGATION.md) documents their provenance.

3. With CrossOver installed, configure and build:

   ```sh
   cmake -S . -B build -G Ninja \
     -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_OSX_ARCHITECTURES=arm64
   cmake --build build --parallel 4
   ctest --test-dir build --output-on-failure --parallel 1
   ```

   The first build fetches the pinned MIT-licensed x87 translation source and
   applies this repository's reviewed patch. It validates the revision and
   patch on subsequent builds. A changed or unexpected checkout is rejected.
   The build prepares and signs private runtime copies without changing the
   installed CrossOver files.

   Output:

   ```text
   build/A1Revival_artefacts/Release/VST3/A1 Revival.vst3
   ```

4. Before running the instrument or runtime fixtures, ensure the dedicated
   compatibility environment is prepared at:

   ```text
   ~/Library/Application Support/A1RevivalPreview/bottles/A1RevivalPreview/
   ```

   This must be the dedicated CrossOver Windows 10 64-bit A1 environment,
   including its `cxbottle.conf`. Building the source does not provision this
   environment. The qualified setup already had this bottle; first-time setup
   on another Mac remains outside the verified workflow. See
   [BUILD-INPUTS.md](docs/BUILD-INPUTS.md#compatibility-environment).

## Install and use

Quit Cubase before replacing an installed instrument. Preserve any earlier
bundle outside the DAW's scanned VST3 directories, then copy the built
**A1 Revival.vst3** into:

```text
~/Library/Audio/Plug-Ins/VST3/
```

Keep one installed copy with this identity. Restart Cubase, add an instrument
track and choose **A1 Revival**. Existing A1 Revival projects retain the same
bundle identifier and VST3 class IDs; older instance labels may still say
**A1 Revival Preview**. Original historical A1 VST2 instances do not
automatically become this VST3.

Choose a factory sound using Cubase's program selector above the panel. Hover
or edit a control to see its original value and MIDI CC in the readout. Drag
knobs horizontally or vertically; Shift gives finer movement. Control-click,
Command-click or double-click resets a control; Option-drag uses circular
movement. The logos open the original credits.

Save your Cubase project to retain edited sounds. Factory selection initializes
the hidden MIDI volume to unity; live controller changes and deliberately muted
saved states preserve their actual values. Cubase's program label can lag after
MIDI program changes even though the panel values and sound change correctly.

## Performance and validation

Use **44.1 or 48 kHz with a 256-sample host buffer** for the main qualified
workflow. The bridge reports 768 samples of additional latency: **17.4 ms at
44.1 kHz** or **16 ms at 48 kHz**, plus the audio interface's latency. This is
a **62.5% reduction** from version 0.2.1. Cubase compensates playback and export;
live keyboard monitoring retains this delay. Larger host blocks increase the
bridge delay to at least twice the prepared maximum block size.

Allow about **5–6 seconds per instance** when opening a project. The temporary
warm-up audio is discarded before a fresh musical effect is opened.

Local version 1 acceptance on **24 November 2026** covered:

- A warmed candidate's **674-second two-instrument BehindTheScenes chord and
  filter-automation window**, with zero missing/late frames, queue drops or
  callback deadline misses.
- Shorter two-instance windows at 44.1 kHz / 128, 48 kHz / 256 and 48 kHz / 512.
- The final binary's MIDI program/controller audition, both banks, parameter
  edits, edited-state recall after a full Cubase restart, stop/start/seek and
  nonzero, unclipped stereo Wave exports. Cubase reported no audio dropouts.
- **13 passing CTest targets**, including all 256 factory slots, 99-parameter
  state, the original editor, helper recovery, source pinning and signatures.

The long candidate window, shorter matrix windows and final binary checks used
different build stages. [V1-RELEASE.md](docs/V1-RELEASE.md) distinguishes them.
[PUBLIC-SOURCE-IMPORT.md](docs/PUBLIC-SOURCE-IMPORT.md) records the separate
standalone-repository build checks.

Historical bit-identical audio is unverified. The accelerated translator has
documented numerical differences from full x87 extended precision, and warm-up
advances the original module's random streams. Unlisted rate/buffer combinations,
arbitrary instance counts and heavy competing workloads remain unqualified.
The original-source test projects, recordings and local binary ZIP are private
acceptance artifacts and are not repository downloads.

## Support and project notes

See [SUPPORT.md](SUPPORT.md) for missing-runtime errors, diagnostics, rollback
and what to include in a [GitHub issue](https://github.com/vincepeaceuk/A1-Synth-Revived-VST3/issues).
Further notes cover [recovery](docs/INVESTIGATION.md),
[the restored editor](docs/ORIGINAL-UI.md),
[the audio pipeline](docs/PERFORMANCE.md),
[chord stability](docs/CHORD-STABILITY.md) and
[verification history](docs/VERIFICATION.md).

## Licensing and original materials

The restoration integration code, tests and build tools use
**AGPL-3.0-or-later**, following the original workspace's licensing and JUCE's
AGPL option. See [LICENSE.md](LICENSE.md), the full [LICENSE](LICENSE) and
dependency notices in [licenses/](licenses/). The x87 translation source remains
MIT licensed and is fetched at a pinned revision.

The original A1 engine, factory banks, artwork, XML, Steinberg branding and
CrossOver components retain their respective owners' rights. They are not
relicensed by this repository. A locally generated binary package contains
original materials and should not be assumed freely redistributable.
