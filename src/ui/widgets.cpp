#include "widgets.hpp"

#include <QEnterEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QToolTip>

#include <cmath>


using namespace theme;

namespace ui {

namespace {

const QColor onAccent{0x10, 0x12, 0x1a};

void roundRect(QPainter &p, const QRectF &r, qreal radius, const QColor &fill, const QColor &stroke = Qt::transparent)
{
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setBrush(fill);
	if (stroke.alpha() > 0)
		p.setPen(QPen(stroke, 1.0));
	else
		p.setPen(Qt::NoPen);
	p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
	p.restore();
}

void drawText(QPainter &p, const QRectF &r, int flags, const QString &text, const QFont &font, const QColor &color)
{
	p.setFont(font);
	p.setPen(color);
	QString elided = QFontMetrics(font).elidedText(text, Qt::ElideRight, (int)std::ceil(r.width()) + 3);
	p.drawText(r, flags, elided);
}

int textWidth(const QFont &font, const QString &text)
{
	return QFontMetrics(font).horizontalAdvance(text);
}

QString shortClipName(const QString &path)
{
	QString base = QFileInfo(path).completeBaseName();
	static const QRegularExpression re(R"(^Clip \d{4}-\d{2}-\d{2} (\d{2}-\d{2}-\d{2}))");
	auto m = re.match(base);
	return m.hasMatch() ? "Clip " + m.captured(1) : base;
}

QString timeAgo(const QDateTime &when)
{
	qint64 s = when.secsTo(QDateTime::currentDateTime());
	if (s < 60)
		return "just now";
	if (s < 3600)
		return QString("%1 min ago").arg(s / 60);
	if (s < 86400)
		return QString("%1 h ago").arg(s / 3600);
	return when.toString("MMM d");
}

} // namespace

QLabel *makeLabel(const QString &text, const QFont &font, const QColor &color, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setFont(font);
	// Repeat the font in the label's own style sheet: OBS themes style QLabel fonts, and an
	// application style sheet wins over setFont() but not over a widget style sheet.
	label->setStyleSheet(QString("color: %1; background: transparent; font-family: \"%2\"; font-size: %3px; "
				     "font-weight: %4;")
				     .arg(color.name(), font.family())
				     .arg(font.pixelSize())
				     .arg(font.weight()));
	return label;
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
	QFont f = makeFont(12, 600);
	f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
	return makeLabel(text.toUpper(), f, muted, parent);
}

QLabel *hintLabel(const QString &text, QWidget *parent)
{
	QLabel *label = makeLabel(text, makeFont(13, 400), muted, parent);
	label->setWordWrap(true);
	return label;
}

/* ------------------------------------------------------------------------ */

PillButton::PillButton(const QString &text, Kind kind, Lead lead, QWidget *parent)
	: QAbstractButton(parent),
	  kind_(kind),
	  lead_(lead)
{
	setText(text);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void PillButton::setKind(Kind kind, Lead lead)
{
	kind_ = kind;
	lead_ = lead;
	updateGeometry();
	update();
}

QSize PillButton::sizeHint() const
{
	QFont f = makeFont(kind_ == Kind::Link ? 14 : 15, kind_ == Kind::Link ? 500 : 600);
	int w = textWidth(f, text());
	if (kind_ == Kind::Link)
		return {w + 4, 24};
	int lead = lead_ == Lead::None ? 0 : (lead_ == Lead::Back ? 16 : 14) + 7;
	int pad = kind_ == Kind::Ghost ? 8 : 16;
	return {w + lead + pad * 2, kind_ == Kind::Ghost ? 32 : 36};
}

void PillButton::enterEvent(QEnterEvent *)
{
	hover_ = true;
	update();
}

void PillButton::leaveEvent(QEvent *)
{
	hover_ = false;
	update();
}

void PillButton::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	if (!isEnabled())
		p.setOpacity(0.45);

	QRectF r = rect();
	QColor fg = textColor;
	bool down = isDown();

	switch (kind_) {
	case Kind::Primary:
		roundRect(p, r, radius, down ? mix(accent, ground, 0.12) : hover_ ? mix(accent, Qt::white, 0.1) : accent);
		fg = onAccent;
		break;
	case Kind::Secondary:
		roundRect(p, r, radius, down ? surface : hover_ ? surface2 : surface, hover_ ? borderHover : border);
		break;
	case Kind::Ghost:
		if (hover_ || down)
			roundRect(p, r, 8, surface);
		break;
	case Kind::Link:
		fg = hover_ ? mix(accentText, Qt::white, 0.35) : accentText;
		break;
	}
	if (hasFocus() && kind_ != Kind::Link)
		roundRect(p, r.adjusted(-1, -1, 1, 1), radius + 1, Qt::transparent, mix(accent, ground, 0.3));

	QFont f = makeFont(kind_ == Kind::Link ? 14 : 15, kind_ == Kind::Link ? 500 : 600);
	int tw = textWidth(f, text());
	int leadW = lead_ == Lead::None ? 0 : (lead_ == Lead::Back ? 16 : 14) + 7;
	qreal x = kind_ == Kind::Link ? r.width() - tw - 2 : (r.width() - tw - leadW) / 2.0;
	qreal cy = r.height() / 2.0;

	switch (lead_) {
	case Lead::Dot:
		p.setPen(Qt::NoPen);
		p.setBrush(fg);
		p.drawEllipse(QPointF(x + 5, cy), 4.5, 4.5);
		break;
	case Lead::Stop:
		drawIcon(p, Icon::Stop, QRectF(x - 1, cy - 8, 16, 16), fg);
		break;
	case Lead::Back:
		drawIcon(p, Icon::ChevronLeft, QRectF(x - 2, cy - 9, 18, 18), fg);
		break;
	case Lead::None:
		break;
	}
	drawText(p, QRectF(x + leadW, 0, tw + 2, r.height()), Qt::AlignLeft | Qt::AlignVCenter, text(), f, fg);
}

/* ------------------------------------------------------------------------ */

StatusCard::StatusCard(QWidget *parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void StatusCard::setState(bool live, const QString &label, const QString &detail)
{
	live_ = live;
	label_ = label;
	detail_ = detail;
	update();
}

QSize StatusCard::sizeHint() const
{
	return {200, 40};
}

void StatusCard::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF r = rect();
	roundRect(p, r, radius, surface, border);

	qreal cy = r.height() / 2.0;
	QPointF dot(17, cy);
	p.setPen(Qt::NoPen);
	if (live_) {
		QColor halo = live;
		halo.setAlphaF(0.22);
		p.setBrush(halo);
		p.drawEllipse(dot, 8, 8);
	}
	p.setBrush(live_ ? live : QColor(0x80, 0x83, 0x90));
	p.drawEllipse(dot, 4.5, 4.5);

	QFont detailFont = makeFont(14, 400, Face::Text, true);
	int dw = textWidth(detailFont, detail_) + 4;
	qreal right = r.width() - 14;
	drawText(p, QRectF(right - dw, 0, dw, r.height()), Qt::AlignRight | Qt::AlignVCenter, detail_, detailFont, muted);
	drawText(p, QRectF(32, 0, std::max(0.0, right - dw - 40), r.height()), Qt::AlignLeft | Qt::AlignVCenter, label_,
		 makeFont(14, 600), live_ ? live : textColor);
}

/* ------------------------------------------------------------------------ */

ClipButton::ClipButton(int seconds, QWidget *parent) : QAbstractButton(parent), seconds_(seconds)
{
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setText(ClipManager::lengthLabel(seconds).remove(' '));
}

void ClipButton::setHotkey(const QString &text)
{
	hotkey_ = text;
	setToolTip(QString("Save the last %1%2")
			   .arg(ClipManager::lengthLabel(seconds_), text.isEmpty() ? QString() : " (" + text + ")"));
	update();
}

void ClipButton::setWide(bool wide)
{
	if (wide_ == wide)
		return;
	wide_ = wide;
	updateGeometry();
	update();
}

void ClipButton::setActive(bool active)
{
	active_ = active;
	update();
}

void ClipButton::setTooLong(bool tooLong)
{
	if (tooLong_ == tooLong)
		return;
	tooLong_ = tooLong;
	updateGeometry();
	update();
}

void ClipButton::setDimmed(bool dimmed)
{
	dimmed_ = dimmed;
	update();
}

QSize ClipButton::sizeHint() const
{
	if (wide_)
		return {200, tooLong_ ? 62 : 50};
	return {100, 62};
}

QSize ClipButton::minimumSizeHint() const
{
	return {80, sizeHint().height()};
}

void ClipButton::enterEvent(QEnterEvent *)
{
	hover_ = true;
	update();
}

void ClipButton::leaveEvent(QEvent *)
{
	hover_ = false;
	update();
}

void ClipButton::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF r = rect();

