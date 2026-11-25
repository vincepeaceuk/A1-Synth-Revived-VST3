# Private build inputs

The public repository supplies the integration source and build tools. The
following matching original resources must be supplied locally. They are not
distributed in this repository.

| Local path | Required input |
| --- | --- |
| `local/original/a1.dll` | Original Windows x86 A1 DLL, 224,612 bytes |
| `local/original/A1_Default_Bank2.fxb` | Original 128-slot Windows bank |
| `local/original/A1.vstxml` | Original parameter metadata |
| `local/original-mac/a1_default_bank1.fxb` | Original 128-slot Mac bank |
| `local/original-ui/BITMAP_128.png` through `BITMAP_148.png` | All 21 recovered original panel images |
| `local/tools/zig-aarch64-macos-0.15.2/zig` | Zig 0.15.2 Apple Silicon toolchain |

[ORIGINAL-ASSETS.sha256](ORIGINAL-ASSETS.sha256) records all 25 original
resource hashes. Run this from the repository root after placing your files:

```sh
shasum -a 256 -c docs/ORIGINAL-ASSETS.sha256
```

## Reuse your existing local A1 Revival resources

If you already have the matching private A1 Revival bundle, copy its original
resources into the build-input layout. Replace the example path below with
your own bundle's `Contents/Resources` directory:

```sh
mkdir -p local/original local/original-mac local/original-ui local/tools
a1_resources='/path/to/A1 Revival.vst3/Contents/Resources'
cp "$a1_resources/a1.dll" local/original/
cp "$a1_resources/A1_Default_Bank2.fxb" local/original/
cp "$a1_resources/A1.vstxml" local/original/
cp "$a1_resources/A1_Default_Bank1.fxb" local/original-mac/a1_default_bank1.fxb
cp "$a1_resources"/OriginalUI/BITMAP_*.png local/original-ui/
shasum -a 256 -c docs/ORIGINAL-ASSETS.sha256
```

Alternatively, use your already-extracted compatible originals from the vendor
installers. The [investigation](INVESTIGATION.md) records the verified source
archives and extraction history. The original Mac executable is PowerPC and is
not the running sound engine. UI PNGs are converted original resources; arbitrary
replacement images do not satisfy the recorded manifest.

Download the [Zig 0.15.2 aarch64-macos toolchain](https://ziglang.org/download/0.15.2/zig-aarch64-macos-0.15.2.tar.xz)
and extract its directory under `local/tools/`. The official
[download index](https://ziglang.org/download/index.json) records its SHA256 as
`3cc2bab367e185cdfb27501c4b30b1b0653c28d9f73df8dc91488e66ece5fa6b`.
The compiler is used to build the 32-bit Windows helper with strict warnings.

## Dependencies fetched by the build

- `python3 tools/bootstrap.py` prepares `deps/JUCE` at JUCE 9.0.5 commit
  `d60b7a78030661574a615b586d9325ba7a7fa7dd`.
- `tools/prepare-vst3-wrapper.py` requires the exact original wrapper SHA256
  `2c057fbf6eecdf829ac830fa5e87967177af034455c7a8b177359acdbdde1114`.
  It generates a private build copy with the reviewed A1 program-queue adaptation.
  The JUCE checkout stays unchanged.
- `tools/prepare-x87-runtime.py` fetches x87sidecar at commit
  `052944fc613e187f5e238bfa362b411787045d6b`, applies the exact tracked patch,
  rejects unexpected tracked/untracked changes and builds the local translator.
- CrossOver supplies the local runtime files. Their build-time hashes are
  recorded and checked at startup. Installed CrossOver files are not modified.

The CMake build requires the original UI images even for its full regression
suite. Runtime/source-pin fixtures also require their corresponding prepared
local sources and runtime. No original A1 DLL is loaded by the generic-host
unit fixtures; real musical rendering requires actual Cubase.

## Compatibility environment

The qualified instrument expects a dedicated CrossOver Windows 10 64-bit bottle:

```text
~/Library/Application Support/A1RevivalPreview/bottles/A1RevivalPreview/
```

It must contain `cxbottle.conf` and the prepared Windows environment. This public
source import uses the existing qualified local bottle; it neither redistributes
nor creates a bottle. Initial provisioning on another Mac has not been qualified.
Keep this setup separate from unrelated CrossOver bottles.

The historical `tools/probe.py` script creates a separate research bottle when
explicitly run. It is a generic-host investigation tool, not a provisioning or
acceptance tool for the released instrument. Its original A1 host rejection
(exit 12) is expected and must not be reported as successful audio rendering.
