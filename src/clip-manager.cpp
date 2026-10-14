#include "clip-manager.hpp"

#include "background.hpp"
#include "sound.hpp"
#include "updater.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>
#include <util/dstr.h>
#include <util/platform.h>

#include <algorithm>
#include <cstring>

namespace {

ClipManager *g_instance = nullptr;

constexpr int kMaxLengths = 12;
constexpr int kMaxLengthSeconds = 3600;
constexpr int kMaxRecentStored = 30;
constexpr int kSaveTimeoutMs = 30000;

QString hotkeyName(int seconds)
{
	return QString("InstantClip.SaveLast.%1").arg(seconds);
}

QString hotkeyKey(int seconds)
{
	return QString("s%1").arg(seconds);
}

QString configPath()
{
	char *path = obs_module_config_path("settings.json");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

QString clock(int seconds)
{
	return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

} // namespace

ClipManager *ClipManager::get()
{
	return g_instance;
}

void ClipManager::create()
{
	if (!g_instance)
		g_instance = new ClipManager();
}

void ClipManager::destroy()
{
	if (!g_instance)
		return;
	g_instance->shutdown();
	delete g_instance;
	g_instance = nullptr;
}

ClipManager::ClipManager()
{
	bindingsSaveTimer_.setSingleShot(true);
	bindingsSaveTimer_.setInterval(250);
	connect(&bindingsSaveTimer_, &QTimer::timeout, this, [this] {
		saveHotkeyBindings();
		emit hotkeysChanged();
	});

	saveTimeout_.setSingleShot(true);
	connect(&saveTimeout_, &QTimer::timeout, this,
		[this] { failInflight("OBS didn't save the replay buffer. Is recording paused?"); });

	updater_ = new Updater(this);
	connect(updater_, &Updater::settingsChanged, this, [this] { save(); });

	load();

	signal_handler_connect(obs_get_signal_handler(), "hotkey_bindings_changed", bindingsChangedCallback, this);

	worker_ = std::thread([this] { workerLoop(); });
}

ClipManager::~ClipManager()
{
	shutdown();
	obs_data_release(hotkeyData_);
}

void ClipManager::shutdown()
{
	if (worker_.joinable()) {
		signal_handler_disconnect(obs_get_signal_handler(), "hotkey_bindings_changed", bindingsChangedCallback,
					  this);
		saveHotkeyBindings();
		save();
		for (auto &[seconds, id] : hotkeys_)
			obs_hotkey_unregister(id);
		hotkeys_.clear();

		{
			std::lock_guard lock(jobMutex_);
			quit_ = true;
		}
		jobCv_.notify_all();
		worker_.join(); // finishes clips already being trimmed
	}
}

/* ------------------------------------------------------------------------ */
/* Settings                                                                  */

void ClipManager::load()
{
	QString path = configPath();
	obs_data_t *data = obs_data_create_from_json_file_safe(path.toUtf8().constData(), "bak");

	lengths_.clear();
	if (data) {
		obs_data_array_t *arr = obs_data_get_array(data, "lengths");
		for (size_t i = 0, n = obs_data_array_count(arr); i < n; i++) {
			obs_data_t *item = obs_data_array_item(arr, i);
			int s = (int)obs_data_get_int(item, "seconds");
			if (s > 0 && s <= kMaxLengthSeconds && !lengths_.contains(s))
				lengths_.append(s);
			obs_data_release(item);
		}
		obs_data_array_release(arr);

		obs_data_set_default_string(data, "format", "mp4");
		obs_data_set_default_string(data, "trim_mode", "fast");
		obs_data_set_default_bool(data, "delete_original", true);
		obs_data_set_default_bool(data, "sound_on_save", false);

		format_ = QString::fromUtf8(obs_data_get_string(data, "format"));
		if (format_ != "mp4" && format_ != "mkv" && format_ != "mov")
			format_ = "mp4";
		trimMode_ = strcmp(obs_data_get_string(data, "trim_mode"), "precise") == 0 ? trimmer::Mode::Precise
											   : trimmer::Mode::Fast;
		deleteOriginal_ = obs_data_get_bool(data, "delete_original");
		soundOnSave_ = obs_data_get_bool(data, "sound_on_save");
		outputDir_ = QString::fromUtf8(obs_data_get_string(data, "output_dir"));

		obs_data_array_t *recent = obs_data_get_array(data, "recent");
		for (size_t i = 0, n = obs_data_array_count(recent); i < n; i++) {
			obs_data_t *item = obs_data_array_item(recent, i);
			RecentClip clip;
			clip.path = QString::fromUtf8(obs_data_get_string(item, "path"));
			clip.seconds = (int)obs_data_get_int(item, "seconds");
			clip.savedAt = QDateTime::fromMSecsSinceEpoch(obs_data_get_int(item, "saved_at"));
			if (!clip.path.isEmpty())
				recent_.append(clip);
			obs_data_release(item);
		}
		obs_data_array_release(recent);

		hotkeyData_ = obs_data_get_obj(data, "hotkeys");
	}
	updater_->load(data);
	obs_data_release(data);

	if (!data)
		lengths_ = {15, 30, 60, 120, 300};
	if (!hotkeyData_)
		hotkeyData_ = obs_data_create();

	for (int s : lengths_)
		registerHotkey(s);
}

void ClipManager::save()
{
	obs_data_t *data = obs_data_create();

	obs_data_array_t *arr = obs_data_array_create();
	for (int s : lengths_) {
		obs_data_t *item = obs_data_create();
		obs_data_set_int(item, "seconds", s);
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "lengths", arr);
	obs_data_array_release(arr);

	obs_data_set_string(data, "format", format_.toUtf8().constData());
	obs_data_set_string(data, "trim_mode", trimMode_ == trimmer::Mode::Precise ? "precise" : "fast");
	obs_data_set_bool(data, "delete_original", deleteOriginal_);
	obs_data_set_bool(data, "sound_on_save", soundOnSave_);
	obs_data_set_string(data, "output_dir", outputDir_.toUtf8().constData());

	obs_data_array_t *recent = obs_data_array_create();
	for (const RecentClip &clip : recent_) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "path", clip.path.toUtf8().constData());
		obs_data_set_int(item, "seconds", clip.seconds);
		obs_data_set_int(item, "saved_at", clip.savedAt.toMSecsSinceEpoch());
		obs_data_array_push_back(recent, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "recent", recent);
	obs_data_array_release(recent);

	obs_data_set_obj(data, "hotkeys", hotkeyData_);
	updater_->save(data);

	QString path = configPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	obs_data_save_json_safe(data, path.toUtf8().constData(), "tmp", "bak");
	obs_data_release(data);
}

bool ClipManager::addLength(int seconds)
{
	if (seconds <= 0 || seconds > kMaxLengthSeconds || lengths_.contains(seconds) ||
	    lengths_.size() >= kMaxLengths)
		return false;
	lengths_.append(seconds);
	std::sort(lengths_.begin(), lengths_.end());
	registerHotkey(seconds);
	save();
	emit lengthsChanged();
	return true;
}

void ClipManager::removeLength(int seconds)
{
	if (!lengths_.removeOne(seconds))
		return;
	saveHotkeyBindings(); // keep its bindings in case the length comes back
	unregisterHotkey(seconds);
	save();
	emit lengthsChanged();
}

void ClipManager::setFormat(const QString &format)
{
	if (format == format_)
		return;
	format_ = format;
	save();
	emit settingsChanged();
}

void ClipManager::setTrimMode(trimmer::Mode mode)
{
	if (mode == trimMode_)
		return;
	trimMode_ = mode;
	save();
	emit settingsChanged();
}

void ClipManager::setDeleteOriginal(bool on)
{
	deleteOriginal_ = on;
	save();
	emit settingsChanged();
}

void ClipManager::setSoundOnSave(bool on)
{
	soundOnSave_ = on;
	save();
	emit settingsChanged();
	if (on)
		play_save_sound();
}

void ClipManager::setOutputDir(const QString &dir)
{
	outputDir_ = dir;
	save();
	emit settingsChanged();
}

QString ClipManager::outputDir() const
{
	if (!outputDir_.isEmpty())
		return outputDir_;
	char *path = obs_frontend_get_current_record_output_path();
	QString dir = QString::fromUtf8(path);
	bfree(path);
	if (dir.isEmpty())
		dir = QDir::homePath() + "/Videos";
	return QDir::cleanPath(dir);
}

bool ClipManager::obsAutoRemux() const
{
	config_t *cfg = obs_frontend_get_profile_config();
	return cfg && config_get_bool(cfg, "Video", "AutoRemux");
}

QString ClipManager::lengthLabel(int seconds)
{
	if (seconds < 60)
		return QString("%1s").arg(seconds);
	if (seconds % 60 == 0)
		return QString("%1m").arg(seconds / 60);
	return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
}

/* ------------------------------------------------------------------------ */
/* Hotkeys                                                                   */

void ClipManager::hotkeyCallback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	int seconds = (int)(intptr_t)data;
	if (ClipManager *m = get())
		QMetaObject::invokeMethod(m, [m, seconds] { m->requestClip(seconds); }, Qt::QueuedConnection);
}