	QColor fill = surface, stroke = border, hotkeyColor = muted;
	QColor durationColor = dimmed_ ? mix(textColor, ground, 0.38) : textColor;
	if (tooLong_) {
		fill = mix(ground, warn, 0.07);
		stroke = mix(ground, warn, 0.36);
	}
	if (hover_ && !active_) {
		fill = mix(fill, surface2, 0.6);
		stroke = tooLong_ ? mix(ground, warn, 0.5) : borderHover;
	}
	if (active_ || isDown()) {
		fill = mix(ground, accent, 0.17);
		stroke = accent;
		hotkeyColor = accentText;
		durationColor = textColor;
	}
	if (dimmed_ && !active_)
		hotkeyColor = mix(muted, ground, 0.3);
	roundRect(p, r, radius, fill, stroke);
	if (hasFocus())
		roundRect(p, r.adjusted(1, 1, -1, -1), radius - 1, Qt::transparent, mix(accent, ground, 0.4));

	const qreal pad = 13;
	QFont durFont = makeFont(22, 700, Face::Display, true);
	QFont keyFont = makeFont(12, 400);
	QString key = hotkey_.isEmpty() ? QStringLiteral("Not bound") : hotkey_;
	QFont warnFont = makeFont(13, 500);

	if (wide_) {
		qreal rowH = 48;
		int dw = textWidth(durFont, text());
		drawText(p, QRectF(pad, 0, dw + 4, rowH), Qt::AlignLeft | Qt::AlignVCenter, text(), durFont, durationColor);
		qreal keyX = pad + dw + 16;
		drawText(p, QRectF(keyX, 0, r.width() - pad - keyX, rowH), Qt::AlignRight | Qt::AlignVCenter, key, keyFont,
			 hotkeyColor);
		if (tooLong_) {
			qreal y = 40;
			drawIcon(p, Icon::Warning, QRectF(pad, y, 15, 15), warn);
			drawText(p, QRectF(pad + 21, y - 2, r.width() - pad * 2 - 21, 19), Qt::AlignLeft | Qt::AlignVCenter,
				 "Longer than the buffer", warnFont, warn);
		}
		return;
	}

