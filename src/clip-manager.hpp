#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <obs.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

#include "trimmer.hpp"

class Updater;

struct RecentClip {
	QString path;
	int seconds = 0;
	QDateTime savedAt;
};

// Owns everything that isn't UI: settings, hotkeys, the save-request queue
// and the trim worker. Lives on the Qt UI thread.
class ClipManager : public QObject {
	Q_OBJECT

public:
	static ClipManager *get();
	static void create();
	static void destroy();

	// Replay buffer
	bool bufferActive() const;
	bool bufferEnabled() const;  // replay buffer turned on in OBS's output settings
	int bufferMaxSeconds() const; // max replay time from the active profile
	void startBuffer();
	void stopBuffer();

	// Clips
	void requestClip(int seconds);
	QList<RecentClip> recentClips(int max = 5) const;

	// Settings
	QList<int> lengths() const { return lengths_; }
	bool addLength(int seconds);
	void removeLength(int seconds);
	QString format() const { return format_; }
	void setFormat(const QString &format);
	trimmer::Mode trimMode() const { return trimMode_; }
	void setTrimMode(trimmer::Mode mode);
	bool deleteOriginal() const { return deleteOriginal_; }
	void setDeleteOriginal(bool on);
	bool soundOnSave() const { return soundOnSave_; }
	void setSoundOnSave(bool on);
	QString outputDirSetting() const { return outputDir_; }
	QString outputDir() const; // setting, or OBS's recording path when unset
	void setOutputDir(const QString &dir);
	bool obsAutoRemux() const;

	QString hotkeyText(int seconds) const; // "" when not bound

	Updater *updater() const { return updater_; }

	void onFrontendEvent(int event);
	void shutdown();

	static QString lengthLabel(int seconds); // 15s, 1m, 1m 30s

signals:
	void bufferChanged();
	void lengthsChanged();
	void hotkeysChanged();
	void settingsChanged();
	void recentChanged();
	void clipQueued(int seconds);
	void clipProgress(int seconds, double progress);
	void clipSaved(int seconds, const QString &path, const QString &note);
	void clipFailed(int seconds, const QString &error);
	void bufferOffNotice(int seconds);

private:
	ClipManager();
	~ClipManager() override;

	struct PendingRequest {
		int seconds = 0;
		uint64_t pressedNs = 0;
		QDateTime pressedAt;
	};

	struct Job {
		int seconds = 0;
		QString source;
		QString output;
		trimmer::Request req;
	};

	void load();
	void save();
	void registerHotkey(int seconds);
	void unregisterHotkey(int seconds);
	void saveHotkeyBindings();

	void pump();
	void onReplaySaved();
	void failInflight(const QString &error);
	QString uniqueClipPath(const PendingRequest &r) const;

	void workerLoop();
	void onJobDone(const Job &job, const trimmer::Result &res);
	void deleteWhenFree(const QString &path, int attemptsLeft, bool onlyIfAppears);

	static void hotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	static void bindingsChangedCallback(void *data, calldata_t *cd);

	// Settings
	QList<int> lengths_;
	QString format_ = "mp4";
	trimmer::Mode trimMode_ = trimmer::Mode::Fast;
	bool deleteOriginal_ = true;
	bool soundOnSave_ = false;
	QString outputDir_;
	QList<RecentClip> recent_;
	Updater *updater_ = nullptr;

	// Hotkeys: seconds -> id, plus saved bindings for every length ever configured.
	std::map<int, obs_hotkey_id> hotkeys_;
	obs_data_t *hotkeyData_ = nullptr;
	QTimer bindingsSaveTimer_;

	// Save requests are sent to OBS one at a time: OBS merges overlapping saves
	// into a single file, which would leave a request without its replay.
	std::deque<PendingRequest> queue_;
	std::optional<PendingRequest> inflight_;
	uint64_t inflightIssuedNs_ = 0;
	QTimer saveTimeout_;

	std::thread worker_;
	std::mutex jobMutex_;
	std::condition_variable jobCv_;
	std::deque<Job> jobs_;
	bool quit_ = false;
};
