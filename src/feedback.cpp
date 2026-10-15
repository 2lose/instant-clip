#include "feedback.hpp"

#include "background.hpp"
#include "embedded-secrets.hpp"
#include "http.hpp"
#include "updater.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSysInfo>

#include <obs.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace {

std::mutex g_mutex;
std::thread g_worker;
std::atomic<bool> g_busy = false;

} // namespace

bool feedback_available()
{
	return !embedded_secret(Secret::FeedbackUrl).empty();
}

void send_feedback(const FeedbackForm &form, QObject *context, std::function<void(bool, QString)> done)
{
	if (g_busy.exchange(true)) {
		done(false, "Still sending the last one.");
		return;
	}

	QJsonObject payload{
		{"kind", form.kind},
		{"message", form.message},
		{"contact", form.contact},
		{"plugin_version", Updater::currentVersion()},
		{"obs_version", QString::fromUtf8(obs_get_version_string())},
		{"os", QSysInfo::prettyProductName()},
	};
	std::string body = QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString();

	std::lock_guard lock(g_mutex);
	if (g_worker.joinable())
		g_worker.join();

	QPointer<QObject> guard(context);
	g_worker = std::thread([body, guard, done = std::move(done)] {
		enter_background_mode();
		std::string url = embedded_secret(Secret::FeedbackUrl);
		std::string key = embedded_secret(Secret::FeedbackKey);

		bool ok = false;
		QString error;
		if (url.empty()) {
			error = "Feedback isn't set up in this build.";
		} else {
			std::vector<std::wstring> headers = {
				L"Content-Type: application/json",
				L"X-Instant-Clip-Key: " + QString::fromStdString(key).toStdWString(),
				L"User-Agent: instant-clip/" + Updater::currentVersion().toStdWString(),
			};
			HttpResponse res = http_post(QString::fromStdString(url).toStdWString(), headers, body);
			if (!res.error.empty())
				error = QString::fromStdString(res.error);
			else if (res.status == 429)
				error = "That's a lot of feedback at once. Try again in a minute.";
			else if (res.status < 200 || res.status >= 300)
				error = QString("Couldn't send feedback (HTTP %1).").arg(res.status);
			else
				ok = true;
		}

		g_busy = false;
		if (QObject *ctx = guard.data())
			QMetaObject::invokeMethod(ctx, [done, ok, error] { done(ok, error); }, Qt::QueuedConnection);
	});
}

void feedback_shutdown()
{
	std::lock_guard lock(g_mutex);
	if (g_worker.joinable())
		g_worker.join();
}