	drawText(p, QRectF(pad, 9, r.width() - pad * 2, 28), Qt::AlignLeft | Qt::AlignVCenter, text(), durFont,
		 durationColor);
	if (tooLong_) {
		drawIcon(p, Icon::Warning, QRectF(r.width() - pad - 14, 14, 15, 15), warn);
		drawText(p, QRectF(pad, 37, r.width() - pad * 2, 17), Qt::AlignLeft | Qt::AlignVCenter,
			 "Over buffer", makeFont(12, 500), warn);
	} else {
		drawText(p, QRectF(pad, 37, r.width() - pad * 2, 17), Qt::AlignLeft | Qt::AlignVCenter, key, keyFont,
			 hotkeyColor);
	}
}

/* ------------------------------------------------------------------------ */

Toast::Toast(QWidget *parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setMouseTracking(true);
	spinTimer_.setInterval(40);
	connect(&spinTimer_, &QTimer::timeout, this, [this] {
		// Off-screen (dock hidden or tabbed away): stop waking up. The next
		// progress update restarts it.
		if (!isVisible()) {
			spinTimer_.stop();
			return;
		}
		spin_ = (spin_ + 15) % 360;
		update();
	});
	hideTimer_.setSingleShot(true);
	connect(&hideTimer_, &QTimer::timeout, this, [this] { setVisible(false); });
	setVisible(false);
}

