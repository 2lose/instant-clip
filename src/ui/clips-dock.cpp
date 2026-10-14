#include "clips-dock.hpp"

#include "clip-manager.hpp"
#include "updater.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

using namespace theme;
using namespace ui;

namespace {

QString clock(int seconds)
{
	return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

QString styleSheet()
{
	return QString(R"(
#ICRoot QStackedWidget, #ICRoot QScrollArea, #ICRoot #ICPage, #ICRoot #ICRow,
#ICRoot QScrollArea > QWidget#qt_scrollarea_viewport { background: transparent; border: none; }
#ICRoot QScrollBar:vertical { background: transparent; width: 8px; margin: 2px 1px 2px 0; border: none; }
#ICRoot QScrollBar::handle:vertical { background: %1; border-radius: 3px; min-height: 32px; }
#ICRoot QScrollBar::handle:vertical:hover { background: %2; }
#ICRoot QScrollBar::add-line:vertical, #ICRoot QScrollBar::sub-line:vertical { height: 0; border: none; background: none; }
#ICRoot QScrollBar::add-page:vertical, #ICRoot QScrollBar::sub-page:vertical { background: none; }
#ICRoot QLineEdit#ICPath { background: %3; border: 1px solid %1; border-radius: 10px; padding: 0 12px;
	color: %4; min-height: 36px; max-height: 36px; font-family: "Inter"; font-size: 14px; font-weight: 400;
	selection-background-color: %5; selection-color: #10121a; }
#ICRoot QLineEdit#ICPath:focus { border-color: %5; }
)")
		.arg(border.name(), borderHover.name(), surface.name(), textColor.name(), accent.name());
}

QWidget *row(QWidget *parent = nullptr)
{
	auto *w = new QWidget(parent);
	w->setObjectName("ICRow");
	return w;
}

QScrollArea *scrollPage(QWidget *content)
{
	auto *scroll = new QScrollArea;
	scroll->setWidget(content);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	return scroll;
}

} // namespace

ClipsDock::ClipsDock(QWidget *parent) : QWidget(parent), m_(ClipManager::get())
{
	theme::loadFonts();
	setObjectName("ICRoot");
	setStyleSheet(styleSheet());
	setMinimumWidth(220);

	stack_ = new QStackedWidget(this);
	stack_->addWidget(buildMain());
	stack_->addWidget(buildSettings());
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(stack_);

	connect(m_, &ClipManager::bufferChanged, this, [this] { refreshBuffer(); });
	connect(m_, &ClipManager::lengthsChanged, this, [this] {
		rebuildClipButtons();
		rebuildChips();
	});
	connect(m_, &ClipManager::hotkeysChanged, this, [this] { refreshHotkeys(); });
	connect(m_, &ClipManager::settingsChanged, this, [this] {
		refreshSettings();
		refreshBuffer();
	});
	connect(m_, &ClipManager::recentChanged, this, [this] { refreshRecent(); });

	connect(m_, &ClipManager::clipQueued, this, [this](int s) {
		busy_[s]++;
		refreshClipStates();
		toast_->showMessage(Toast::Kind::Progress,
				    QString("Saving %1 clip").arg(ClipManager::lengthLabel(s).remove(' ')));
	});
	connect(m_, &ClipManager::clipProgress, this, [this](int s, double p) {
		toast_->showMessage(Toast::Kind::Progress,
				    QString("Trimming %1 clip").arg(ClipManager::lengthLabel(s).remove(' ')));
		toast_->setProgress(p);
	});
	connect(m_, &ClipManager::clipSaved, this, [this](int s, const QString &path, const QString &note) {
		if (busy_.value(s) > 0 && --busy_[s] == 0)
			busy_.remove(s);
		refreshClipStates();
		toast_->showMessage(note.isEmpty() ? Toast::Kind::Success : Toast::Kind::Warning,
				    QString("Saved %1 clip").arg(ClipManager::lengthLabel(s).remove(' ')), note, "Show",
				    [this, path] { reveal(path); }, 20000);
	});
	connect(m_, &ClipManager::clipFailed, this, [this](int s, const QString &error) {
		if (busy_.value(s) > 0 && --busy_[s] == 0)
			busy_.remove(s);
		refreshClipStates();
		toast_->showMessage(Toast::Kind::Error,
				    QString("Couldn't save %1 clip").arg(ClipManager::lengthLabel(s).remove(' ')), error);
	});
	connect(m_, &ClipManager::bufferOffNotice, this, [this](int) {
		if (!m_->bufferEnabled()) {
			toast_->showMessage(Toast::Kind::Warning, "Replay buffer is disabled",
					    "Turn it on in OBS Settings > Output > Replay Buffer.", {}, {}, 10000);
			return;
		}
		toast_->showMessage(Toast::Kind::Warning, "Replay buffer is off", {}, "Start",
				    [this] {
					    m_->startBuffer();
					    toast_->setVisible(false);
				    },
				    10000);
	});

	connect(m_->updater(), &Updater::stateChanged, this, [this] { refreshUpdate(); });
	connect(m_->updater(), &Updater::settingsChanged, this, [this] { refreshUpdate(); });

	tick_.setInterval(10000);
	connect(&tick_, &QTimer::timeout, this, [this] {
		refreshBuffer(); // picks up replay-time changes made in OBS's settings
		refreshRecent(); // "12 min ago"
	});
	tick_.start();

	rebuildClipButtons();
	rebuildChips();
	refreshSettings();
	refreshBuffer();
	refreshRecent();
	refreshUpdate();

	if (!m_->updater()->justUpdatedFrom().isEmpty() && m_->updater()->state() == Updater::State::Idle) {
		updateBox_->setVisible(true);
		updateToast_->showMessage(Toast::Kind::Success, "Updated to " + Updater::currentVersion());
		QTimer::singleShot(15000, this, [this] {
			if (m_->updater()->state() == Updater::State::Idle)
				updateBox_->setVisible(false);
		});
	}
}

void ClipsDock::refreshUpdate()
{
	Updater *u = m_->updater();
	const QString current = Updater::currentVersion();
	const QString latest = u->latestVersion();
	QString status = "Version " + current + ".";
	bool banner = true;

	switch (u->state()) {
	case Updater::State::Available:
		status += " Version " + latest + " is available.";
		updateToast_->showMessage(Toast::Kind::Info, "Update " + latest + " available", {}, "Install",
					  [u] { u->download(); });
		break;
	case Updater::State::Downloading:
		status += " Downloading " + latest + "...";
		updateToast_->showMessage(Toast::Kind::Progress, "Downloading update " + latest);
		updateToast_->setProgress(u->progress());
		break;
	case Updater::State::Ready:
		status += " Version " + latest + " installs when you close OBS.";
		updateToast_->showMessage(Toast::Kind::Success, "Update " + latest + " ready",
					  "Installs when you close OBS.");
		break;
	case Updater::State::Error:
		status += " " + u->error();
		updateToast_->showMessage(Toast::Kind::Error, "Update failed", u->error(), "Retry",
					  [u] { u->check(true); });
		break;
	case Updater::State::Checking:
		status += " Checking for updates...";
		banner = false;
		break;
	case Updater::State::UpToDate:
		status += " You're up to date.";
		banner = false;
		break;
	case Updater::State::Idle:
		banner = false;
		break;
	}
	if (banner || u->state() != Updater::State::Idle)
		updateBox_->setVisible(banner);

	updateStatus_->setText(status);
	tokenEdit_->setPlaceholderText(u->hasStoredToken() ? "Token saved (hidden)" : "Override token (optional)");
	if (auto *clear = findChild<QWidget *>("ICClearToken"))
		clear->setVisible(u->hasStoredToken());
}

void ClipsDock::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), ground);
}

