# Contributing to Scene Output Control

Thanks for helping make scene presets and personal audio monitoring easier to
use. Documentation fixes, reproducible bug reports, and platform testing are
as useful as code changes.

## Start here

- Read the [user guide](https://snowyukitty.github.io/scene-output-control/) and
  [known limits](README.md#current-scope) before reporting unexpected behavior.
- Search [existing issues](https://github.com/snowyukitty/scene-output-control/issues).
  Use the bug report or feature request form for a new issue.
- Identify both the plugin and OBS versions. The current published plugin is
  **1.1.1**; the Windows Listen checker on `feat/audio-setup-check` is still
  unreleased. Say whether you tested a release or a development build.
- Discuss a large behavior change before investing in an implementation.
  Small fixes and clearer instructions can go straight to a focused pull request.

## A useful audio bug report

Describe three separate observations: what you hear, what the OBS meter shows,
and what is present in the saved recording. Include the source type, monitoring
mode, recorded track, monitoring device, application output route, and whether
Windows **Listen to this device** or a mixer forwards audio directly.

For a minimal reproduction, use one source, one recorded track, and a disposable
OBS profile/scene collection. If possible, record a short **Hear → Mute → Hear**
test and play the saved file back. Meter movement alone does not prove that the
correct audio track was recorded. Never interrupt an important recording to test.

Before sharing logs, screenshots, or samples, remove stream keys, tokens, private
paths, device identifiers, window titles, and personal content. A short redacted
excerpt is usually enough; do not upload an entire scene collection by default.

## Build and validate

Follow [BUILD.md](BUILD.md) for the pinned OBS/Qt dependencies and platform
presets. Keep your build directory separate from the source tree.

For plugin changes, run the relevant build and CTest suite. For example:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

Use the repository's `.clang-format` with clang-format 19 for changed C/C++ files,
and `.gersemirc` with Gersemi for changed CMake files. Report the exact checks you
ran, the platform, and any skipped runtime tests. Audio changes need a saved-file
check as well as build/unit-test evidence. Do not describe a diagram, isolated UI
smoke test, or moving meter as proof of a real recorded audio result.

For website changes, see [docs/README.md](docs/README.md). The site is static:
no Node build, analytics, external fonts, audio playback, or OBS connection.

## Keep changes focused

- Use English for code, comments, commits, and developer documentation.
- Preserve the existing package identifiers and preset storage keys. They are
  compatibility boundaries, even though the product name is Scene Output Control.
- Preserve source recovery state before changing monitoring behavior.
- Keep the live-output restrictions explicit: resolution/FPS need idle outputs,
  and a recording restart creates a new file.
- Avoid credentials, personal recordings, generated dependencies, and build
  artifacts in a contribution.
- Explain the problem, resulting behavior, and validation in your pull request.

Contributions are distributed under the project's existing
**GPL-2.0-or-later** license. See [LICENSE](LICENSE).
