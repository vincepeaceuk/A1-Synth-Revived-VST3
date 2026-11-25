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