void ClipsDock::showEvent(QShowEvent *)
{
	refreshBuffer();
	refreshHotkeys();
	refreshRecent();
}

/* ------------------------------------------------------------------------ */
/* Main view                                                                 */

QWidget *ClipsDock::buildMain()
{
	auto *page = new QWidget;
	page->setObjectName("ICPage");
	auto *v = new QVBoxLayout(page);
	v->setContentsMargins(padding, padding, padding, padding);
	v->setSpacing(0);

	// Header
	auto *header = row();
	auto *h = new QHBoxLayout(header);
	h->setContentsMargins(0, 0, 0, 0);
	h->addWidget(makeLabel("Clips", makeFont(26, 700, Face::Display), textColor));
	h->addStretch(1);
	bufferBtn_ = new PillButton("Start", PillButton::Kind::Primary, PillButton::Lead::Dot);
	connect(bufferBtn_, &QAbstractButton::clicked, this, [this] {
		if (m_->bufferActive())
			m_->stopBuffer();
		else
			m_->startBuffer();
	});
	h->addWidget(bufferBtn_);
	v->addWidget(header);
	v->addSpacing(16);

	updateBox_ = row();
	auto *ub = new QVBoxLayout(updateBox_);
	ub->setContentsMargins(0, 0, 0, 12);
	updateToast_ = new Toast;
	ub->addWidget(updateToast_);
	updateBox_->setVisible(false);
	v->addWidget(updateBox_);

	status_ = new StatusCard;
	v->addWidget(status_);

	offHint_ = hintLabel("Start the replay buffer to save clips. Hotkeys will remind you if it's off.");
	v->addSpacing(12);
	v->addWidget(offHint_);
	v->addSpacing(18);

	v->addWidget(sectionLabel("Save last"));
	v->addSpacing(10);
	auto *gridBox = row();
	grid_ = new QGridLayout(gridBox);
	grid_->setContentsMargins(0, 0, 0, 0);
	grid_->setHorizontalSpacing(10);
	grid_->setVerticalSpacing(10);
	grid_->setColumnStretch(0, 1);
	grid_->setColumnStretch(1, 1);
	v->addWidget(gridBox);

	v->addSpacing(16);
	toast_ = new Toast;
	v->addWidget(toast_);

	recentSection_ = row();
	auto *rv = new QVBoxLayout(recentSection_);
	rv->setContentsMargins(0, 18, 0, 0);
	rv->setSpacing(10);
	auto *recentHeader = row();
	auto *rh = new QHBoxLayout(recentHeader);
	rh->setContentsMargins(0, 0, 0, 0);
	rh->addWidget(sectionLabel("Recent"));
	rh->addStretch(1);
	auto *openFolder = new PillButton("Open folder", PillButton::Kind::Link);
	connect(openFolder, &QAbstractButton::clicked, this, [this] { open(m_->outputDir()); });
	rh->addWidget(openFolder);
	rv->addWidget(recentHeader);
	recent_ = new RecentList;
	recent_->onOpen = [this](const QString &path) { open(path); };
	recent_->onReveal = [this](const QString &path) { reveal(path); };
	rv->addWidget(recent_);
	v->addWidget(recentSection_);

	v->addStretch(1);
	v->addSpacing(20);
	auto *settings = new SettingsEntry;
	connect(settings, &QAbstractButton::clicked, this, [this] {
		refreshSettings();
		stack_->setCurrentIndex(1);
	});
	v->addWidget(settings);

	return scrollPage(page);
}

