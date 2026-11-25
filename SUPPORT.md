# Support

Support is community/best-effort through
[GitHub issues](https://github.com/vincepeaceuk/A1-Synth-Revived-VST3/issues).
The verified workflow is genuine Cubase on the prepared Apple Silicon Mac
described in [the release record](docs/V1-RELEASE.md).

## Before reporting a problem

Include your macOS version, Apple chip, Cubase version, CrossOver version,
plug-in version/source commit, sample rate, host block size and reproduction
steps. Say whether the problem occurs with a factory sound, edited sound,
fresh project, MIDI program change, export or saved-project reload.

Do not attach original commercial binaries, factory packs, CrossOver bottles,
private projects or raw diagnostic logs to public issues. Share a small redacted
excerpt if needed; logs may reveal local paths and project information.

## Build cannot find JUCE or original files

Run `python3 tools/bootstrap.py` from the repository root, then follow
[BUILD-INPUTS.md](docs/BUILD-INPUTS.md). Check the original-input manifest using
`shasum -a 256 -c docs/ORIGINAL-ASSETS.sha256`. A source ZIP does not include
JUCE, Zig or the original A1 payloads.

The JUCE wrapper and x87 translation source are pinned deliberately. An
unexpected revision, changed patch or unreviewed source tree is an error to
investigate. Keep that tree and prepare the matching revision separately.

## Missing compatibility environment or runtime mismatch

CrossOver must be installed at its expected location, Rosetta must be present,
and the dedicated `A1RevivalPreview` bottle must already be prepared under:

```text
~/Library/Application Support/A1RevivalPreview/bottles/
```

The build records hashes of the installed CrossOver loader and `ntdll.so`.
After a CrossOver update, rebuild and repeat startup/Cubase checks. Source
build success does not provision a new Mac or establish its musical stability.

## No audio, latency or overload

Use genuine Cubase, route MIDI to an A1 instrument track and route its stereo
output to an active audio bus. Try a named factory sound; category headers are
also retained as bank slots. Check host mute/solo, output gain and MIDI CC7.

Allow about 5–6 seconds per instance for startup. For the main qualified setup,
use 44.1/48 kHz and a 256-sample host buffer. The bridge adds 768 samples of
delay; live monitoring includes that delay plus the audio interface's latency.
Unlisted configurations and heavy competing workloads are not qualified.

Per-instance diagnostics are kept locally under:

```text
~/Library/Logs/A1RevivalPreview/
```

Performance JSON records missing/late frames, input/output drops and callback
deadline misses. A clean listening response alone does not override failed
counters. Also inspect Cubase's Audio Performance Monitor for host dropouts.
The program-selector label can lag MIDI program changes; the original panel's
actual values and audio reflect the selected sound.

## Roll back or uninstall

Quit Cubase before replacing the bundle. Preserve the previous bundle in a
backup folder outside all scanned plug-in directories. To roll back, copy that
preserved bundle into `~/Library/Audio/Plug-Ins/VST3/`, keeping one installed
copy with the A1 Revival identity, then reopen Cubase.

To uninstall, remove only `A1 Revival.vst3` from that VST3 folder while Cubase
is closed. Keep your projects and user-created sounds. Removing the instrument
does not require removing CrossOver or any unrelated bottle.
