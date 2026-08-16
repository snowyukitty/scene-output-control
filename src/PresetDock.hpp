// SPDX-License-Identifier: GPL-2.0-or-later
//
// PresetDock: the Qt dock that lets the user configure per-scene output and
// recording overrides. It edits whichever scene is selected in its combo box
// (defaulting to the current program scene) and writes changes straight into
// that scene source's private_settings.

#pragma once

#include <QPoint>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;

struct obs_source;
typedef struct obs_source obs_source_t;

class PresetDock : public QWidget {
	Q_OBJECT

public:
	explicit PresetDock(QWidget *parent = nullptr);
	~PresetDock() override;

	// Called from the frontend event callback (UI thread).
	void refreshSceneList();
	void onProgramSceneChanged();
	void onProfileChanged();
	void refreshMuteState();
	void showStatus(const QString &message);

private slots:
	void onEditSceneChanged(int index);
	void onFieldChanged();
	void onBrowsePath();
	void onCopyFromCurrent();
	void onApplyNow();
	void onMuteToMeToggled(bool checked);
	void onCompactDockModeClicked();
	void showMuteContextMenu(const QPoint &pos);

private:
	void buildUi();
	void loadFromScene();
	void saveToScene();
	void updateEnabledState();
	void updateModeLabel();
	void setMuteToMeFromUi(bool checked);
	void syncMuteToMeUi();
	void setCompactDockMode(bool compact, bool save);
	void loadCompactDockMode();
	void saveCompactDockMode(bool compact);
	// Returns a new reference to the scene currently selected for editing,
	// or nullptr. Caller must obs_source_release().
	obs_source_t *editedScene() const;

	bool m_loading = false; // guard so loadFromScene() doesn't trigger saves

	// Scene-following: the dock normally tracks the program scene, but once the
	// user manually picks a different scene to edit we stop yanking the
	// selection away on every scene change.
	bool m_followProgram = true;
	bool m_programmaticSceneChange = false;
	bool m_syncingMuteUi = false;
	bool m_compactDockModeActive = false;

	QPushButton *m_muteToMe = nullptr;
	QPushButton *m_showFullSettings = nullptr;
	QWidget *m_fullControls = nullptr;
	QScrollArea *m_fullControlsScroll = nullptr;
	QPushButton *m_compactDockMode = nullptr;
	QComboBox *m_sceneCombo = nullptr;
	QLabel *m_modeLabel = nullptr;
	QLabel *m_statusLabel = nullptr;

	QCheckBox *m_enabled = nullptr;

	QGroupBox *m_videoGroup = nullptr;
	QCheckBox *m_useBaseRes = nullptr;
	QSpinBox *m_baseCx = nullptr;
	QSpinBox *m_baseCy = nullptr;
	QCheckBox *m_useOutputRes = nullptr;
	QSpinBox *m_outputCx = nullptr;
	QSpinBox *m_outputCy = nullptr;
	QCheckBox *m_useFps = nullptr;
	QSpinBox *m_fps = nullptr;

	QGroupBox *m_recGroup = nullptr;
	QCheckBox *m_useRecPath = nullptr;
	QLineEdit *m_recPath = nullptr;
	QPushButton *m_browse = nullptr;
	QCheckBox *m_useRecFormat = nullptr;
	QComboBox *m_recFormat = nullptr;
	QCheckBox *m_useAudioTracks = nullptr;
	QCheckBox *m_track[6] = {};
	QCheckBox *m_useRecBitrate = nullptr;
	QSpinBox *m_recBitrate = nullptr;

	QGroupBox *m_audioGroup = nullptr;
	QCheckBox *m_useMonitorDevice = nullptr;
	QComboBox *m_monitorDevice = nullptr;

	QCheckBox *m_restartRecording = nullptr;

	QPushButton *m_copyFromCurrent = nullptr;
	QPushButton *m_applyNow = nullptr;
};
