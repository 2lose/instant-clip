#pragma once

#include <QAbstractButton>
#include <QBoxLayout>
#include <QLayout>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <functional>

#include "clip-manager.hpp"
#include "theme.hpp"

class QLabel;
class QLineEdit;

namespace ui {

QLabel *makeLabel(const QString &text, const QFont &font, const QColor &color, QWidget *parent = nullptr);
QLabel *sectionLabel(const QString &text, QWidget *parent = nullptr);
QLabel *hintLabel(const QString &text, QWidget *parent = nullptr);

// Rounded button used for Start/Stop, Browse, the back button and inline links.
class PillButton : public QAbstractButton {
public:
	enum class Kind { Primary, Secondary, Ghost, Link };
	enum class Lead { None, Dot, Stop, Back };

	PillButton(const QString &text, Kind kind, Lead lead = Lead::None, QWidget *parent = nullptr);
	void setKind(Kind kind, Lead lead);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *) override;
	void enterEvent(QEnterEvent *) override;
	void leaveEvent(QEvent *) override;

private:
	Kind kind_;
	Lead lead_;
	bool hover_ = false;
};

// "Buffer live, holds 5:00" / "Buffer off, max 2:00"
class StatusCard : public QWidget {
public:
	explicit StatusCard(QWidget *parent = nullptr);
	void setState(bool live, const QString &label, const QString &detail);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent *) override;

private:
	bool live_ = false;
	QString label_;
	QString detail_;
};

// One "save last N" button.
class ClipButton : public QAbstractButton {
public:
	ClipButton(int seconds, QWidget *parent = nullptr);
	int seconds() const { return seconds_; }
	void setHotkey(const QString &text);
	void setWide(bool wide);
	void setActive(bool active);
	void setTooLong(bool tooLong);
	void setDimmed(bool dimmed);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *) override;
	void enterEvent(QEnterEvent *) override;
	void leaveEvent(QEvent *) override;

private:
	int seconds_;
	QString hotkey_;
	bool wide_ = false;
	bool active_ = false;
	bool tooLong_ = false;
	bool dimmed_ = false;
	bool hover_ = false;
};

// Status line under the buttons: progress while trimming, then the result.
class Toast : public QWidget {
public:
	enum class Kind { Progress, Success, Warning, Error, Info };

	explicit Toast(QWidget *parent = nullptr);
	void showMessage(Kind kind, const QString &text, const QString &detail = {}, const QString &action = {},
		  std::function<void()> onAction = {}, int hideAfterMs = 0);
	void setProgress(double p);
	QSize sizeHint() const override;
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int w) const override;

protected:
	void paintEvent(QPaintEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void leaveEvent(QEvent *) override;

private:
	QRectF actionRect() const;
	int detailLines(int width) const;

	Kind kind_ = Kind::Success;
	QString text_;
	QString detail_;
	QString action_;
	std::function<void()> onAction_;
	double progress_ = 0.0;
	bool actionHover_ = false;
	int spin_ = 0;
	QTimer spinTimer_;
	QTimer hideTimer_;
};

// Card of recently saved clips with open / show-in-folder actions.
class RecentList : public QWidget {
public:
	explicit RecentList(QWidget *parent = nullptr);
	void setClips(const QList<RecentClip> &clips);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

	std::function<void(const QString &)> onOpen;
	std::function<void(const QString &)> onReveal;

protected:
	void paintEvent(QPaintEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void leaveEvent(QEvent *) override;

private:
	enum class Hit { None, Open, Reveal };
	struct HitResult {
		int row = -1;
		Hit hit = Hit::None;
	};
	HitResult hitTest(const QPoint &pos) const;
	QRectF rowRect(int row) const;
	QRectF playRect(int row) const;
	QRectF revealRect(int row) const;

	QList<RecentClip> clips_;
	HitResult hover_;
};

class Toggle : public QAbstractButton {
public:
	explicit Toggle(QWidget *parent = nullptr);
	QSize sizeHint() const override { return {40, 22}; }

protected:
	void paintEvent(QPaintEvent *) override;
};

// Rounded card that draws dividers between its rows.
class Card : public QWidget {
public:
	explicit Card(QWidget *parent = nullptr);
	void addRow(QWidget *row);

protected:
	void paintEvent(QPaintEvent *) override;

private:
	QVBoxLayout *layout_;
};

// Label + toggle row for a Card.
QWidget *toggleRow(const QString &label, Toggle *toggle);

class Segmented : public QWidget {
public:
	Segmented(const QStringList &options, QWidget *parent = nullptr);
	void setCurrent(int index);
	int current() const { return current_; }
	QSize sizeHint() const override { return {200, 42}; }
	QSize minimumSizeHint() const override { return {120, 42}; }

	std::function<void(int)> onChanged;

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void leaveEvent(QEvent *) override;

private:
	QRectF segment(int i) const;
	QStringList options_;
	int current_ = 0;
	int hover_ = -1;
};

// "30s x" pill in the clip-length editor.
class Chip : public QAbstractButton {
public:
	Chip(const QString &text, QWidget *parent = nullptr);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *) override;
	void enterEvent(QEnterEvent *) override;
	void leaveEvent(QEvent *) override;

private:
	bool hover_ = false;
};

// Dashed "+ Add" pill that turns into a small text field.
class AddChip : public QWidget {
public:
	explicit AddChip(QWidget *parent = nullptr);
	QSize sizeHint() const override;
	void setInvalid(bool invalid);

	// Return false to keep the editor open (input rejected).
	std::function<bool(const QString &)> onSubmit;

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void enterEvent(QEnterEvent *) override;
	void leaveEvent(QEvent *) override;
	bool eventFilter(QObject *obj, QEvent *event) override;

private:
	void setEditing(bool editing);

	QLineEdit *edit_;
	bool editing_ = false;
	bool hover_ = false;
	bool invalid_ = false;
};

// Card-style entry at the bottom of the main view ("Settings", "Feedback").
class SettingsEntry : public QAbstractButton {
public:
	SettingsEntry(const QString &label, theme::Icon icon, QWidget *parent = nullptr);
	QSize sizeHint() const override { return {200, 44}; }
	QSize minimumSizeHint() const override { return {120, 44}; }

protected:
	void paintEvent(QPaintEvent *) override;
	void enterEvent(QEnterEvent *) override;
	void leaveEvent(QEvent *) override;

private:
	theme::Icon icon_;
	bool hover_ = false;
};

// Wrapping layout for the length chips.
class FlowLayout : public QLayout {
public:
	FlowLayout(QWidget *parent, int hSpacing, int vSpacing);
	~FlowLayout() override;

	void addItem(QLayoutItem *item) override;
	int count() const override { return (int)items_.size(); }
	QLayoutItem *itemAt(int index) const override;
	QLayoutItem *takeAt(int index) override;
	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override;
	void setGeometry(const QRect &rect) override;
	QSize sizeHint() const override { return minimumSize(); }
	QSize minimumSize() const override;

private:
	int doLayout(const QRect &rect, bool testOnly) const;
	QList<QLayoutItem *> items_;
	int hSpacing_;
	int vSpacing_;
};

} // namespace ui
