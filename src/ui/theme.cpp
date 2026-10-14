#include "theme.hpp"

#include <QByteArray>
#include <QFontDatabase>
#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

#include <obs-module.h>

namespace theme {

void loadFonts()
{
	static bool loaded = false;
	if (loaded)
		return;
	loaded = true;

	const char *files[] = {"fonts/Inter-Regular.ttf",        "fonts/Inter-Medium.ttf",
			       "fonts/Inter-SemiBold.ttf",       "fonts/Inter-Bold.ttf",
			       "fonts/InterDisplay-SemiBold.ttf", "fonts/InterDisplay-Bold.ttf"};
	for (const char *file : files) {
		char *path = obs_module_file(file);
		if (!path) {
			blog(LOG_WARNING, "[instant-clip] missing font %s", file);
			continue;
		}
		if (QFontDatabase::addApplicationFont(QString::fromUtf8(path)) < 0)
			blog(LOG_WARNING, "[instant-clip] couldn't load font %s", path);
		bfree(path);
	}
}

QFont makeFont(int px, int weight, Face face, bool tabular)
{
	QFont f(face == Face::Display ? QStringLiteral("Inter Display") : QStringLiteral("Inter"));
	f.setFamilies({f.family(), QStringLiteral("Inter"), QStringLiteral("Segoe UI")});
	f.setPixelSize(px);
	f.setWeight(QFont::Weight(weight));
	f.setHintingPreference(QFont::PreferNoHinting);
	f.setStyleStrategy(QFont::PreferAntialias);
	if (tabular)
		f.setFeature(QFont::Tag("tnum"), 1);
	return f;
}

QColor mix(const QColor &a, const QColor &b, double t)
{
	return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
				a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

namespace {

// 24x24 stroke icons, drawn for this plugin.
const char *iconBody(Icon icon)
{
	switch (icon) {
	case Icon::Play:
		return R"(<path d="M8 5.8v12.4a.8.8 0 0 0 1.2.7l9.6-6.2a.8.8 0 0 0 0-1.4L9.2 5.1A.8.8 0 0 0 8 5.8z"/>)";
	case Icon::External:
		return R"(<path d="M14 4h6v6"/><path d="M20 4l-8.5 8.5"/><path d="M18 14v4.5a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 4 18.5v-11A1.5 1.5 0 0 1 5.5 6H10"/>)";
	case Icon::Check:
		return R"(<path d="M5 12.5l4.5 4.5L19 7.5"/>)";
	case Icon::Sliders:
		return R"(<path d="M4 8h8"/><path d="M18 8h2"/><path d="M4 16h2"/><path d="M12 16h8"/><circle cx="15" cy="8" r="2.5"/><circle cx="9" cy="16" r="2.5"/>)";
	case Icon::ChevronDown:
		return R"(<path d="M6 9.5l6 6 6-6"/>)";
	case Icon::ChevronLeft:
		return R"(<path d="M14.5 6l-6 6 6 6"/>)";
	case Icon::Stop:
		return R"(<rect x="6.5" y="6.5" width="11" height="11" rx="2.5"/>)";
	case Icon::Warning:
		return R"(<path d="M10.3 4.6L2.9 17.5A2 2 0 0 0 4.6 20.5h14.8a2 2 0 0 0 1.7-3L13.7 4.6a2 2 0 0 0-3.4 0z"/><path d="M12 10v4"/><path d="M12 17.2v.1"/>)";
	case Icon::Close:
		return R"(<path d="M7 7l10 10"/><path d="M17 7L7 17"/>)";
	case Icon::Folder:
		return R"(<path d="M3.5 7.5A1.5 1.5 0 0 1 5 6h4.2l2 2.2H19a1.5 1.5 0 0 1 1.5 1.5v8.3A1.5 1.5 0 0 1 19 19.5H5A1.5 1.5 0 0 1 3.5 18z"/>)";
	case Icon::Download:
		return R"(<path d="M12 4v11"/><path d="M7 10.5l5 5 5-5"/><path d="M5 20h14"/>)";
	case Icon::Spinner:
		return R"(<path d="M12 3.5a8.5 8.5 0 1 0 8.5 8.5"/>)";
	}
	return "";
}

} // namespace

void drawIcon(QPainter &p, Icon icon, const QRectF &rect, const QColor &color)
{
	static QHash<QString, QSvgRenderer *> cache;
	QString key = QString("%1/%2").arg((int)icon).arg(color.name(QColor::HexArgb));
	QSvgRenderer *r = cache.value(key);
	if (!r) {
		QByteArray svg = QString(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" )"
					 R"(stroke="%1" stroke-opacity="%2" stroke-width="2" stroke-linecap="round" )"
					 R"(stroke-linejoin="round">%3</svg>)")
					 .arg(color.name(QColor::HexRgb))
					 .arg(color.alphaF())
					 .arg(iconBody(icon))
					 .toUtf8();
		r = new QSvgRenderer(svg);
		cache.insert(key, r);
	}
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	r->render(&p, rect);
	p.restore();
}

} // namespace theme