void ClipManager::bindingsChangedCallback(void *data, calldata_t *)
{
	auto *m = static_cast<ClipManager *>(data);
	QMetaObject::invokeMethod(m, [m] { m->bindingsSaveTimer_.start(); }, Qt::QueuedConnection);
}

void ClipManager::registerHotkey(int seconds)
{
	if (hotkeys_.count(seconds))
		return;
	QString desc = QString("Clips: Save last %1").arg(lengthLabel(seconds));
	obs_hotkey_id id = obs_hotkey_register_frontend(hotkeyName(seconds).toUtf8().constData(),
							desc.toUtf8().constData(), hotkeyCallback,
							(void *)(intptr_t)seconds);
	hotkeys_[seconds] = id;

	obs_data_array_t *bindings = obs_data_get_array(hotkeyData_, hotkeyKey(seconds).toUtf8().constData());
	if (bindings) {
		obs_hotkey_load(id, bindings);
		obs_data_array_release(bindings);
	}
}

void ClipManager::unregisterHotkey(int seconds)
{
	auto it = hotkeys_.find(seconds);
	if (it == hotkeys_.end())
		return;
	obs_hotkey_unregister(it->second);
	hotkeys_.erase(it);
	emit hotkeysChanged();
}

void ClipManager::saveHotkeyBindings()
{
	bool changed = false;
	for (auto &[seconds, id] : hotkeys_) {
		obs_data_array_t *arr = obs_hotkey_save(id);
		obs_data_set_array(hotkeyData_, hotkeyKey(seconds).toUtf8().constData(), arr);
		obs_data_array_release(arr);
		changed = true;
	}
	if (changed)
		save();
}