void Toast::showMessage(Kind kind, const QString &text, const QString &detail, const QString &action,
			std::function<void()> onAction, int hideAfterMs)
{
	kind_ = kind;
	text_ = text;
	detail_ = detail;
	action_ = action;
	onAction_ = std::move(onAction);
	if (kind != Kind::Progress)
		progress_ = 0.0;
	if (kind == Kind::Progress)
		spinTimer_.start();
	else
		spinTimer_.stop();
	if (hideAfterMs > 0)
		hideTimer_.start(hideAfterMs);
	else
		hideTimer_.stop();
	setVisible(true);
	updateGeometry();
	update();
}

void Toast::setProgress(double p)
{
	progress_ = p;
	update();
}

int Toast::detailLines(int width) const
{
	if (detail_.isEmpty())
		return 0;
	QFontMetrics fm(makeFont(12, 400));
	QRect br = fm.boundingRect(QRect(0, 0, std::max(width - 44 - 14, 40), 1000), Qt::TextWordWrap, detail_);
	return std::max(1, (int)std::ceil(br.height() / (double)fm.lineSpacing()));
}

int Toast::heightForWidth(int w) const
{
	int lines = detailLines(w);
	return 46 + (lines ? lines * 16 + 2 : 0);
}

QSize Toast::sizeHint() const
{
	return {200, heightForWidth(width() > 0 ? width() : 268)};
}

QRectF Toast::actionRect() const
{
	if (action_.isEmpty())
		return {};
	int w = textWidth(makeFont(14, 600), action_);
	return QRectF(width() - 14 - w - 4, 11, w + 8, 24);
}

void Toast::mouseMoveEvent(QMouseEvent *e)
{
	bool h = actionRect().contains(e->position());
	if (h != actionHover_) {
		actionHover_ = h;
		setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
		update();
	}
}

void Toast::mousePressEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton && actionRect().contains(e->position()) && onAction_)
		onAction_();
}

void Toast::leaveEvent(QEvent *)
{
	actionHover_ = false;
	update();
}

void Toast::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF r = rect();

	QColor tone = kind_ == Kind::Progress || kind_ == Kind::Info ? accent
		      : kind_ == Kind::Success			     ? live
		      : kind_ == Kind::Warning			     ? warn
								     : danger;
	roundRect(p, r, radius, mix(ground, tone, 0.10), mix(ground, tone, 0.32));

	QRectF iconRect(14, 13, 20, 20);
	if (kind_ == Kind::Progress) {
		p.save();
		p.translate(iconRect.center());
		p.rotate(spin_);
		drawIcon(p, Icon::Spinner, QRectF(-9, -9, 18, 18), tone);
		p.restore();
	} else {
		Icon icon = kind_ == Kind::Success ? Icon::Check : kind_ == Kind::Info ? Icon::Download : Icon::Warning;
		drawIcon(p, icon, iconRect, tone);
	}

	QRectF act = actionRect();
	qreal textRight = act.isNull() ? r.width() - 14 : act.left() - 8;
	drawText(p, QRectF(44, 0, textRight - 44, 46), Qt::AlignLeft | Qt::AlignVCenter, text_, makeFont(14, 500), textColor);
	if (!act.isNull())
		drawText(p, act, Qt::AlignCenter, action_, makeFont(14, 600),
			 actionHover_ ? mix(accentText, Qt::white, 0.35) : accentText);

	if (!detail_.isEmpty()) {
		p.setFont(makeFont(12, 400));
		p.setPen(muted);
		p.drawText(QRectF(44, 38, r.width() - 44 - 14, r.height() - 44), Qt::TextWordWrap | Qt::AlignTop, detail_);
	}

	if (kind_ == Kind::Progress && progress_ > 0) {
		QPainterPath clip;
		clip.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
		p.setClipPath(clip);
		p.fillRect(QRectF(0, r.height() - 3, r.width() * progress_, 3), accent);
	}
}