void ClipsDock::rebuildClipButtons()
{
	for (ClipButton *b : clipButtons_)
		b->deleteLater();
	clipButtons_.clear();
	while (grid_->count())
		delete grid_->takeAt(0);

	QList<int> lengths = m_->lengths();
	for (int i = 0; i < lengths.size(); i++) {
		auto *b = new ClipButton(lengths[i]);
		int seconds = lengths[i];
		connect(b, &QAbstractButton::clicked, this, [this, seconds] { m_->requestClip(seconds); });
		bool lastOdd = i == lengths.size() - 1 && lengths.size() % 2 == 1;
		b->setWide(lastOdd);
		if (lastOdd)
			grid_->addWidget(b, i / 2, 0, 1, 2);
		else
			grid_->addWidget(b, i / 2, i % 2);
		clipButtons_.append(b);
	}
	refreshHotkeys();
	refreshClipStates();
}

void ClipsDock::refreshHotkeys()
{
	for (ClipButton *b : clipButtons_)
		b->setHotkey(m_->hotkeyText(b->seconds()));
}

void ClipsDock::refreshClipStates()
{
	bool active = m_->bufferActive();
	int max = m_->bufferMaxSeconds();
	for (ClipButton *b : clipButtons_) {
		b->setActive(busy_.contains(b->seconds()));
		b->setTooLong(max > 0 && b->seconds() > max);
		b->setDimmed(!active);
	}
}

