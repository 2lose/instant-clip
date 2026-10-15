#include "updater.hpp"

#include "background.hpp"

#include "embedded-token.hpp"
#include "http.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#include <obs-module.h>

#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>

#include <functional>
#include <string>
#include <vector>

namespace {

constexpr int kCheckIntervalMs = 6 * 60 * 60 * 1000;
constexpr int kFirstCheckDelayMs = 20 * 1000;


/* ------------------------------------------------------------------------ */

QList<int> parse_version(QString v)
{
	if (v.startsWith('v') || v.startsWith('V'))
		v.remove(0, 1);
	QList<int> parts;
	for (const QString &p : v.split('.'))
		parts << p.toInt();
	while (parts.size() < 3)
		parts << 0;
	return parts;
}

bool newer(const QString &a, const QString &b)
{
	QList<int> x = parse_version(a), y = parse_version(b);
	for (int i = 0; i < 3; i++) {
		if (x[i] != y[i])
			return x[i] > y[i];
	}
	return false;
}

QByteArray dpapi(const QByteArray &in, bool protect)
{
	DATA_BLOB src{(DWORD)in.size(), (BYTE *)in.data()}, dst{};
	BOOL ok = protect ? CryptProtectData(&src, L"instant-clip", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &dst)
			  : CryptUnprotectData(&src, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &dst);
	if (!ok)
		return {};
	QByteArray result((const char *)dst.pbData, (int)dst.cbData);
	LocalFree(dst.pbData);
	return result;
}

std::string gh_cli_token()
{
	for (const QString &program : {QStringLiteral("gh"), QStringLiteral("C:/Program Files/GitHub CLI/gh.exe")}) {
		QProcess p;
		p.setCreateProcessArgumentsModifier(
			[](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
		p.start(program, {"auth", "token", "--hostname", "github.com"});
		if (!p.waitForFinished(8000) || p.exitCode() != 0)
			continue;
		QByteArray token = p.readAllStandardOutput().trimmed();
		if (!token.isEmpty())
			return token.toStdString();
	}
	return {};
}

// GitHub answers 401 to a revoked or expired token even on public repos. Retry without it
// so a dead token never blocks updates from a public repo.
HttpResponse github_get(const std::wstring &url, std::vector<std::wstring> headers, QFile *out = nullptr,
			const std::function<void(uint64_t, uint64_t)> &progress = {})
{
	HttpResponse res = http_get(url, headers, out, progress);
	bool hadAuth =
		std::erase_if(headers, [](const std::wstring &h) { return h.rfind(L"Authorization:", 0) == 0; }) != 0;
	if (res.error.empty() && res.status == 401 && hadAuth) {
		blog(LOG_INFO, "[instant-clip] update token rejected, retrying without it");
		res = http_get(url, headers, out, progress);
	}
	return res;
}

QString obs_string(void *data, const char *key)
{
	return QString::fromUtf8(obs_data_get_string(static_cast<obs_data_t *>(data), key));
}

const char *kApplyScript = R"PS(param([int]$ObsPid, [string]$Zip, [string]$Dest, [string]$Log)
$ErrorActionPreference = 'Stop'
try {
    try { Wait-Process -Id $ObsPid -Timeout 180 -ErrorAction Stop } catch {}
    Start-Sleep -Milliseconds 500
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('instant-clip-update-' + [guid]::NewGuid())
    Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force
    $src = Join-Path $tmp 'instant-clip'
    if (-not (Test-Path (Join-Path $src 'bin\64bit\instant-clip.dll'))) { throw 'The update zip has an unexpected layout.' }
    for ($i = 0; $i -lt 30; $i++) {
        try { Copy-Item -Path (Join-Path $src '*') -Destination $Dest -Recurse -Force; break }
        catch { if ($i -eq 29) { throw }; Start-Sleep -Seconds 1 }
    }
    Remove-Item -LiteralPath $tmp -Recurse -Force
    Remove-Item -LiteralPath $Zip -Force
    "ok $(Get-Date -Format o)" | Out-File -LiteralPath $Log -Encoding utf8
} catch {
    "error $_" | Out-File -LiteralPath $Log -Encoding utf8
}
)PS";

} // namespace

/* ------------------------------------------------------------------------ */

Updater::Updater(QObject *parent) : QObject(parent)
{
	timer_.setInterval(kCheckIntervalMs);
	connect(&timer_, &QTimer::timeout, this, [this] { check(false); });
	timer_.start();
	QTimer::singleShot(kFirstCheckDelayMs, this, [this] { check(false); });
}

Updater::~Updater()
{
	if (worker_.joinable())
		worker_.join();
}

QString Updater::currentVersion()
{
	return QStringLiteral(PLUGIN_VERSION);
}

QString Updater::repo()
{
	return QStringLiteral(UPDATE_REPO);
}

void Updater::load(void *data)
{
	if (data) {
		encryptedToken_ = QByteArray::fromBase64(obs_string(data, "update_token").toLatin1());
		lastVersionSeen_ = obs_string(data, "last_version");
		QString stagedVersion = obs_string(data, "staged_version");
		QString stagedZip = obs_string(data, "staged_zip");
		if (!stagedVersion.isEmpty() && newer(stagedVersion, currentVersion()) && QFileInfo::exists(stagedZip)) {
			latest_ = stagedVersion;
			stagedZip_ = stagedZip;
			state_ = State::Ready;
		}
	}

	if (!lastVersionSeen_.isEmpty() && lastVersionSeen_ != currentVersion())
		updatedFrom_ = lastVersionSeen_;

	// Result of the installer that ran after the last exit.
	QFile log(QDir(stagingDir()).filePath("update.log"));
	if (log.open(QIODevice::ReadOnly)) {
		QString text = QString::fromUtf8(log.readAll()).trimmed();
		log.close();
		log.remove();
		if (text.startsWith("error")) {
			error_ = "The last update didn't install: " + text.mid(6);
			state_ = State::Error;
			blog(LOG_WARNING, "[instant-clip] update install failed: %s", text.toUtf8().constData());
		}
	}
}

void Updater::save(void *data) const
{
	auto *d = static_cast<obs_data_t *>(data);
	obs_data_set_string(d, "update_token", encryptedToken_.toBase64().constData());
	obs_data_set_string(d, "last_version", currentVersion().toUtf8().constData());
	bool staged = state_ == State::Ready && !stagedZip_.isEmpty();
	obs_data_set_string(d, "staged_version", staged ? latest_.toUtf8().constData() : "");
	obs_data_set_string(d, "staged_zip", staged ? stagedZip_.toUtf8().constData() : "");
}

bool Updater::hasStoredToken() const
{
	return !encryptedToken_.isEmpty();
}

void Updater::setToken(const QString &token)
{
	encryptedToken_ = token.trimmed().isEmpty() ? QByteArray() : dpapi(token.trimmed().toUtf8(), true);
	emit settingsChanged();
}

std::string Updater::token() const
{
	if (!encryptedToken_.isEmpty()) {
		QByteArray plain = dpapi(encryptedToken_, false);
		if (!plain.isEmpty())
			return plain.toStdString();
	}
	std::string builtIn = embedded_secret(Secret::UpdateToken);
	if (!builtIn.empty())
		return builtIn;
	return gh_cli_token();
}

QString Updater::stagingDir() const
{
	char *path = obs_module_config_path("update");
	QString dir = QString::fromUtf8(path);
	bfree(path);
	QDir().mkpath(dir);
	return dir;
}

QString Updater::pluginRoot() const
{
	// .../instant-clip/bin/64bit/instant-clip.dll -> .../instant-clip
	QFileInfo dll(QString::fromUtf8(obs_get_module_binary_path(obs_current_module())));
	QDir dir = dll.absoluteDir();
	if (dir.dirName() != "64bit" || !dir.cdUp() || dir.dirName() != "bin" || !dir.cdUp())
		return {};
	return dir.absolutePath();
}

void Updater::setState(State s)
{
	state_ = s;
	emit stateChanged();
}

void Updater::run(std::function<void()> fn)
{
	if (busy_.exchange(true))
		return;
	if (worker_.joinable())
		worker_.join();
	worker_ = std::thread([this, fn = std::move(fn)] {
		enter_background_mode();
		fn();
		busy_ = false;
	});
}

void Updater::check(bool manual)
{
	if (busy_ || state_ == State::Downloading || state_ == State::Ready)
		return;
	manualCheck_ = manual;
	if (manual)
		setState(State::Checking);

	run([this] {
		std::string tok = token();
		std::vector<std::wstring> headers = {L"Accept: application/vnd.github+json",
						     L"X-GitHub-Api-Version: 2022-11-28",
						     (L"User-Agent: instant-clip/" + currentVersion().toStdWString())};
		if (!tok.empty())
			headers.push_back(L"Authorization: Bearer " + QString::fromStdString(tok).toStdWString());

		std::wstring url = L"https://api.github.com/repos/" + repo().toStdWString() + L"/releases/latest";
		HttpResponse res = github_get(url, headers);

		QString err, version, notes, zipUrl, shaUrl, zipName;
		if (!res.error.empty()) {
			err = QString::fromStdString(res.error);
		} else if (res.status == 404 || res.status == 401 || res.status == 403) {
			err = tok.empty() ? "This build has no update access. Rebuild with secrets\\update-token.txt or add a token "
					    "in Settings > Updates."
					  : QString("GitHub refused access to %1 (HTTP %2). Check the token.")
						    .arg(repo())
						    .arg(res.status);
		} else if (res.status != 200) {
			err = QString("GitHub returned HTTP %1").arg(res.status);
		} else {
			obs_data_t *json = obs_data_create_from_json(res.body.c_str());
			version = QString::fromUtf8(obs_data_get_string(json, "tag_name"));
			if (version.startsWith('v'))
				version.remove(0, 1);
			notes = QString::fromUtf8(obs_data_get_string(json, "body"));
			obs_data_array_t *assets = obs_data_get_array(json, "assets");
			for (size_t i = 0, n = obs_data_array_count(assets); i < n; i++) {
				obs_data_t *a = obs_data_array_item(assets, i);
				QString name = QString::fromUtf8(obs_data_get_string(a, "name"));
				QString apiUrl = QString::fromUtf8(obs_data_get_string(a, "url"));
				if (name.endsWith("-windows-x64.zip")) {
					zipUrl = apiUrl;
					zipName = name;
				} else if (name.endsWith("-windows-x64.zip.sha256")) {
					shaUrl = apiUrl;
				}
				obs_data_release(a);
			}
			obs_data_array_release(assets);
			obs_data_release(json);
			if (version.isEmpty())
				err = "The latest release has no version tag.";
			else if (newer(version, currentVersion()) && (zipUrl.isEmpty() || shaUrl.isEmpty()))
				err = QString("Release %1 is missing its zip or .sha256 file.").arg(version);
		}

		QMetaObject::invokeMethod(
			this,
			[this, err, version, notes, zipUrl, shaUrl, zipName] {
				if (!err.isEmpty()) {
					blog(LOG_INFO, "[instant-clip] update check failed: %s", err.toUtf8().constData());
					error_ = err;
					// Background checks fail quietly; only a manual check shows the error.
					setState(manualCheck_ ? State::Error : State::Idle);
					return;
				}
				latest_ = version;
				notes_ = notes;
				if (newer(version, currentVersion())) {
					zipApiUrl_ = zipUrl;
					shaApiUrl_ = shaUrl;
					zipName_ = zipName;
					blog(LOG_INFO, "[instant-clip] update %s available", version.toUtf8().constData());
					setState(State::Available);
				} else {
					setState(manualCheck_ ? State::UpToDate : State::Idle);
				}
			},
			Qt::QueuedConnection);
	});
}

void Updater::download()
{
	if (state_ != State::Available || busy_)
		return;
	if (pluginRoot().isEmpty()) {
		error_ = "Can't find the plugin's install folder.";
		setState(State::Error);
		return;
	}
	progress_ = 0.0;
	setState(State::Downloading);

	QString target = QDir(stagingDir()).filePath(zipName_);
	run([this, target] {
		std::string tok = token();
		std::vector<std::wstring> headers = {L"Accept: application/octet-stream",
						     (L"User-Agent: instant-clip/" + currentVersion().toStdWString())};
		if (!tok.empty())
			headers.push_back(L"Authorization: Bearer " + QString::fromStdString(tok).toStdWString());

		QString err;
		QByteArray expected;
		HttpResponse sha = github_get(shaApiUrl_.toStdWString(), headers);
		if (!sha.error.empty() || sha.status != 200)
			err = "Couldn't download the checksum" +
			      (sha.error.empty() ? QString(" (HTTP %1)").arg(sha.status) : ": " + QString::fromStdString(sha.error));
		else
			expected = QByteArray::fromStdString(sha.body).trimmed().split(' ').first().toLower();

		if (err.isEmpty()) {
			QFile out(target + ".part");
			if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
				err = "Couldn't write to " + QDir::toNativeSeparators(out.fileName());
			} else {
				double last = 0;
				HttpResponse zip = github_get(zipApiUrl_.toStdWString(), headers, &out,
							    [this, &last](uint64_t got, uint64_t total) {
								    if (!total)
									    return;
								    double p = (double)got / total;
								    if (p - last < 0.02 && p < 1.0)
									    return;
								    last = p;
								    QMetaObject::invokeMethod(
									    this,
									    [this, p] {
										    progress_ = p;
										    emit stateChanged();
									    },
									    Qt::QueuedConnection);
							    });
				out.close();
				if (!zip.error.empty() || zip.status != 200)
					err = "Download failed" + (zip.error.empty() ? QString(" (HTTP %1)").arg(zip.status)
										    : ": " + QString::fromStdString(zip.error));
			}
		}

		if (err.isEmpty()) {
			QFile f(target + ".part");
			QCryptographicHash hash(QCryptographicHash::Sha256);
			if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f))
				err = "Couldn't read the download.";
			f.close();
			if (err.isEmpty() && hash.result().toHex() != expected)
				err = "The download didn't match its checksum.";
		}