/* ------------------------------------------------------------------------ */

namespace {
constexpr int kRowH = 54;
}

RecentList::RecentList(QWidget *parent) : QWidget(parent)
{
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void RecentList::setClips(const QList<RecentClip> &clips)
{
	clips_ = clips;
	updateGeometry();
	update();
}

QSize RecentList::sizeHint() const
{
	return {200, std::max<int>(1, (int)clips_.size()) * kRowH};
}

QRectF RecentList::rowRect(int row) const
{
	return QRectF(0, row * kRowH, width(), kRowH);
}

QRectF RecentList::playRect(int row) const
{
	QRectF r = rowRect(row);
	return QRectF(12, r.top() + 11, 32, 32);
}

QRectF RecentList::revealRect(int row) const
{
	QRectF r = rowRect(row);
	return QRectF(r.width() - 12 - 32, r.top() + 11, 32, 32);
}

RecentList::HitResult RecentList::hitTest(const QPoint &pos) const
{
	HitResult h;
	int row = pos.y() / kRowH;
	if (row < 0 || row >= clips_.size())
		return h;
	h.row = row;
	if (revealRect(row).contains(pos))
		h.hit = Hit::Reveal;
	else
		h.hit = Hit::Open;
	return h;
}

void RecentList::mouseMoveEvent(QMouseEvent *e)
{
	HitResult h = hitTest(e->pos());
	if (h.row != hover_.row || h.hit != hover_.hit) {
		hover_ = h;
		setCursor(h.row >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
		QString tip = h.hit == Hit::Reveal ? "Show in folder" : h.hit == Hit::Open ? "Open" : QString();
		if (tip.isEmpty())
			QToolTip::hideText();
		else
			QToolTip::showText(e->globalPosition().toPoint(), tip, this);
		update();
	}
}

void RecentList::mousePressEvent(QMouseEvent *e)
{
	if (e->button() != Qt::LeftButton)
		return;
	HitResult h = hitTest(e->pos());
	if (h.row < 0)
		return;
	const QString path = clips_[h.row].path;
	if (h.hit == Hit::Reveal && onReveal)
		onReveal(path);
	else if (h.hit == Hit::Open && onOpen)
		onOpen(path);
}

void RecentList::leaveEvent(QEvent *)
{
	hover_ = {};
	update();
}

void RecentList::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF r = rect();
	roundRect(p, r, radius, surface, border);

	if (clips_.isEmpty()) {
		drawText(p, r, Qt::AlignCenter, "No clips yet", makeFont(13, 400), muted);
		return;
	}

	for (int i = 0; i < clips_.size(); i++) {
		const RecentClip &clip = clips_[i];
		QRectF row = rowRect(i);
		bool rowHover = hover_.row == i;

		if (i > 0) {
			p.setPen(QPen(border, 1));
			p.drawLine(QPointF(1, row.top() + 0.5), QPointF(r.width() - 1, row.top() + 0.5));
		}

		QRectF play = playRect(i);
		bool playHot = rowHover && hover_.hit == Hit::Open;
		roundRect(p, play, 7, playHot ? mix(surface2, accent, 0.25) : surface2);
		drawIcon(p, Icon::Play, play.adjusted(8, 8, -8, -8), playHot ? textColor : muted);

		QRectF reveal = revealRect(i);
		bool revealHot = rowHover && hover_.hit == Hit::Reveal;
		if (revealHot)
			roundRect(p, reveal, 7, surface2);
		drawIcon(p, Icon::External, reveal.adjusted(7, 7, -7, -7), revealHot ? textColor : muted);

		qreal x = play.right() + 12;
		qreal w = reveal.left() - 8 - x;
		drawText(p, QRectF(x, row.top() + 8, w, 20), Qt::AlignLeft | Qt::AlignVCenter, shortClipName(clip.path),
			 makeFont(14, 600, Face::Text, true), textColor);
		QString sub = ClipManager::lengthLabel(clip.seconds).remove(' ') + QString::fromUtf8(" · ") +
			      timeAgo(clip.savedAt);
		drawText(p, QRectF(x, row.top() + 28, w, 18), Qt::AlignLeft | Qt::AlignVCenter, sub,
			 makeFont(13, 400, Face::Text, true), muted);
	}
}

/* ------------------------------------------------------------------------ */

Toggle::Toggle(QWidget *parent) : QAbstractButton(parent)
{
	setCheckable(true);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Toggle::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF r(0, 0, 40, 22);
	bool on = isChecked();
	roundRect(p, r, 11, on ? accent : QColor(0x3a, 0x3c, 0x46));
	p.setPen(Qt::NoPen);
	p.setBrush(on ? QColor(Qt::white) : QColor(0xd2, 0xd4, 0xdc));
	p.drawEllipse(QPointF(on ? 29 : 11, 11), 8, 8);
}

/* ------------------------------------------------------------------------ */

Card::Card(QWidget *parent) : QWidget(parent), layout_(new QVBoxLayout(this))
{
	layout_->setContentsMargins(0, 0, 0, 0);
	layout_->setSpacing(1);
}

void Card::addRow(QWidget *row)
{
	layout_->addWidget(row);
}

void Card::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	roundRect(p, rect(), radius, surface, border);
	p.setPen(QPen(border, 1));
	for (int i = 1; i < layout_->count(); i++) {
		QWidget *w = layout_->itemAt(i)->widget();
		if (!w || !w->isVisible())
			continue;
		qreal y = w->geometry().top() - 0.5;
		p.drawLine(QPointF(1, y), QPointF(width() - 1, y));
	}
}

