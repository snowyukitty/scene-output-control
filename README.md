# Scene Output Control

A third-party plugin for OBS Studio providing **per-scene output presets and personal audio monitoring control**.

Scene Output Control lets each scene carry its own output/recording preferences and adds a global `Mute to me` control that silences OBS monitoring without removing audio from the recording mix. When you switch scenes, the plugin applies that scene's enabled overrides and stores the preset directly in the scene source's `private_settings`, so duplicated scenes keep their preset automatically.

**[Open the setup and user guide](https://snowyukitty.github.io/scene-output-control/)** — including the one-time capture-safe audio setup and where to find per-scene resizing.

## Features

| Setting | When it applies | Notes |
|---|---|---|
| Base/canvas resolution | On scene switch, only while OBS outputs are idle | Uses `obs_reset_video()` |
| Output/scaled resolution | On scene switch, only while OBS outputs are idle | Same pipeline limitation |
| Integer FPS | On scene switch, only while OBS outputs are idle | Same pipeline limitation |
| Recording folder | Next recording | Writes the active OBS profile config |
| Recording format | Next recording | `mkv`, `hybrid_mp4`, `mp4`, `mov`, `fragmented_mp4`, `fragmented_mov`, `mpegts`, `flv`; `hls` in Advanced mode |
| Recording audio tracks | Next recording | Six-track bitmask; FLV supports one track and Simple-mode FLV uses track 1 |
| Recording video bitrate | Next recording | Advanced output mode only; writes `recordEncoder.json` as CBR |
| Audio monitoring device | Immediately, even while recording | Changes the OBS monitoring playback device only |

Global controls:

- **Mute to me**: one click stops *you* from hearing OBS-monitored audio without changing OBS's internal recording/streaming mix.
- **Compact dock mode**: keeps the mute control small and shows a clear `Show resize / output settings` button for returning to the full panel. The full editor scrolls inside the dock's existing space so expanding it does not resize the attached OBS layout. The mute button's right-click menu provides the same action.
- **Audio setup guide**: opens the capture-safe routing checklist from the full dock or the compact button's context menu.

## Important OBS Limitation

OBS cannot change base resolution, output resolution, or FPS while recording, streaming, replay buffer, or virtual camera output is active. That is a libobs video pipeline limitation, not a plugin limitation.

Behavior:

- If OBS is idle, scene video overrides apply immediately.
- If an output is active, recording folder/format/tracks/bitrate are staged for the next recording, but video changes are blocked.
- If `restart recording to apply video changes` is enabled and recording is the only active output, the plugin can stop recording, apply the video change, and start a new recording. This creates a new file.

## Audio Monitoring And "Mute To Me"

OBS audio monitoring is the playback path that lets *you* hear sources. It is separate from the recording/streaming encoder path.

```text
Source ("Monitor and Output")
├─ OBS track mix ───────────────> encoder ──> recording / stream
└─ OBS monitoring ──> playback session ──> speakers / headphones
                              "Mute to me" acts here
```

`Mute to me` only affects the lower playback branch:

- For a `Monitor and Output` source, the plugin temporarily disables monitoring while leaving the source in OBS's normal output/encoder mix.
- For a `Monitor Only` source, the plugin temporarily mutes the source because OBS already excludes it from recording and streaming.
- Before changing a source, the plugin stores its exact original state in that source's private settings and saves it with the scene collection. Disabling the control or unloading the plugin restores the state; startup recovery handles an interrupted OBS run.
- If no source currently uses OBS monitoring, the control shows **Armed** and suppresses any source that begins monitoring later.
- If a source cannot be updated or restored safely, the control shows **Mute error** instead of claiming that monitoring is silent.
- On Windows, a cross-process guard gives recovery ownership to only one OBS instance. Later OBS instances show the control as unavailable instead of risking conflicting scene-collection recovery state.

The plugin deliberately does not mute the Windows audio session owned by OBS. Application/window/game audio capture can share that session, so session-level mute can also silence the recorded signal.

For audio you want to hear *and* record, open **Advanced Audio Properties**, set the source's **Audio Monitoring** to `Monitor and Output`, and make sure the source is assigned to a recorded track. `Monitor Only` can also be silenced by this control, but OBS intentionally excludes that source from its recording/streaming output.

If a sound is being played directly by the operating system to your headphones or speakers, OBS monitoring controls cannot silence it without also affecting what the system loopback capture receives.

Recommended Windows setup:

1. If using per-application audio from Window Capture, Game Capture, or Application Audio Capture, disable the global **Desktop Audio** device in **Settings -> Audio** to avoid duplicate capture and echo.
2. In **Settings -> Advanced -> Audio**, choose the physical headphones or speakers as the **Monitoring Device** instead of relying on `Default`.
3. Enable **Capture Audio** on the Window/Game Capture source when applicable.
4. In **Advanced Audio Properties**, set that source to **Monitor and Output** and select the track that the recording uses.
5. In **Settings -> Output -> Recording**, enable the same recording track.
6. Make a short test recording, toggle **Mute to me**, and confirm that the source meter continues moving and the saved file still contains audio.

For a browser or game that still plays directly to the listening device, route the application to a virtual audio cable or another device you are not listening to, capture that device in OBS, and listen through OBS **Monitor and Output**. Do not enable Windows **Listen to this device** for that route, because it bypasses the plugin's OBS-monitoring control. See OBS's [Application Audio Capture Guide](https://obsproject.com/kb/application-audio-capture-guide) for the capture and routing options.

## Usage

1. Open OBS and enable **Docks -> Scene Output Control**.
2. Choose the scene to edit. The dock follows the current program scene until you manually select another scene.
3. Enable **per-scene overrides** for that scene.
4. Check only the settings that scene should override.
5. Optionally click **Copy from current OBS settings** to use the current profile values as a starting point.
6. Click **Apply now** to apply immediately when the edited scene is the current program scene, or just switch scenes and let the plugin apply automatically.
7. Use **Mute to me** whenever you want to stop hearing monitored audio without affecting recording.
8. Use **Compact dock mode** when you want a minimal monitoring control. Click **Show resize / output settings**, or right-click the mute button and choose **Show full settings**, to expand it again.
9. Open **Audio setup guide** from the dock whenever an application still plays directly to your listening device.

## Installation

Download the current installers from the [latest GitHub Release](https://github.com/snowyukitty/scene-output-control/releases/latest).

Windows plugin layout:

```text
%ProgramData%\obs-studio\plugins\obs-auto-resize-output\bin\64bit\obs-auto-resize-output.dll
%ProgramData%\obs-studio\plugins\obs-auto-resize-output\data\locale\en-US.ini
```

Close OBS before replacing the DLL.

The installed directory, DLL filename, macOS bundle ID, preset storage key, and OBS dock ID intentionally retain the original `obs-auto-resize-output` identifiers. Keeping them stable lets an update replace the old plugin cleanly and preserves existing scene presets and dock layouts after the public product rename.

## Build

This repository uses the OBS plugin-template build system. The pinned dependencies are in `buildspec.json` and currently target OBS Studio 31.1.1 with the matching OBS dependencies and Qt6 package.

Cloud build:

```powershell
gh workflow run dispatch.yaml --ref main -f job=build
```

Local Windows build with the template presets:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

See [BUILD.md](BUILD.md) for the full build guide.
See [ROADMAP.md](ROADMAP.md) for the architecture baseline, release-verification matrix, and prioritized follow-up work.

## Design Notes

- Presets are stored under the scene source's `private_settings` key `auto_resize_output`.
- Only public OBS APIs are used: `obs.h`, `obs-frontend-api.h`, profile `config_t`, and `obs_data`.
- Scene duplication carries presets because libobs copies source private settings during duplication.
- `Mute to me` temporarily changes per-source monitoring state only after persisting recovery metadata in source private settings. Unmute, unload, and startup recovery restore the exact prior state, including after a scene-collection save or interrupted run.

## Current Scope

Supported:

- OBS Studio 30/31-era recording config keys, with CI pinned to OBS 31.1.1.
- Windows, macOS, and Ubuntu builds through GitHub Actions.
- Advanced output mode with a dedicated recording encoder for recording bitrate overrides.

Known limits:

- Simple output mode, Custom Output (FFmpeg), and Advanced mode's stream-encoder reuse do not expose a clean independent recording bitrate override.
- Video changes cannot be applied while OBS outputs are active unless the plugin restarts recording.
- `Mute to me` only affects audio heard through OBS monitoring.
- Only the first active OBS instance can use Windows `Mute to me`. A later instance must be restarted after the owner exits before it can acquire the recovery guard.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