void ClipsDock::refreshBuffer()
{
	bool active = m_->bufferActive();
	bool enabled = m_->bufferEnabled();
	int max = m_->bufferMaxSeconds();

	if (active) {
		status_->setState(true, "Buffer live", max > 0 ? "holds " + clock(max) : QString());
		bufferBtn_->setText("Stop");
		bufferBtn_->setKind(PillButton::Kind::Secondary, PillButton::Lead::Stop);
	} else {
		status_->setState(false, enabled ? "Buffer off" : "Buffer disabled", max > 0 ? "max " + clock(max) : QString());
		bufferBtn_->setText("Start");
		bufferBtn_->setKind(PillButton::Kind::Primary, PillButton::Lead::Dot);
	}
	bufferBtn_->setEnabled(active || enabled);
	bufferBtn_->setToolTip(enabled ? QString() : "Turn on the replay buffer in OBS Settings > Output first");

	offHint_->setText(enabled ? "Start the replay buffer to save clips. Hotkeys will remind you if it's off."
				  : "The replay buffer is turned off for this profile. Enable it in OBS Settings > "
				    "Output > Replay Buffer.");
	offHint_->setVisible(!active);
	refreshClipStates();
}

void ClipsDock::refreshRecent()
{
	QList<RecentClip> clips = m_->recentClips(5);
	recent_->setClips(clips);
	recentSection_->setVisible(!clips.isEmpty());
}

void ClipsDock::reveal(const QString &path)
{
	QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
}