QWidget *toggleRow(const QString &label, Toggle *toggle)
{
	auto *row = new QWidget;
	row->setFixedHeight(46);
	row->setAttribute(Qt::WA_TranslucentBackground);
	auto *l = new QHBoxLayout(row);
	l->setContentsMargins(14, 0, 12, 0);
	QLabel *text = makeLabel(label, makeFont(14, 400), textColor);
	text->setMinimumWidth(10);
	l->addWidget(text, 1);
	l->addWidget(toggle);
	return row;
}

/* ------------------------------------------------------------------------ */

Segmented::Segmented(const QStringList &options, QWidget *parent) : QWidget(parent), options_(options)
{
	setMouseTracking(true);
	setCursor(Qt::PointingHandCursor);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Segmented::setCurrent(int index)
{
	current_ = index;
	update();
}

QRectF Segmented::segment(int i) const
{
	qreal inner = width() - 8;
	qreal w = inner / options_.size();
	return QRectF(4 + i * w, 4, w, height() - 8);
}

void Segmented::mousePressEvent(QMouseEvent *e)
{
	for (int i = 0; i < options_.size(); i++) {
		if (segment(i).contains(e->position()) && i != current_) {
			current_ = i;
			update();
			if (onChanged)
				onChanged(i);
			return;
		}
	}
}

void Segmented::mouseMoveEvent(QMouseEvent *e)
{
	int h = -1;
	for (int i = 0; i < options_.size(); i++) {
		if (segment(i).contains(e->position()))
			h = i;
	}
	if (h != hover_) {
		hover_ = h;
		update();
	}
}

void Segmented::leaveEvent(QEvent *)
{
	hover_ = -1;
	update();
}

void Segmented::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	roundRect(p, rect(), radius, surface, border);
	QFont f = makeFont(14, 600);
	for (int i = 0; i < options_.size(); i++) {
		QRectF s = segment(i);
		if (i == current_)
			roundRect(p, s, 7, accent);
		else if (i == hover_)
			roundRect(p, s, 7, surface2);
		drawText(p, s, Qt::AlignCenter, options_[i], f, i == current_ ? onAccent : textColor);
	}
}

