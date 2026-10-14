#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <mutex>
#include <thread>

// Checks the public GitHub repo's latest release, downloads the plugin zip on request,
// and installs it after OBS exits (the plugin DLL is locked while OBS runs).
class Updater : public QObject {
	Q_OBJECT

public:
	enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Error };

	explicit Updater(QObject *parent = nullptr);
	~Updater() override;

	static QString currentVersion();
	static QString repo(); // "owner/name"

	State state() const { return state_; }
	QString latestVersion() const { return latest_; }
	QString notes() const { return notes_; }
	QString error() const { return error_; }
	double progress() const { return progress_; }
	QString justUpdatedFrom() const { return updatedFrom_; } // non-empty right after an update

	void check(bool manual);
	void download();
	void onExit(); // launches the installer if an update is staged

	void load(void *data);  // obs_data_t* from the plugin settings
	void save(void *data) const;

signals:
	void stateChanged();
	void settingsChanged();

private:
	void setState(State s);
	void run(std::function<void()> fn);
	QString stagingDir() const;
	QString pluginRoot() const;

	State state_ = State::Idle;
	QString latest_;
	QString notes_;
	QString error_;
	double progress_ = 0.0;
	QString zipApiUrl_;
	QString shaApiUrl_;
	QString zipName_;
	QString stagedZip_;
	QString updatedFrom_;
	QString lastVersionSeen_;
	bool manualCheck_ = false;

	QTimer timer_;
	std::thread worker_;
	std::atomic<bool> busy_ = false;
};