void ClipsDock::open(const QString &path)
{
	QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

/* ------------------------------------------------------------------------ */
/* Settings view                                                             */

QWidget *ClipsDock::buildSettings()
{
	auto *page = new QWidget;
	page->setObjectName("ICPage");
	auto *v = new QVBoxLayout(page);
	v->setContentsMargins(padding, padding - 4, padding, padding);
	v->setSpacing(0);

	// Header: back button on the left, title centred over the full width.
	auto *header = row();
	auto *hg = new QGridLayout(header);
	hg->setContentsMargins(0, 0, 0, 0);
	auto *back = new PillButton("Clips", PillButton::Kind::Ghost, PillButton::Lead::Back);
	connect(back, &QAbstractButton::clicked, this, [this] { stack_->setCurrentIndex(0); });
	hg->addWidget(makeLabel("Settings", makeFont(20, 700, Face::Display), textColor), 0, 0, Qt::AlignCenter);
	hg->addWidget(back, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
	v->addWidget(header);
	v->addSpacing(18);

	// Clip lengths
	v->addWidget(sectionLabel("Clip lengths"));
	v->addSpacing(10);
	chipsBox_ = row();
	chipsFlow_ = new FlowLayout(chipsBox_, 8, 8);
	v->addWidget(chipsBox_);
	v->addSpacing(10);
	v->addWidget(hintLabel("Each length gets its own hotkey in OBS Settings > Hotkeys."));
	v->addSpacing(20);

	// Output format
	v->addWidget(sectionLabel("Output format"));
	v->addSpacing(10);
	formatSeg_ = new Segmented({"MP4", "MKV", "MOV"});
	formatSeg_->onChanged = [this](int i) {
		static const char *formats[] = {"mp4", "mkv", "mov"};
		m_->setFormat(formats[i]);
	};
	v->addWidget(formatSeg_);
	v->addSpacing(10);
	v->addWidget(hintLabel("Clips are remuxed while trimming, so OBS's own auto-remux can stay off."));
	remuxHint_ = makeLabel({}, makeFont(13, 400), warn);
	remuxHint_->setWordWrap(true);
	v->addSpacing(6);
	v->addWidget(remuxHint_);
	v->addSpacing(20);

	// Trim mode
	v->addWidget(sectionLabel("Trim mode"));
	v->addSpacing(10);
	trimSeg_ = new Segmented({"Fast", "Precise"});
	trimSeg_->onChanged = [this](int i) { m_->setTrimMode(i == 1 ? trimmer::Mode::Precise : trimmer::Mode::Fast); };
	v->addWidget(trimSeg_);
	v->addSpacing(10);
	v->addWidget(hintLabel("Fast cuts on the nearest keyframe, instantly. Precise is frame-exact but re-encodes."));
	v->addSpacing(20);

	// Toggles
	auto *card = new Card;
	deleteToggle_ = new Toggle;
	connect(deleteToggle_, &QAbstractButton::toggled, this, [this](bool on) { m_->setDeleteOriginal(on); });
	soundToggle_ = new Toggle;
	connect(soundToggle_, &QAbstractButton::toggled, this, [this](bool on) { m_->setSoundOnSave(on); });
	card->addRow(toggleRow("Delete full-length original", deleteToggle_));
	card->addRow(toggleRow("Sound on save", soundToggle_));
	v->addWidget(card);
	v->addSpacing(20);

	// Output folder
	v->addWidget(sectionLabel("Save to"));
	v->addSpacing(10);
	auto *pathRow = row();
	auto *ph = new QHBoxLayout(pathRow);
	ph->setContentsMargins(0, 0, 0, 0);
	ph->setSpacing(10);
	pathEdit_ = new QLineEdit;
	pathEdit_->setObjectName("ICPath");
	pathEdit_->setMinimumWidth(60);
	connect(pathEdit_, &QLineEdit::editingFinished, this, [this] {
		QString dir = QDir::fromNativeSeparators(pathEdit_->text().trimmed());
		if (dir != m_->outputDirSetting())
			m_->setOutputDir(dir);
	});
	auto *browse = new PillButton("Browse", PillButton::Kind::Secondary);
	connect(browse, &QAbstractButton::clicked, this, [this] {
		QString dir = QFileDialog::getExistingDirectory(this, "Save clips to", m_->outputDir());
		if (!dir.isEmpty())
			m_->setOutputDir(QDir::fromNativeSeparators(dir));
	});
	ph->addWidget(pathEdit_, 1);
	ph->addWidget(browse);
	v->addWidget(pathRow);
	v->addSpacing(8);
	v->addWidget(hintLabel("Leave empty to use OBS's recording folder."));
	v->addSpacing(20);

	// Updates
	auto *updHeader = row();
	auto *uh = new QHBoxLayout(updHeader);
	uh->setContentsMargins(0, 0, 0, 0);
	uh->addWidget(sectionLabel("Updates"));
	uh->addStretch(1);
	auto *checkNow = new PillButton("Check now", PillButton::Kind::Link);
	connect(checkNow, &QAbstractButton::clicked, this, [this] { m_->updater()->check(true); });
	uh->addWidget(checkNow);
	v->addWidget(updHeader);
	v->addSpacing(8);
	updateStatus_ = hintLabel({});
	v->addWidget(updateStatus_);
	v->addSpacing(10);
	tokenEdit_ = new QLineEdit;
	tokenEdit_->setObjectName("ICPath");
	tokenEdit_->setEchoMode(QLineEdit::Password);
	tokenEdit_->setMinimumWidth(60);
	connect(tokenEdit_, &QLineEdit::editingFinished, this, [this] {
		QString token = tokenEdit_->text().trimmed();
		if (token.isEmpty())
			return;
		m_->updater()->setToken(token);
		tokenEdit_->clear();
		refreshUpdate();
		m_->updater()->check(true);
	});
	v->addWidget(tokenEdit_);
	v->addSpacing(8);
	v->addWidget(hintLabel(QString("Updates from %1 use the access built into this plugin. Paste a token only to "
				       "override it; it's stored encrypted for your Windows account.")
				       .arg(Updater::repo())));
	auto *clearToken = new PillButton("Forget saved token", PillButton::Kind::Link);
	connect(clearToken, &QAbstractButton::clicked, this, [this] {
		m_->updater()->setToken({});
		refreshUpdate();
	});
	clearToken->setObjectName("ICClearToken");
	v->addSpacing(4);
	v->addWidget(clearToken, 0, Qt::AlignLeft);

	v->addStretch(1);
	return scrollPage(page);
}

void ClipsDock::rebuildChips()
{
	while (QLayoutItem *item = chipsFlow_->takeAt(0)) {
		if (item->widget())
			item->widget()->deleteLater();
		delete item;
	}

	for (int s : m_->lengths()) {
		auto *chip = new Chip(ClipManager::lengthLabel(s).remove(' '), chipsBox_);
		connect(chip, &QAbstractButton::clicked, this, [this, s] { m_->removeLength(s); });
		chipsFlow_->addWidget(chip);
	}

	addChip_ = new AddChip(chipsBox_);
	addChip_->onSubmit = [this](const QString &text) {
		int s = parseLength(text);
		return s > 0 && m_->addLength(s);
	};
	addChip_->setVisible(m_->lengths().size() < 12);
	chipsFlow_->addWidget(addChip_);
	chipsBox_->updateGeometry();
}

void ClipsDock::refreshSettings()
{
	const QString fmt = m_->format();
	formatSeg_->setCurrent(fmt == "mkv" ? 1 : fmt == "mov" ? 2 : 0);
	trimSeg_->setCurrent(m_->trimMode() == trimmer::Mode::Precise ? 1 : 0);

	QSignalBlocker b1(deleteToggle_), b2(soundToggle_);
	deleteToggle_->setChecked(m_->deleteOriginal());
	soundToggle_->setChecked(m_->soundOnSave());

	if (!pathEdit_->hasFocus())
		pathEdit_->setText(QDir::toNativeSeparators(m_->outputDirSetting()));
	pathEdit_->setPlaceholderText(QDir::toNativeSeparators(m_->outputDir()));

	bool remux = m_->obsAutoRemux();
	remuxHint_->setVisible(remux);
	remuxHint_->setText(m_->deleteOriginal()
				    ? "OBS auto-remux is on, so OBS also writes a full-length MP4 of each replay. "
				      "Clips deletes it along with the original."
				    : "OBS auto-remux is on, so OBS also writes a full-length MP4 of each replay. "
				      "Turn it off in OBS Settings > Advanced > Recording.");
}

int ClipsDock::parseLength(const QString &input)
{
	QString s = input.toLower().remove(' ');
	if (s.isEmpty())
		return 0;

	QRegularExpressionMatch m;
	if ((m = QRegularExpression(R"(^(\d+)s?$)").match(s)).hasMatch())
		return m.captured(1).toInt();
	if ((m = QRegularExpression(R"(^(\d+):(\d{1,2})$)").match(s)).hasMatch())
		return m.captured(1).toInt() * 60 + m.captured(2).toInt();
	if ((m = QRegularExpression(R"(^(\d+(?:\.\d+)?)m(?:in)?$)").match(s)).hasMatch())
		return (int)std::lround(m.captured(1).toDouble() * 60);
	if ((m = QRegularExpression(R"(^(\d+)m(?:in)?(\d+)s?$)").match(s)).hasMatch())
		return m.captured(1).toInt() * 60 + m.captured(2).toInt();
	return 0;
}
