// This illustration changes only its own DOM. It never connects to OBS or plays audio.
(() => {
  const muteButton = document.getElementById('demo-mute');
  const monitoring = document.getElementById('monitor-branch');
  const monitoringLabel = document.getElementById('demo-monitoring');
  const status = document.getElementById('demo-status');
  const frame = document.getElementById('preview-frame');
  const resolution = document.getElementById('demo-resolution');
  const fps = document.getElementById('demo-fps');
  const sceneButtons = [...document.querySelectorAll('[data-scene]')];
  const presets = {
    gameplay: { resolution: '1920 × 1080', fps: 60 },
    vertical: { resolution: '1080 × 1920', fps: 30 },
  };

  if (!muteButton || !monitoring || !monitoringLabel || !status || !frame || !resolution || !fps) return;

  for (const button of sceneButtons) {
    button.disabled = false;
    button.addEventListener('click', () => {
      const preset = presets[button.dataset.scene];
      if (!preset) return;
      for (const other of sceneButtons) other.setAttribute('aria-pressed', String(other === button));
      frame.classList.toggle('vertical', button.dataset.scene === 'vertical');
      resolution.textContent = preset.resolution;
      fps.textContent = `${preset.fps} FPS · applies while OBS outputs are idle`;
    });
  }

  muteButton.disabled = false;
  muteButton.addEventListener('click', () => {
    const muted = muteButton.getAttribute('aria-pressed') !== 'true';
    muteButton.setAttribute('aria-pressed', String(muted));
    monitoring.classList.toggle('is-muted', muted);
    monitoringLabel.textContent = muted ? 'Silent to you' : 'Audible';
    muteButton.textContent = muted ? 'Preview Hear again' : 'Preview Mute to me';
    status.textContent = muted
      ? 'Monitoring is silent. Captured audio stays in the recording mix.'
      : 'Monitoring is audible again. The recording mix was unchanged.';
  });
})();
