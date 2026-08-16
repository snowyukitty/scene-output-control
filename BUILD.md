# Build Guide

This project is based on the OBS plugin-template build system. In normal use you do **not** need to build OBS Studio or install Qt manually. The template workflow downloads the pinned OBS sources, OBS dependencies, and Qt6 package described in `buildspec.json`.

## Recommended Path: GitHub Actions

This is the simplest way to produce a Windows DLL and release artifacts.

1. Push the repository to GitHub.
2. Open the **Actions** tab.
3. Run the **Dispatch** workflow with `job=build`, or push to a branch covered by the regular workflows.
4. Download the Windows artifact from the completed run.
5. Install the unpacked plugin folder into OBS.

CLI equivalent:

```powershell
gh workflow run dispatch.yaml --ref main -f job=build
gh run list --workflow dispatch.yaml --limit 5
gh run download <run-id> -D artifacts\<run-id>
```

## Release Build

Release artifacts are produced by pushing a semantic version tag:

```powershell
git tag 1.1.0
git push origin 1.1.0
```

The tag workflow builds Windows, macOS, Ubuntu, and source artifacts, creates a draft GitHub Release, and uploads checksums. Review the generated artifacts before publishing the release.

## Local Windows Build

Install:

- Git
- Visual Studio 2022 Community with the **Desktop development with C++** workload
- CMake 3.28 or newer

Then run:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

The first configure/build downloads the pinned OBS/Qt dependencies and may take a few minutes.

The checked-in Windows preset targets Visual Studio 2022. With a newer Visual Studio generator, use a separate build directory and select that installed generator explicitly; do not reuse `build_x64` across generators.

## Local macOS / Linux Build

Use the platform presets provided by the template. The exact package requirements are inherited from the OBS plugin-template workflow and the pinned dependency versions in `buildspec.json`.

```bash
cmake --preset macos
cmake --build --preset macos --config RelWithDebInfo
ctest --test-dir build_macos -C RelWithDebInfo --output-on-failure
```

```bash
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64 --config RelWithDebInfo
ctest --test-dir build_x86_64 --output-on-failure
```

## Manual OBS/Qt Build

Only use this route if you already maintain an OBS development environment. Point `CMAKE_PREFIX_PATH` at an OBS build/install tree that exports `libobs` and `obs-frontend-api`, plus the matching Qt6 package.

```powershell
cmake -B build -DCMAKE_PREFIX_PATH="C:/path/to/obs-build;C:/path/to/Qt6"
cmake --build build --config RelWithDebInfo
```

## Install Into OBS

Close OBS before replacing the DLL.

Windows layout:

```text
%ProgramData%\obs-studio\plugins\obs-auto-resize-output\bin\64bit\obs-auto-resize-output.dll
%ProgramData%\obs-studio\plugins\obs-auto-resize-output\data\locale\en-US.ini
```

After installing, start OBS and enable **Docks -> Scene Output Control**.

The installation directory and DLL filename intentionally keep the original
`obs-auto-resize-output` identifier so an upgrade replaces the existing plugin
instead of leaving two modules for OBS to load.