		if (err.isEmpty()) {
			QFile::remove(target);
			if (!QFile::rename(target + ".part", target))
				err = "Couldn't save the download.";
		} else {
			QFile::remove(target + ".part");
		}

		QMetaObject::invokeMethod(
			this,
			[this, err, target] {
				if (!err.isEmpty()) {
					error_ = err;
					setState(State::Error);
					return;
				}
				stagedZip_ = target;
				blog(LOG_INFO, "[instant-clip] update %s downloaded, installs on exit",
				     latest_.toUtf8().constData());
				setState(State::Ready);
				emit settingsChanged();
			},
			Qt::QueuedConnection);
	});
}

void Updater::onExit()
{
	if (state_ != State::Ready || stagedZip_.isEmpty() || !QFileInfo::exists(stagedZip_))
		return;
	QString root = pluginRoot();
	if (root.isEmpty())
		return;

	QString dir = stagingDir();
	QString script = QDir(dir).filePath("apply-update.ps1");
	QFile f(script);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return;
	f.write(kApplyScript);
	f.close();

	QProcess p;
	p.setProgram("powershell.exe");
	p.setArguments({"-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File",
			QDir::toNativeSeparators(script), "-ObsPid", QString::number(QCoreApplication::applicationPid()),
			"-Zip", QDir::toNativeSeparators(stagedZip_), "-Dest", QDir::toNativeSeparators(root), "-Log",
			QDir::toNativeSeparators(QDir(dir).filePath("update.log"))});
	p.setCreateProcessArgumentsModifier(
		[](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
	if (p.startDetached())
		blog(LOG_INFO, "[instant-clip] installing update %s after exit", latest_.toUtf8().constData());
}
