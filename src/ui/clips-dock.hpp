#pragma once

#include <QList>
#include <QMap>
#include <QTimer>
#include <QWidget>

class QGridLayout;
class QLabel;
class QLineEdit;
class QStackedWidget;
class ClipManager;

namespace ui {
class AddChip;
class ClipButton;
class FlowLayout;
class PillButton;
class RecentList;
class Segmented;
class StatusCard;
class Toast;
class Toggle;
} // namespace ui

// The "Clips" dock: buffer status, one button per clip length, recent clips, settings.
class ClipsDock : public QWidget {
public:
	explicit ClipsDock(QWidget *parent = nullptr);

	// "45", "45s", "1m", "1m30", "1m 30s", "1.5m", "2:30" -> seconds, 0 if unreadable.
	static int parseLength(const QString &input);

protected:
	void paintEvent(QPaintEvent *) override;
	void showEvent(QShowEvent *) override;

private:
	QWidget *buildMain();
	QWidget *buildSettings();

	void refreshBuffer();
	void rebuildClipButtons();
	void refreshClipStates();
	void refreshHotkeys();
	void refreshRecent();
	void rebuildChips();
	void refreshSettings();

	void reveal(const QString &path);
	void open(const QString &path);

	ClipManager *m_;
	QStackedWidget *stack_ = nullptr;

	// Main view
	ui::PillButton *bufferBtn_ = nullptr;
	ui::StatusCard *status_ = nullptr;
	QLabel *offHint_ = nullptr;
	QGridLayout *grid_ = nullptr;
	QList<ui::ClipButton *> clipButtons_;
	ui::Toast *toast_ = nullptr;
	ui::RecentList *recent_ = nullptr;
	QWidget *recentSection_ = nullptr;
	QMap<int, int> busy_; // seconds -> clips of that length still in flight

	// Settings view
	QWidget *chipsBox_ = nullptr;
	ui::FlowLayout *chipsFlow_ = nullptr;
	ui::AddChip *addChip_ = nullptr;
	ui::Segmented *formatSeg_ = nullptr;
	ui::Segmented *trimSeg_ = nullptr;
	ui::Toggle *deleteToggle_ = nullptr;
	ui::Toggle *soundToggle_ = nullptr;
	QLineEdit *pathEdit_ = nullptr;
	QLabel *remuxHint_ = nullptr;

	// Updates
	QWidget *updateBox_ = nullptr;
	ui::Toast *updateToast_ = nullptr;
	QLabel *updateStatus_ = nullptr;
	void refreshUpdate();

	QTimer tick_;
};