/* ------------------------------------------------------------------------ */

Chip::Chip(const QString &label, QWidget *parent) : QAbstractButton(parent)
{
	setText(label);
	setCursor(Qt::PointingHandCursor);
	setToolTip("Remove " + label);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize Chip::sizeHint() const
{
	return {textWidth(makeFont(14, 600, Face::Text, true), text()) + 14 + 10 + 8 + 12, 32};
}

void Chip::enterEvent(QEnterEvent *)
{
	hover_ = true;
	update();
}

void Chip::leaveEvent(QEvent *)
{
	hover_ = false;
	update();
}

void Chip::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF r = rect();
	roundRect(p, r, r.height() / 2, hover_ ? surface2 : mix(surface, surface2, 0.6), hover_ ? borderHover : border);
	QFont f = makeFont(14, 600, Face::Text, true);
	int tw = textWidth(f, text());
	drawText(p, QRectF(14, 0, tw + 2, r.height()), Qt::AlignLeft | Qt::AlignVCenter, text(), f, textColor);
	drawIcon(p, Icon::Close, QRectF(r.width() - 12 - 11, r.height() / 2 - 5.5, 11, 11), hover_ ? textColor : dim);
}

/* ------------------------------------------------------------------------ */

AddChip::AddChip(QWidget *parent) : QWidget(parent), edit_(new QLineEdit(this))
{
	setCursor(Qt::PointingHandCursor);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setToolTip("Add a clip length, e.g. 45s, 90, 1m30 or 2:30");
	edit_->setFont(makeFont(14, 600));
	edit_->setPlaceholderText("45s");
	edit_->setFrame(false);
	edit_->setAlignment(Qt::AlignCenter);
	edit_->setStyleSheet(QString("QLineEdit { background: transparent; color: %1; border: none; padding: 0; }")
				     .arg(textColor.name()));
	edit_->hide();
	edit_->installEventFilter(this);
	connect(edit_, &QLineEdit::returnPressed, this, [this] {
		if (!onSubmit || onSubmit(edit_->text().trimmed()))
			setEditing(false);
		else
			setInvalid(true);
	});
	connect(edit_, &QLineEdit::textEdited, this, [this] { setInvalid(false); });
}

QSize AddChip::sizeHint() const
{
	if (editing_)
		return {84, 32};
	return {textWidth(makeFont(14, 600), "+ Add") + 30, 32};
}

void AddChip::setInvalid(bool invalid)
{
	invalid_ = invalid;
	update();
}

void AddChip::setEditing(bool editing)
{
	editing_ = editing;
	invalid_ = false;
	edit_->setVisible(editing);
	if (editing) {
		edit_->clear();
		edit_->setGeometry(rect().adjusted(10, 4, -10, -4));
		edit_->setFocus(Qt::MouseFocusReason);
		// A floating dock activates its window after the click; take focus back once it has.
		QTimer::singleShot(0, this, [this] {
			if (!editing_)
				return;
			window()->activateWindow();
			edit_->setFocus(Qt::MouseFocusReason);
		});
	}
	updateGeometry();
	if (parentWidget() && parentWidget()->layout())
		parentWidget()->layout()->invalidate();
	update();
	if (editing)
		QTimer::singleShot(0, this, [this] { edit_->setGeometry(rect().adjusted(10, 4, -10, -4)); });
}

void AddChip::mousePressEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton && !editing_)
		setEditing(true);
}

