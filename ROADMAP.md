# Project Roadmap

This roadmap is based on the current event flow, OBS 31.1.1 source behavior, and
recent OBS runtime logs. It separates release-blocking verification from larger
maintenance work so reliability improvements can land incrementally.

## Post-release Verification: `Mute to me` capture safety (P0, updated 2026-08-16)

### Reported behavior

- After clicking `Mute to me`, the user can no longer hear the monitored audio,
  but the audio also stops reaching OBS/the recording.
- Expected behavior: only the local OBS monitoring playback becomes inaudible.
  The source meter, internal OBS track mix, recording, and streaming output must
  continue without any interruption or level change.
- Status: root cause confirmed, the fixed DLL was installed, and the user
  confirmed the intended heard/silent behavior after removing the direct
  Windows playback path. Formal post-fix saved-waveform verification remains a
  release check.

### Confirmed root cause and fix

- OBS 32.2.1 was loading the installed `1.1.0` DLL from the system plugin
  directory while the development worktree contained newer uninstalled code.
- In a live recording, audio measured about `-31 dB` immediately before mute,
  digital silence (`-91 dB`) throughout mute, and about `-33 dB` after unmute.
  A later toggle reproduced the same exact pattern.
- The Windows backend selected audio sessions only by the current OBS process
  ID. Window/Game/Application Audio Capture can create a process-loopback client
  in that same OBS-owned session, so `ISimpleAudioVolume::SetMute(TRUE)` also
  removed the captured signal.
- New mute operations no longer change Windows audio-session volume. For
  `Monitor and Output`, the plugin temporarily disables only source monitoring;
  for `Monitor Only`, it temporarily mutes the already output-excluded source.
- Before changing a source, the plugin persists its original monitoring and
  mute state in source private settings and immediately saves the scene
  collection. Unmute, unload, and startup recovery restore that state, including
  after a save or interrupted run.
- The fixed source builds with the Windows toolchain and all current unit tests
  pass. OBS loaded the fixed `1.1.1` development DLL and the user confirmed that
  `Mute to me` now produces local silence with the documented capture-safe
  routing. A post-fix saved-file waveform measurement is still pending.

The existing warnings-as-errors build, unit tests, OBS 32.2.1 ABI check, and
isolated plugin-load smoke test passed. Those checks prove that the plugin
builds and loads; they do **not** exercise a real monitored source or inspect a
recorded waveform before and after the mute transition.

### Remaining live verification

1. Verify with a disposable OBS profile and the smallest valid independent
   routing setup:
   - disable global `Desktop Audio`;
   - add one directly captured application/window/game source or one audio input
     capture source;
   - enable capture audio, set `Audio Monitoring` to `Monitor and Output`, and
     assign the source to recording Track 1;
   - enable Track 1 in the recording settings and record to MKV;
   - select an explicit physical monitoring device instead of `Default`.
2. Record at least ten seconds spanning `Hear -> Mute -> Hear`. For every phase,
   note three independent signals: what the user hears, whether the source and
   output meters continue moving, and whether the saved file contains a
   continuous waveform/audio signal.
3. Repeat with `Desktop Audio` enabled only as a compatibility case. The fixed
   implementation must not touch endpoint or process-session volume in either
   configuration.
4. Verify that `Monitor and Output` changes to `Monitor Off` only while muted,
   remains assigned to Track 1, and restores exactly on `Hear`, unload, and
   forced restart. Verify `Monitor Only` separately and confirm it never enters
   the recording mix.

### Likely causes to distinguish

- OBS loaded the older installed DLL rather than the development build.
- The source uses `Monitor Only`, or is not assigned to the recorded track.
- `Desktop Audio`/endpoint loopback is the only path carrying the monitored
  signal back into OBS. Muting OBS's render session then also silences that
  loopback capture by design.
- The application is playing directly to the physical device, while OBS is
  capturing or monitoring a different path.
- The session filter selected an unexpected OBS-owned render session, or a
  monitoring endpoint/session changed during the test.
- The OBS meter continues but only the file is silent, indicating a recording
  track/output configuration problem rather than a monitoring mute problem.

The fix intentionally uses the per-source monitoring boundary. Because OBS can
persist temporary monitoring types, recovery metadata is written first and
saved in the same source's private settings; startup recovery makes that
otherwise-unsafe approach resumable and reversible.

### Acceptance criteria

- During `Mute`, the user hears silence while source/output meters and the
  encoded Track 1 signal remain continuous and unchanged.
- The saved MKV contains audio across the complete `Hear -> Mute -> Hear`
  interval, verified by playback and waveform/level inspection.
- `Hear` restores every source's exact prior monitoring and mute state.
- No duplicate audio or feedback occurs with the documented recommended setup.
- Late-monitored sources, monitoring-device changes, plugin unload, OBS restart,
  and forced-termination recovery still pass after the fix.
- Add an automated test where practical, plus retain the manual runtime matrix
  because a unit test cannot prove Windows/OBS endpoint behavior by itself.

### Handoff state

- Path scope: this repository only.
- Files changed by this handoff: mute implementation and UI guidance in
  `src/ApplyPreset.cpp`, `src/ApplyPreset.hpp`, `src/PresetDock.cpp`, and
  `src/plugin-main.cpp`; build marker in `buildspec.json`; plus README, web
  guide, changelog, and this roadmap.
