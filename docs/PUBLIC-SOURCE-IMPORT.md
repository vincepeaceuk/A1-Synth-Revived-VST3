# Standalone public source import

Date: 25 November 2026. Destination:
[vincepeaceuk/A1-Synth-Revived-VST3](https://github.com/vincepeaceuk/A1-Synth-Revived-VST3).

The source was imported from the completed A1 Revival 1.0 workspace at commit
`e34b9fa`. All files in `src/`, existing regression tests, original runtime
preparation tools and the reviewed translator patch are retained byte for byte.
The dedicated repository has its own history; unrelated plug-ins and private
workspace history are not imported.

The publication follows the existing standalone CamelSpace source pattern:
root-level CMake, pinned JUCE bootstrap, source and tests, Markdown build/use/
support notes and explicit original-material rights. Commercial A1 payloads,
CrossOver components/bottles, local binary packages, personal projects, audio
recordings and raw diagnostics stay outside public Git.

## Acceptance and build layout

The publication journey is: clone this repository, fetch the pinned dependency,
provide the documented original inputs/toolchain/runtime, configure from the
repository root, build and run the existing tests. Original A1 DSP, host checks,
program/state handling, UI behavior and runtime policy are unchanged.

The first root configure reproduces the workspace-relative JUCE-path problem:
JUCE exists at `deps/JUCE`, but the imported CMake expects `../deps/JUCE`.
The intended standalone fix changes only that default path. Its RED and GREEN
checkpoints and final validation results are recorded here before publication.

- RED checkpoint `0d92d43`: the root configure failed because its JUCE lookup
  used the old parent-workspace path, despite the pinned checkout being present
  at the standalone root's `deps/JUCE`.
- GREEN: the default JUCE path now resolves inside this repository. The same
  configure command, in a fresh build directory, completes successfully. No
  DSP, helper, processor, editor, protocol or regression-test source was changed.

## Executed standalone verification

The fresh checkout was configured and built on macOS 26.6.2 with the matching
original assets supplied under ignored `local/`, the existing Zig 0.15.2
toolchain, installed CrossOver 26.3 and the existing qualified A1 bottle.
JUCE was cloned afresh from its official repository by the bootstrap. The
translator source was separately fetched, pinned, patched, built and probed by
the existing runtime tool.

```sh
python3 tools/bootstrap.py
shasum -a 256 -c docs/ORIGINAL-ASSETS.sha256
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --parallel 1
```

| Check | Result |
| --- | --- |
| Root configure and full Release build | PASS |
| Existing CTest suite | **13 / 13 passed**, 10.99 seconds |
| Public bootstrap | Correct JUCE pin accepted; mismatched Git revision and unverified source directory rejected without changing their sentinel files |
| Original input manifest | All 25 resource hashes match |
| Native bundle | ARM64; package fixture validates minimum macOS, stable IDs and unchanged original payloads |
| Bundle/runtime signatures | Strict verification passes in the release package fixture |
| Released implementation comparison | All 40 imported implementation/test/tool/patch files unchanged; CMake differs only in the dependency path |
| Python tools and tests | Syntax checked |
| Markdown | Local link targets resolve |
| Publication inventory | Source, tests, patch, text manifests, notices and Markdown only; no binary payloads, original resources, bottles, builds or private evidence |
| Publication secret scan | No matching credential/private-key patterns in the reviewed files |

There is no separate native linter, project-wide type-check command or
whole-plugin coverage percentage configured. Compiler diagnostics, focused
fixtures, resource hashes, signatures and the final diff form the applicable
checks. Raw build/test logs and import manifests remain private under `local/`.

This source publication did not replace the installed release or repeat the
full musical Cubase session with the newly built bundle. The actual Cubase
acceptance remains the explicitly identified 24 November 1.0 record; all musical
implementation files are unchanged. The target acceptance for this task is the
published GitHub source tree and rendered README, checked after pushing.
