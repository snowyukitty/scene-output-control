# Changelog

## Unreleased

## 1.1.1 - 2026-08-16

### Added

- Added validation tests for preset normalization, rational FPS matching, and FLV audio-track selection.
- Added legacy Windows audio-session recovery identity tests.
- Added an `Armed` mute state that catches sources which begin monitoring after the button is pressed.
- Added an in-dock audio setup guide shortcut, including compact-mode access from the mute button's context menu.
- Added a visible `Show resize / output settings` exit from compact mode so per-scene controls are no longer hidden behind a right-click-only action.
- Kept the expanded editor inside a size-independent scroll area and stopped calling `adjustSize()` on attached docks, preventing full-settings expansion from redistributing the OBS layout.
- Added a capture-safe quick setup, track-matching guidance, and a discoverable per-scene resize walkthrough to the web guide.
- Added the tests to all three platform builds in GitHub Actions.
- Added HLS to the known recording-container list while preserving unknown container IDs from newer OBS versions.

### Fixed

- Skipped destructive `obs_reset_video()` calls when the requested scene settings already match the live video pipeline.
- Normalized persisted dimensions, FPS, bitrate, and audio-track values before applying or saving them.
- Fixed Advanced FLV recording presets to write and copy the one-based `FLVTrack` setting used by OBS.
- Made profile and encoder configuration writes safe and idempotent, and stopped claiming bitrate overrides worked for unsupported output modes.
- Reworked recording restart handling to use the latest program scene, preserve paused state, avoid duplicate stop requests, respect an active replay buffer, and report start failures.
- Re-applied the active scene and refreshed profile-scoped dock state after profile changes.
- Preserved unknown preset fields, future schema markers, and recording formats that are not in the plugin's built-in list during unrelated edits.
- Rounded fractional OBS frame rates to the nearest integer when copying them into the integer-only preset editor.
- Rejected incomplete monitoring-device presets and avoided redundant device resets.
- Fixed `Mute to me` silencing application/window/game capture and the recorded track when those capture clients shared OBS's Windows audio session.
- Moved `Mute to me` to the per-source monitoring boundary: `Monitor and Output` keeps its encoder path, while `Monitor Only` is silenced without adding it to the output mix.
- Persisted exact per-source recovery state before changing monitoring, with clean-unmute, unload, and interrupted-startup restoration.
- Added a degraded `Mute error` state when a monitored source cannot be changed or restored safely.
- Added a cross-process Windows guard so concurrent OBS instances cannot overwrite each other's mute recovery state.
- Corrected the monitoring guidance: sources that must also be recorded need `Monitor and Output`; OBS excludes `Monitor Only` sources from the output mix.
- Failed plugin loading cleanly if OBS rejects the dock ID.

### Changed

- Renamed the public product and OBS dock to `Scene Output Control` to reflect its two main responsibilities: scene-aware output presets and personal monitoring control, while keeping its third-party status clear.
- Retained the original plugin filename, package identity, preset key, dock ID, profile section, and recovery identifiers so existing installations and scene collections upgrade in place.
- Added a schema version to newly saved per-scene presets.
- Expanded accepted video dimensions to libobs' current bounds and aligned output sizes the same way as `obs_reset_video()`.

## 1.1.0 - 2026-06-27

### Added

- Added compact dock mode, which collapses the OBS dock to a single `Mute` / `Hear` button.
- Added a right-click compact-dock menu item to restore the full settings panel.
- Added public English documentation and GitHub Pages user guide.

### Fixed

- Fixed Windows `Mute to me` so it targets OBS playback sessions only on the current monitoring endpoint.
- Restored previously muted Windows audio sessions to their original mute state when `Mute to me` is disabled.
- Avoided muting OBS monitoring output globally when the user only wants to stop hearing monitored audio locally.

### Changed

- Replaced the experimental floating mute overlay with OBS dock-native compact mode.
- Clarified installation, usage, monitoring behavior, and build instructions.

## 1.0.0 - 2026-06-23

- Initial public version.