- Checks run: installed-DLL path/version/hash inspection; live pre-fix MKV
  audio-level analysis across two mute intervals; fixed-DLL load verification;
  user-confirmed post-fix listening behavior; Windows RelWithDebInfo builds; and
  the current unit test suite. Builds and 1/1 tests passed. Both pre-fix mute
  intervals produced digital silence and confirmed the defect.
- Unresolved risk: the formal post-fix saved-waveform matrix and an end-to-end
  attached-dock interaction check remain manual follow-up work. The final
  `1.1.1` DLL is installed, loads successfully, and passed the automated build,
  test, formatting, and packaging checks.
- Lease state: no workspace lease was claimed.

## Architecture Baseline

The plugin has four main responsibilities:

1. `plugin-main.cpp` translates OBS frontend events into plugin actions.
2. `ScenePreset` persists one preset in each scene source's private settings.
3. `PresetValidation` enforces libobs-compatible values before persistence or
   application.
4. `ApplyPreset` mutates live video state, profile recording settings, encoder
   JSON, and recoverable per-source monitoring state. `PresetDock` edits those
   values.

The key invariant is that a scene switch must be idempotent: applying the same
preset twice must not reset the video pipeline, rewrite configuration files, or
restart an output twice.

## P0: Extended Runtime Verification

- Exercise the plugin with disposable OBS profiles on both the pinned OBS
  31.1.1 SDK version and the current supported OBS 32 runtime.
- Verify from logs that startup and repeated same-scene events do not call
  `obs_reset_video()` when the pipeline already matches.
- Test recording restart success, invalid-path rejection, immediate encoder
  failure, a previously paused recording, and a scene change while stopping.
- Test FLV and multi-track containers separately, including "Copy from current
  OBS settings".
- Test profile changes and scene-collection changes with both enabled and
  disabled presets.
- Test Windows `Mute to me` with no initial playback session, multiple
  monitored sources, a monitoring-device switch/hot-plug, a session that was
  already muted, and a forced OBS termination followed by startup recovery.
- Run the Windows, macOS, and Ubuntu CI jobs added for the validation tests.

Do not install a development DLL over a running OBS process. Close OBS first
and use a disposable profile/scene collection for the runtime matrix.

## P1: Reliability

- Add a restart watchdog for the rare case where OBS acknowledges
  `RECORDING_STARTING` but emits neither `RECORDING_STARTED` nor
  `RECORDING_STOPPED`.
- Replace the current lightweight Windows session/endpoint polling with native
  notifications if profiling shows the maintenance timer is material, and let
  a secondary OBS instance acquire the recovery guard after its owner exits
  without requiring that secondary instance to restart.
- Refresh the monitoring-device list when devices change and expose a clear
  unavailable-device state instead of retaining a stale combo entry.
- Coordinate profile writes with an open OBS Settings dialog so a later dialog
  save cannot overwrite a just-applied scene preset.
- Add structured diagnostic logging for the selected scene, output mode,
  effective format, encoder mode, and every skipped override.

## P2: User Experience

- Add an **Audio Setup Check** that inspects, without changing anything, the
  selected source's capture-audio state, monitoring mode, source track mask,
  recording track mask, explicit monitoring endpoint, and duplicate global
  Desktop Audio state. Present one review screen before applying any OBS-owned
  fixes.
- Add a **Fix OBS settings** action for the safe subset: set the selected source
  to `Monitor and Output`, align a recording track, and select/disable duplicate
  OBS devices only after explicit confirmation. Windows per-application output
  routing remains a user choice because the plugin cannot safely infer the
  intended listening endpoint or install a virtual audio driver.
- Put the full dock in a scroll area for short or narrow dock layouts.
- Make format and bitrate controls mode-aware, including an explanation when an
  override cannot affect the selected OBS output mode.
- Show per-field validation and the last effective value rather than only a
  single dock-level status message.
- Refresh labels and help text through locale files and add a complete
  localization path beyond the dock title.
- Continue using the original storage keys, dock ID, bundle/package identity,
  and plugin filename after the public rename to `Scene Output Control` so
  scene presets, OBS layouts, and installed-plugin replacement remain
  compatible.

## P3: Maintainability

- Split `ApplyPreset.cpp` into video, recording, and monitoring modules with
  narrow interfaces and independently testable state.
- Extract the recording-restart controller into a pure state machine with event
  sequence tests.
- Add a small libobs integration harness for private-settings persistence and
  profile-config behavior.
- Add an OBS-version CI matrix instead of compiling against only one pinned SDK
  snapshot.
- Review inherited workflow permissions and update the OBS plugin template
  without hand-editing generated template output.

## Compatibility Policy

- Keep stored presets forward-compatible: unknown keys and recording format IDs
  must not be silently replaced during unrelated edits.
- Treat unsupported output modes as explicit no-ops with useful status, never
  as successful writes.
- Keep recording and streaming behavior separate. A recording-only override
  must not mutate the stream encoder.
- Maintain public OBS API usage unless a specific compatibility decision is
  documented and tested.