void AddChip::enterEvent(QEnterEvent *)
{
	hover_ = true;
	update();
}

void AddChip::leaveEvent(QEvent *)
{
	hover_ = false;
	update();
}

bool AddChip::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == edit_) {
		if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
			setEditing(false);
			return true;
		}
		if (event->type() == QEvent::FocusOut && editing_) {
			// Window activation shuffles focus around; only a real move elsewhere cancels.
			Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
			if (reason != Qt::ActiveWindowFocusReason && reason != Qt::PopupFocusReason &&
			    reason != Qt::OtherFocusReason)
				QTimer::singleShot(0, this, [this] { setEditing(false); });
		}
	}
	return QWidget::eventFilter(obj, event);
}

void AddChip::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	QColor stroke = invalid_ ? danger : editing_ ? accent : hover_ ? accentText : QColor(0x5a, 0x5d, 0x6a);
	QPen pen(stroke, 1.2);
	if (!editing_)
		pen.setDashPattern({3.5, 3});
	p.setPen(pen);
	p.setBrush(editing_ ? surface : Qt::transparent);
	p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
	if (!editing_)
		drawText(p, rect(), Qt::AlignCenter, "+ Add", makeFont(14, 600), accentText);
}

/* ------------------------------------------------------------------------ */

SettingsEntry::SettingsEntry(const QString &label, Icon icon, QWidget *parent) : QAbstractButton(parent), icon_(icon)
{
	setText(label);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SettingsEntry::enterEvent(QEnterEvent *)
{
	hover_ = true;
	update();
}

void SettingsEntry::leaveEvent(QEvent *)
{
	hover_ = false;
	update();
}

void SettingsEntry::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF r = rect();
	roundRect(p, r, radius, hover_ ? mix(surface, surface2, 0.6) : surface, hover_ ? borderHover : border);
	qreal cy = r.height() / 2;
	drawIcon(p, icon_, QRectF(14, cy - 9, 18, 18), muted);
	drawText(p, QRectF(42, 0, r.width() - 80, r.height()), Qt::AlignLeft | Qt::AlignVCenter, text(), makeFont(15, 600),
		 textColor);
	drawIcon(p, Icon::ChevronDown, QRectF(r.width() - 14 - 18, cy - 9, 18, 18), muted);
}

/* ------------------------------------------------------------------------ */

FlowLayout::FlowLayout(QWidget *parent, int hSpacing, int vSpacing)
	: QLayout(parent),
	  hSpacing_(hSpacing),
	  vSpacing_(vSpacing)
{
	setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
	while (QLayoutItem *item = takeAt(0))
		delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
	items_.append(item);
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
	return items_.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
	if (index < 0 || index >= items_.size())
		return nullptr;
	return items_.takeAt(index);
}

int FlowLayout::heightForWidth(int width) const
{
	return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect &rect)
{
	QLayout::setGeometry(rect);
	doLayout(rect, false);
}

QSize FlowLayout::minimumSize() const
{
	QSize size;
	for (QLayoutItem *item : items_)
		size = size.expandedTo(item->minimumSize());
	return size;
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
	int x = rect.x(), y = rect.y(), lineHeight = 0;
	for (QLayoutItem *item : items_) {
		// isHidden(), not !isVisible(): the settings page is laid out while it's still off-screen.
		if (item->widget() && item->widget()->isHidden())
			continue;
		QSize hint = item->sizeHint();
		int nextX = x + hint.width() + hSpacing_;
		if (nextX - hSpacing_ > rect.right() + 1 && lineHeight > 0) {
			x = rect.x();
			y += lineHeight + vSpacing_;
			nextX = x + hint.width() + hSpacing_;
			lineHeight = 0;
		}
		if (!testOnly)
			item->setGeometry(QRect(QPoint(x, y), hint));
		x = nextX;
		lineHeight = std::max(lineHeight, hint.height());
	}
	return y + lineHeight - rect.y();
}

} // namespace ui
