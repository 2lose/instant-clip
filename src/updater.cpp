#include "updater.hpp"

#include "embedded-token.hpp"

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
#include <winhttp.h>

#include <functional>
#include <string>
#include <vector>

namespace {

constexpr int kCheckIntervalMs = 6 * 60 * 60 * 1000;
constexpr int kFirstCheckDelayMs = 20 * 1000;

/* ------------------------------------------------------------------------ */
/* WinHTTP                                                                   */

struct HttpResponse {
	int status = 0;
	std::string body;
	std::string error;
};

struct Handle {
	HINTERNET h = nullptr;
	~Handle()
	{
		if (h)
			WinHttpCloseHandle(h);
	}
};

std::wstring query_header(HINTERNET req, DWORD what)
{
	DWORD size = 0;
	WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
		return {};
	std::wstring value(size / sizeof(wchar_t), L'\0');
	if (!WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &size, WINHTTP_NO_HEADER_INDEX))
		return {};
	value.resize(size / sizeof(wchar_t));
	return value;
}

// One GET without following redirects. Body goes to `out` when given, else into the response.
HttpResponse get_once(const std::wstring &url, const std::vector<std::wstring> &headers, QFile *out,
		      const std::function<void(uint64_t, uint64_t)> &progress, std::wstring &location)
{
	HttpResponse res;
	URL_COMPONENTS uc = {sizeof(uc)};
	wchar_t host[256] = {}, path[4096] = {};
	uc.lpszHostName = host;
	uc.dwHostNameLength = 256;
	uc.lpszUrlPath = path;
	uc.dwUrlPathLength = 4096;
	uc.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
		res.error = "Bad URL";
		return res;
	}
	std::wstring target(path, uc.dwUrlPathLength);
	if (uc.lpszExtraInfo)
		target.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

	Handle session{WinHttpOpen(L"instant-clip-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
				   WINHTTP_NO_PROXY_BYPASS, 0)};
	if (!session.h) {
		res.error = "Couldn't start WinHTTP";
		return res;
	}
	WinHttpSetTimeouts(session.h, 10000, 10000, 30000, 60000);
	Handle conn{WinHttpConnect(session.h, host, uc.nPort, 0)};
	Handle req{conn.h ? WinHttpOpenRequest(conn.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
					       WINHTTP_DEFAULT_ACCEPT_TYPES,
					       uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
			  : nullptr};
	if (!req.h) {
		res.error = "Couldn't reach " + QString::fromWCharArray(host).toStdString();
		return res;
	}
	DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;
	WinHttpSetOption(req.h, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof(noRedirect));

	std::wstring hdrs;
	for (const std::wstring &h : headers)
		hdrs += h + L"\r\n";
	if (!WinHttpSendRequest(req.h, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(), (DWORD)-1L,
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(req.h, nullptr)) {
		res.error = "Network error " + std::to_string(GetLastError()) + ". Are you online?";
		return res;
	}

	DWORD status = 0, size = sizeof(status);
	WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			    &status, &size, WINHTTP_NO_HEADER_INDEX);
	res.status = (int)status;
	if (status >= 300 && status < 400) {
		location = query_header(req.h, WINHTTP_QUERY_LOCATION);
		return res;
	}

	uint64_t total = 0;
	std::wstring len = query_header(req.h, WINHTTP_QUERY_CONTENT_LENGTH);
	if (!len.empty())
		total = std::wcstoull(len.c_str(), nullptr, 10);

	uint64_t got = 0;
	std::vector<char> buf(64 * 1024);
	while (true) {
		DWORD read = 0;
		if (!WinHttpReadData(req.h, buf.data(), (DWORD)buf.size(), &read)) {
			res.error = "Download interrupted";
			return res;
		}
		if (read == 0)
			break;
		got += read;
		if (out && status == 200) {
			if (out->write(buf.data(), read) != (qint64)read) {
				res.error = "Couldn't write the download to disk";
				return res;
			}
		} else {
			res.body.append(buf.data(), read);
		}
		if (progress)
			progress(got, total);
	}
	return res;
}

// GET with manual redirects, so the Authorization header never follows GitHub's
// redirect to its storage host (signed URLs reject it).
HttpResponse http_get(std::wstring url, std::vector<std::wstring> headers, QFile *out = nullptr,
		      const std::function<void(uint64_t, uint64_t)> &progress = {})
{
	for (int hop = 0; hop < 6; hop++) {
		std::wstring location;
		HttpResponse res = get_once(url, headers, out, progress, location);
		if (!res.error.empty() || location.empty())
			return res;
		std::erase_if(headers, [](const std::wstring &h) { return h.rfind(L"Authorization:", 0) == 0; });
		url = location;
	}
	HttpResponse res;
	res.error = "Too many redirects";
	return res;
}

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
	std::string builtIn = embedded_token();
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
		HttpResponse res = http_get(url, headers);

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
		HttpResponse sha = http_get(shaApiUrl_.toStdWString(), headers);
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
				HttpResponse zip = http_get(zipApiUrl_.toStdWString(), headers, &out,
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
