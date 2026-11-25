# A1 investigation — 24 November 2026

> Engineering record: initial recovery and bridge. See [V1-RELEASE.md](V1-RELEASE.md) for
> the completed local release and [PUBLIC-SOURCE-IMPORT.md](PUBLIC-SOURCE-IMPORT.md)
> for the standalone source checks. Historical commit IDs refer to the original
> Audio Plugins workspace. Paths under `local/` or `dist/` describe private
> evidence and packages, not public repository downloads.

## Outcome

Recovery succeeded, followed by a working ARM64 macOS VST3 frontend using the
original Windows A1 DSP through CrossOver. Real Cubase 15 MIDI playback, Stereo
Out Wave export and save/close/reopen recall were verified on this Mac. The
generic standalone probe remains rejected by the original host restriction.
This is an experimental compatibility preview; native DSP, a faithful original
editor, exhaustive sound equivalence and production performance remain open.

## Supplied source

The initially supplied image was unreadable in the observed local state. The
original files were left unchanged; recovery used the official archives below.

## Official alternative

[Steinberg's conversion article](https://helpcenter.steinberg.de/hc/en-us/articles/115000075750-Converting-Cubase-VST-songs-ALL-ARR-into-CPR-format)
links the [full Windows SX 3.1 installer](https://download.steinberg.net/downloads_legacy/Cubase_VST_Song_Conversion/Cubase_SX_3.1_CD_Installer_win.zip).

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Full installer ZIP | 139,452,345 | `f7db28658dbb66e722f1327d2dc29bdc088de632d2d181625888cbc9ac5ecf67` |
| `Installer Data/Setup_CubaseSX.exe` within ZIP | 84,990,597 | `e5814fa52ac6f261b9b0dd04e0c96e9f2db71a3e4eadfe1f8b06bfdbfff6f8f7` |
| `a1.dll` | 224,612 | `073727452a7072b1a711ac639b77710577d717d57e4ebd1ba55af949212b3cc4` |
| `A1_Default_Bank2.fxb` | 58,012 | `7d27216672c0de3f365b96e617358f7bf3942e2004d765ef55dbb019828dec7a` |
| `A1.vstxml` | 5,758 | `6a8684222caa43c54cce97c3655a133ff293283dd69de9a91541bf10ed0994a7` |

The official Windows 3.1.1.944 update alone contains no A1 filename in its Wise
script. The official Mac 3.1.0.933 update has 308 archive members and no A1.
Those bounded inventories do not imply that other Steinberg packages lack A1.

## Static extraction

[REWise](https://codeberg.org/CYBERDEV/REWise) 0.4.0, GPL-3.0-or-later, commit
`9d267fc1b305ec452b3e4edc106b73beffe8ee1b`, was built locally with Clang/zlib.
Its source and compiled tool are retained in `local/tools/REWise/`.

The full installer's script-driven `rewise -V` verification completed with all
CRCs valid and no stderr. Raw extraction finds the three A1 assets as streams
637, 638 and 639 respectively. The raw scan subsequently emits an end-of-input
warning and leaves an empty sentinel; the successful script-driven verification
is the integrity evidence. The full installer was never launched.

Reproduction, from this repository root, into a new writable output directory:

```sh
mkdir local/raw-recovery
local/tools/REWise/build/rewise -r local/raw-recovery local/downloads/Setup_CubaseSX.exe
local/tools/REWise/build/rewise -V local/downloads/Setup_CubaseSX.exe
```

`FILE_000000637` is the bank, `FILE_000000638` the XML and
`FILE_000000639` the DLL. Verify the hashes above before naming/copying them.
REWise overwrites matching destination files; use a fresh directory.

The DLL's PE resources identify A1 Analog Synth Unit, original filename A1.dll.
LLVM 20 identifies PE32 / i386, export `main`, and Neolite packing sections
`.neolit` and `Oreloc`. Much of the original code is not directly represented
by the packed file's normal code section; static stub inspection cannot establish
DSP behavior. REA's resource parser successfully inspected the official update;
its separate Hopper analysis of A1 did not complete, and authenticated document
closure could not be confirmed. That incomplete analysis supplies no DSP claims.

## Factory data

The bank contains 128 slots with VST identifier `3ESS`, 99 stored parameters per
slot, and exact EOF alignment: `156 + 128 × (56 + 99 × 4) = 58,012` bytes.
Some slots are category headers. Slot 1 is `BassIndeed  FH` and slot 3 is
`Landscape  T`.

Legacy bank/program `byteSize` fields are zero; parse by `numParams` with bounds
checks. The XML contains parameter groups and reusable envelope templates; its
literal tag count does not equal the instrument's active control count. No
claim is made that all 99 stored values represent active user controls.

## Original Mac recovery

The official [Mac full installer](https://download.steinberg.net/downloads_legacy/Cubase_VST_Song_Conversion/Cubase_SX_3.1_CD_Installer_mac.dmg)
was also recovered: 199,863,276 bytes, SHA-256
`e630734f9933c9111fe8133fd5ef86aa6ae28a01d8b698e8738410211efac8bf`.
7-Zip decompressed its classic HFS partition; the MIT `machfs` reader read the
filesystem, and the inner gzip/CPIO archive verified and extracted successfully.
No installer script or Mac A1 executable was run.

The synth's `aEff` resource contains a **32-bit PowerPC PEF/CFM** binary, not an
Intel or ARM Mach-O. Therefore the recovered Mac executable provides no direct
modern macOS execution path. Its 21 original PICT resources, parameter XML and
different 128-slot Bank 1 were retained under `local/original-mac/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `A1.aEff.pef` | 329,436 | `f3a275b5adc2901407de382d9454856f6b3bf09c63508c06dfb8e59537bff48a` |
| `a1_default_bank1.fxb` | 58,012 | `62c7c9b98780e48b0a2209865a6bcfe19ebbc76ee8b5c3e7b3a150be81f644a0` |

Bank 1 has the same VST ID and 99-float layout, with `FM Bass WMF` in slot 1.
Both banks are exposed in the preview as 256 slots; category labels are retained
as original slots. Full extraction metadata is in
`local/evidence/extraction/A1-MAC-RECOVERY.md`.

## Initial generic runtime probe

`tools/engine_probe.c` is an offline Windows x86 test harness. It declares the
public VST2 ABI, checks struct sizes at compile time, loads the DLL, supplies a
generic host identity, and is prepared to load a bank program, send a note and
render three seconds to a new float WAV. It changes no DLL instructions or
host identity to defeat the observed restriction.

- Zig 0.15.2, `x86-windows-gnu`, C11, `-Wall -Wextra -Werror`: build passed.
- Zig archive SHA-256: `3cc2bab367e185cdfb27501c4b30b1b0653c28d9f73df8dc91488e66ece5fa6b`,
  checked against [Zig's published download index](https://ziglang.org/download/index.json).
- Runtime: this Mac, CrossOver 26.3, new dedicated Windows 10 64-bit bottle.
- `LoadLibraryExA`: succeeded. `GetProcAddress("main")`: succeeded.
- The entry calls host opcodes 1, 33, 32, 38 and 35, then displays an explicit
  unsupported-host message. Wine's `msgbox` trace confirms the text.
- Timed tests were stopped through the dedicated bottle's Wine server.
- No valid effect instance or rendered audio was obtained from this generic
  probe. The private identification exchange was mapped later for actual Cubase
  integration, as described below.
- The initial native process sample had no useful stacks and supplies no cause.
- Proprietary payloads, archives, toolchain, bottle and evidence are ignored by
  Git. No installer, licensing helper or original Cubase executable was run.

## Actual Cubase bridge

The target is genuine Cubase on the qualified Apple Silicon Mac. The native VST3
frontend checks JUCE's actual host-process detection, queries the genuine
`IHostApplication` interface, and requires its reported name to be `Cubase` or
`Cubase ...`. It then passes that observed host identity to the isolated helper.
Generic standalone probes do not gain a Cubase identity.

A1 uses a private host-identification query in addition to public VST callbacks.
The helper adapts that observed legacy exchange only after the native guard
accepts actual Cubase. It validates request signatures, module-relative pointers
and writable memory ranges before replying. No original DLL instructions are
patched and no licensing helper is run.

The original packed DLL was allowed to unpack normally in its loader. A mapped
image was copied solely for read-only disassembly. The analysis PE changes
section offsets and flags for the disassembler, with instruction bytes unchanged;
the running preview loads the original DLL with its recorded original hash.
Provenance: `local/evidence/a1-loaded-analysis.provenance.json`.

The helper reports a valid VST instrument: 99 parameters, 128 original programs,
zero inputs, stereo output, flags `0x119` and implemented `processReplacing`.
It applies the native factory/state parameters and sends sample-offset VST MIDI
events, then returns actual parameters and stereo float audio. A bounded binary
protocol connects processes through a private socketpair. Helper deadlines,
response-size checks and nonfinite-value checks constrain failure behavior.

### Silent first playback and correction

The first real Cubase test initialized successfully and received MIDI but was
silent. A generic callback-selection regression was fixed and covered with a
fixture, but live flags and disassembly showed A1 correctly advertises replacing;
that was not this instrument's mute cause.

Static descriptors identify parameter 98 as MIDI CC7 volume with default `1.0`.
All 128 slots in each recovered bank store `0.0` there. Original voice setup at
RVA `0xe740` multiplies parameter 98 by amplifier parameter 51. Explicitly
applying all bank values therefore muted the synth. The frontend now initializes
only factory catalog parameter 98 to unity. Raw vendor banks stay unchanged;
saved state and live controller changes preserve their actual values. Regression
tests failed before this change and pass afterward, including intentional mute
restoration.

### Verified audio and recall

The real Cubase test used Bank 2 / `HollowPad FH`, four MIDI notes, 44.1 kHz,
256-frame blocks and 120 BPM. The original helper's first MIDI diagnostic showed
program 6, controller 98 = 1 and a nonzero output peak. Cubase's instrument and
Stereo Out meters then showed audio.

Cubase Quick Audio Export produced a 24-bit stereo Wave file, 278,857 frames
(6.323288 seconds), with peak `0.2905390263` (-10.7359 dBFS) and RMS
`0.0524900087`. PCM inspection confirmed nonzero finite output. SHA-256:
`b18ba0b3cdf74103615801ed5b324236502aba7755a19fe14db98a050cae382b`.
The test project was saved, closed and reopened; the new original helper again
received program 6, controller 98 = 1 and generated nonzero audio.

Final review added version-tagged parameter snapshots, note recovery after
MIDI overflow or skipped blocks, fixed maximum helper block capacity, and
bounded child shutdown. Their focused regressions passed (113 Windows helper
checks and four native CTest targets). The final installed module was loaded
after another Cubase restart and exported again. That 24-bit stereo file is
278,601 frames (6.317483 seconds), peak -10.1798 dBFS, SHA-256
`18adcd68ae8206664291c741aa2539e6c61609bc78f4f54fa21784071f5e8cb0`.
The exported files are separate uncontrolled performances; no bit-exact
equivalence is inferred from them. Final module/helper hashes and environment
versions are retained in `local/evidence/final-audio-verification.json`.

## Remaining work at the initial investigation stage

Restore the original visual interface from recovered artwork; compare more
sounds, control curves and MIDI behavior with a historical reference; qualify
automation, instance concurrency, scheduler stalls and buffer/sample-rate changes.
The current tested path is a local Cubase-only bridge with a temporary editor.
It does not establish native DSP portability, bit-exact historical equivalence,
production readiness or AU compatibility.