QString ClipManager::hotkeyText(int seconds) const
{
	auto it = hotkeys_.find(seconds);
	if (it == hotkeys_.end())
		return {};

	struct Ctx {
		obs_hotkey_id id;
		QStringList combos;
	} ctx{it->second, {}};

	obs_enum_hotkey_bindings(
		[](void *data, size_t, obs_hotkey_binding_t *binding) {
			auto *c = static_cast<Ctx *>(data);
			if (obs_hotkey_binding_get_hotkey_id(binding) != c->id)
				return true;
			struct dstr str = {};
			obs_key_combination_to_str(obs_hotkey_binding_get_key_combination(binding), &str);
			if (str.array && *str.array)
				c->combos << QString::fromUtf8(str.array).replace(" + ", "+");
			dstr_free(&str);
			return true;
		},
		&ctx);

	return ctx.combos.isEmpty() ? QString() : ctx.combos.first();
}

/* ------------------------------------------------------------------------ */
/* Replay buffer                                                             */

bool ClipManager::bufferActive() const
{
	return obs_frontend_replay_buffer_active();
}

static const char *outputSection(config_t *cfg)
{
	const char *mode = config_get_string(cfg, "Output", "Mode");
	return mode && strcmp(mode, "Advanced") == 0 ? "AdvOut" : "SimpleOutput";
}

bool ClipManager::bufferEnabled() const
{
	config_t *cfg = obs_frontend_get_profile_config();
	return !cfg || config_get_bool(cfg, outputSection(cfg), "RecRB");
}

