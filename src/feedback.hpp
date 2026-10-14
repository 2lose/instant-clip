#pragma once

#include <QObject>
#include <QString>

#include <functional>

struct FeedbackForm {
	QString kind; // "bug", "idea" or "other"
	QString message;
	QString contact; // optional
};

// True when this build knows where to send feedback (relay URL embedded).
bool feedback_available();

// Posts the form to the feedback relay on a background thread. `done(ok, error)`
// runs on `context`'s thread, unless `context` is gone by then.
void send_feedback(const FeedbackForm &form, QObject *context, std::function<void(bool, QString)> done);

// Waits for an in-flight send; call before the plugin unloads.
void feedback_shutdown();