int ClipManager::bufferMaxSeconds() const
{
	if (config_t *cfg = obs_frontend_get_profile_config()) {
		int64_t t = config_get_int(cfg, outputSection(cfg), "RecRBTime");
		if (t > 0)
			return (int)t;
	}
	int seconds = 0;
	if (obs_output_t *out = obs_frontend_get_replay_buffer_output()) {
		obs_data_t *s = obs_output_get_settings(out);
		seconds = (int)obs_data_get_int(s, "max_time_sec");
		obs_data_release(s);
		obs_output_release(out);
	}
	return seconds;
}

void ClipManager::startBuffer()
{
	obs_frontend_replay_buffer_start();
}

void ClipManager::stopBuffer()
{
	obs_frontend_replay_buffer_stop();
}

void ClipManager::onFrontendEvent(int event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
		onReplaySaved();
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		if (inflight_ || !queue_.empty())
			failInflight("The replay buffer stopped before it was saved.");
		emit bufferChanged();
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		emit bufferChanged();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		emit bufferChanged();
		emit settingsChanged();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		updater_->onExit();
		shutdown();
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* Request queue                                                             */

void ClipManager::requestClip(int seconds)
{
	if (!bufferActive()) {
		emit bufferOffNotice(seconds);
		return;
	}
	PendingRequest r;
	r.seconds = seconds;
	r.pressedNs = os_gettime_ns();
	r.pressedAt = QDateTime::currentDateTime();
	queue_.push_back(r);
	emit clipQueued(seconds);
	pump();
}

void ClipManager::pump()
{
	if (inflight_ || queue_.empty())
		return;
	inflight_ = queue_.front();
	queue_.pop_front();
	inflightIssuedNs_ = os_gettime_ns();
	saveTimeout_.start(kSaveTimeoutMs);
	obs_frontend_replay_buffer_save();
}

void ClipManager::failInflight(const QString &error)
{
	saveTimeout_.stop();
	if (inflight_)
		emit clipFailed(inflight_->seconds, error);
	inflight_.reset();
	// Anything still queued belonged to the same buffer session; it can't be saved either
	// if the buffer stopped, but on a timeout the next one deserves a fresh try.
	if (!bufferActive()) {
		for (const PendingRequest &r : queue_)
			emit clipFailed(r.seconds, error);
		queue_.clear();
	}
	pump();
}

void ClipManager::onReplaySaved()
{
	if (!inflight_)
		return; // saved by OBS's own hotkey or button, not ours

	char *last = obs_frontend_get_last_replay();
	QString source = QString::fromUtf8(last);
	bfree(last);

	PendingRequest r = *inflight_;
	inflight_.reset();
	saveTimeout_.stop();

	if (source.isEmpty() || !QFileInfo::exists(source)) {
		emit clipFailed(r.seconds, "OBS saved the replay, but the file couldn't be found.");
		pump();
		return;
	}

	QString outDir = outputDir();
	if (!QDir().mkpath(outDir)) {
		emit clipFailed(r.seconds, QString("Couldn't create %1").arg(QDir::toNativeSeparators(outDir)));
		pump();
		return;
	}

	Job job;
	job.seconds = r.seconds;
	job.source = source;
	job.output = uniqueClipPath(r);
	job.req.input = source.toUtf8().toStdString();
	job.req.output = job.output.toUtf8().toStdString();
	job.req.format = format_.toStdString();
	job.req.length = r.seconds;
	// The saved file runs up to when we asked OBS to save, which can be later than the
	// key press if an earlier save was still in progress. Trim that extra tail off.
	job.req.endOffset = inflightIssuedNs_ > r.pressedNs ? (inflightIssuedNs_ - r.pressedNs) / 1e9 : 0.0;
	job.req.mode = trimMode_;
	int seconds = r.seconds;
	job.req.progress = [this, seconds](double p) {
		QMetaObject::invokeMethod(this, [this, seconds, p] { emit clipProgress(seconds, p); },
					  Qt::QueuedConnection);
	};

	{
		std::lock_guard lock(jobMutex_);
		jobs_.push_back(std::move(job));
	}
	jobCv_.notify_one();
	emit clipProgress(seconds, 0.0);

	pump();
}

QString ClipManager::uniqueClipPath(const PendingRequest &r) const
{
	QString base = QString("Clip %1 (%2)").arg(r.pressedAt.toString("yyyy-MM-dd HH-mm-ss"),
						  lengthLabel(r.seconds).remove(' '));
	QDir dir(outputDir());
	QString path = dir.filePath(base + "." + format_);
	for (int n = 2; QFileInfo::exists(path) || QFileInfo::exists(path + ".part"); n++)
		path = dir.filePath(QString("%1 %2.%3").arg(base).arg(n).arg(format_));
	return path;
}

/* ------------------------------------------------------------------------ */
/* Worker                                                                    */

void ClipManager::workerLoop()
{
	enter_background_mode();
	while (true) {
		Job job;
		{
			std::unique_lock lock(jobMutex_);
			jobCv_.wait(lock, [this] { return quit_ || !jobs_.empty(); });
			if (jobs_.empty())
				return; // quit with nothing left to do
			job = std::move(jobs_.front());
			jobs_.pop_front();
		}

		trimmer::Result res = trimmer::trim(job.req);
		job.req.progress = nullptr;
		QMetaObject::invokeMethod(this, [this, job, res] { onJobDone(job, res); }, Qt::QueuedConnection);
	}
}

void ClipManager::onJobDone(const Job &job, const trimmer::Result &res)
{
	if (!res.ok) {
		blog(LOG_WARNING, "[instant-clip] trimming %s failed: %s", job.req.input.c_str(), res.error.c_str());
		emit clipFailed(job.seconds, QString::fromStdString(res.error));
		return;
	}

	blog(LOG_INFO, "[instant-clip] saved %.1fs clip (asked %ds, replay held %.1fs%s%s) -> %s", res.duration,
	     job.seconds, res.available, res.encoder.empty() ? "" : ", encoder ", res.encoder.c_str(),
	     job.req.output.c_str());

	RecentClip clip;
	clip.path = job.output;
	clip.seconds = job.seconds;
	clip.savedAt = QDateTime::currentDateTime();
	recent_.prepend(clip);
	while (recent_.size() > kMaxRecentStored)
		recent_.removeLast();
	save();

	QString note;
	if (res.bufferShort)
		note = QString("Buffer only held %1").arg(clock((int)(res.available + 0.5)));
	else if (res.fellBack)
		note = "Cut on keyframes, precise wasn't possible";

	emit recentChanged();
	emit clipSaved(job.seconds, job.output, note);
	if (soundOnSave_)
		play_save_sound();

	if (deleteOriginal_ && QFileInfo(job.source) != QFileInfo(job.output)) {
		deleteWhenFree(job.source, 40, false);
		// OBS's auto-remux makes a full-length copy next to MKV/FLV replays. It's the same
		// footage as the original we're discarding, so clean it up too once OBS is done with it.
		QFileInfo src(job.source);
		QString ext = src.suffix().toLower();
		if (obsAutoRemux() && ext != "mp4" && ext != "mov") {
			deleteWhenFree(src.dir().filePath(src.completeBaseName() + ".mp4"), 60, true);
			deleteWhenFree(src.dir().filePath(src.completeBaseName() + ".mov"), 60, true);
		}
	}
}

// Deletes a file, retrying while another process (OBS's remuxer) still has it open.
// With onlyIfAppears, waits for the file to show up first.
void ClipManager::deleteWhenFree(const QString &path, int attemptsLeft, bool onlyIfAppears)
{
	bool exists = QFileInfo::exists(path);
	if (exists && QFile::remove(path)) {
		blog(LOG_INFO, "[instant-clip] deleted full-length replay %s", path.toUtf8().constData());
		return;
	}
	if ((!exists && !onlyIfAppears) || attemptsLeft <= 0)
		return;
	QTimer::singleShot(2000, this, [this, path, attemptsLeft, onlyIfAppears] {
		deleteWhenFree(path, attemptsLeft - 1, onlyIfAppears);
	});
}

QList<RecentClip> ClipManager::recentClips(int max) const
{
	QList<RecentClip> list;
	for (const RecentClip &clip : recent_) {
		if (list.size() >= max)
			break;
		if (QFileInfo::exists(clip.path))
			list.append(clip);
	}
	return list;
}
