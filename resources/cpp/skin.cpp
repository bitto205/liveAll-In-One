#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPixmap>
#include <QScreen>

#include <algorithm>
#include <cmath>

namespace liveaio::resources {

struct RoleTextStyle {
    QString fontFamily = QStringLiteral("Microsoft YaHei");
    int pixelSize = 13;
    bool bold = false;
    QString align = QStringLiteral("center");
    QString vAlign = QStringLiteral("vcenter");
    bool wordWrap = false;
    int minPx = 8;
    int maxPx = 13;
    QString fitRef;
    QString styleExtra;
    int minChars = 4;
    int maxChars = 16;
    int slackPx = 2;

    static RoleTextStyle fromObject(const QJsonObject& raw) {
        RoleTextStyle s;
        if (raw.isEmpty()) return s;
        s.fontFamily = raw.value(QStringLiteral("font_family")).toString(s.fontFamily);
        s.pixelSize = raw.value(QStringLiteral("pixel_size")).toInt(s.pixelSize);
        s.bold = raw.value(QStringLiteral("bold")).toBool(false);
        s.align = raw.value(QStringLiteral("align")).toString(s.align);
        s.vAlign = raw.value(QStringLiteral("v_align")).toString(s.vAlign);
        s.wordWrap = raw.value(QStringLiteral("word_wrap")).toBool(false);
        s.minPx = raw.value(QStringLiteral("min_px")).toInt(8);
        s.maxPx = raw.value(QStringLiteral("max_px")).toInt(raw.value(QStringLiteral("pixel_size")).toInt(13));
        s.fitRef = raw.value(QStringLiteral("fit_ref")).toString();
        s.styleExtra = raw.value(QStringLiteral("style_extra")).toString();
        s.minChars = raw.value(QStringLiteral("min_chars")).toInt(4);
        s.maxChars = raw.value(QStringLiteral("max_chars")).toInt(16);
        s.slackPx = raw.value(QStringLiteral("slack_px")).toInt(2);
        return s;
    }

    Qt::Alignment qtAlign() const {
        Qt::Alignment h = Qt::AlignHCenter;
        if (align == QLatin1String("left")) h = Qt::AlignLeft;
        else if (align == QLatin1String("right")) h = Qt::AlignRight;
        Qt::Alignment v = Qt::AlignVCenter;
        if (vAlign == QLatin1String("top")) v = Qt::AlignTop;
        else if (vAlign == QLatin1String("bottom")) v = Qt::AlignBottom;
        return h | v;
    }

    QFont font(int px = -1) const {
        QFont f(fontFamily);
        f.setPixelSize(px > 0 ? px : pixelSize);
        if (bold) f.setBold(true);
        return f;
    }
};

struct SkinMetrics {
    int fadeW = 22;
    int padH = 12;
    int padV = 7;
    int giftIconSize = 32;
    int giftIconGap = 8;
};

static qreal screenDpr() {
    if (auto* s = QApplication::primaryScreen()) return s->devicePixelRatio();
    return 1.0;
}

static QColor parseColor(const QJsonValue& raw, const QColor& fallback = QColor(255, 255, 255)) {
    if (raw.isString()) {
        QColor c(raw.toString());
        return c.isValid() ? c : fallback;
    }
    if (raw.isArray()) {
        const auto a = raw.toArray();
        if (a.size() >= 3) {
            return QColor(a[0].toInt(), a[1].toInt(), a[2].toInt(),
                          a.size() > 3 ? a[3].toInt(255) : 255);
        }
    }
    return fallback;
}

static int fitFontPixelSize(const QString& text, int maxW, int maxH,
                            const QString& family, bool bold, int maxPx, int minPx = 8) {
    maxW = std::max(1, maxW - 2);
    maxH = std::max(1, maxH - 2);
    int lo = minPx;
    int hi = std::max(8, maxH);
    if (maxPx > 0) hi = std::min(hi, maxPx);
    if (lo > hi) return std::max(minPx, hi);
    int best = lo;
    const QString sample = text.isEmpty() ? QStringLiteral("国") : text;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        QFont f(family);
        f.setPixelSize(mid);
        if (bold) f.setBold(true);
        QFontMetrics fm(f);
        if (fm.horizontalAdvance(sample) <= maxW && fm.height() <= maxH) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

struct LoadedStill {
    QPixmap pixmap;
    int logicalW = 0;
    int logicalH = 0;
};

struct LoadedAnim {
    QVector<QPixmap> frames;
    QVector<int> delays;
    int logicalW = 0;
    int logicalH = 0;
};

static LoadedStill loadStillPath(const QString& path, int logicalH, qreal dpr = 0, qreal uiScale = 1.0) {
    if (dpr <= 0) dpr = screenDpr();
    const int lh = std::max(1, int(std::lround(std::max(1, logicalH) * std::max(0.25, uiScale))));
    LoadedStill out;
    out.logicalH = lh;
    out.logicalW = lh;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) return out;
    const int lw = std::max(1, int(std::lround(img.width() * double(lh) / std::max(1, img.height()))));
    const int pw = std::max(1, int(std::lround(lw * dpr)));
    const int ph = std::max(1, int(std::lround(lh * dpr)));
    img = img.scaled(pw, ph, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    out.pixmap = QPixmap::fromImage(img);
    out.pixmap.setDevicePixelRatio(dpr);
    out.logicalW = lw;
    return out;
}

static LoadedAnim loadAnimPath(const QString& path, int logicalH, qreal dpr = 0, qreal uiScale = 1.0) {
    if (dpr <= 0) dpr = screenDpr();
    const int lh = std::max(1, int(std::lround(std::max(1, logicalH) * std::max(0.25, uiScale))));
    LoadedAnim out;
    out.logicalH = lh;
    out.logicalW = lh;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        // Fallback to still.
        auto still = loadStillPath(path, logicalH, dpr, uiScale);
        if (!still.pixmap.isNull()) {
            out.frames = {still.pixmap};
            out.delays = {100};
            out.logicalW = still.logicalW;
        }
        return out;
    }
    int i = 0;
    while (true) {
        QImage img = reader.read();
        if (img.isNull()) break;
        const int lw = std::max(1, int(std::lround(img.width() * double(lh) / std::max(1, img.height()))));
        const int pw = std::max(1, int(std::lround(lw * dpr)));
        const int ph = std::max(1, int(std::lround(lh * dpr)));
        img = img.scaled(pw, ph, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QPixmap pm = QPixmap::fromImage(img);
        pm.setDevicePixelRatio(dpr);
        out.frames.push_back(pm);
        out.delays.push_back(std::max(30, reader.nextImageDelay() > 0 ? reader.nextImageDelay() : 100));
        out.logicalW = lw;
        ++i;
        if (!reader.jumpToImage(i)) break;
        if (i > 256) break;
    }
    if (out.frames.isEmpty()) {
        auto still = loadStillPath(path, logicalH, dpr, uiScale);
        if (!still.pixmap.isNull()) {
            out.frames = {still.pixmap};
            out.delays = {100};
            out.logicalW = still.logicalW;
        }
    }
    return out;
}

class ToolSkin {
public:
    QString toolId;
    QString skinId = QStringLiteral("default");
    QString name = QStringLiteral("默认");
    QString rootPath;
    QJsonObject meta;

    static ToolSkin load(const QString& appRoot, const QString& tool, const QString& skin = QStringLiteral("default")) {
        ToolSkin s;
        s.toolId = tool;
        s.skinId = skin;
        s.rootPath = QDir(appRoot).filePath(QStringLiteral("resources/skin/%1/%2").arg(tool, skin));
        QFile f(QDir(s.rootPath).filePath(QStringLiteral("skin.json")));
        if (f.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) s.meta = doc.object();
        }
        s.name = s.meta.value(QStringLiteral("name")).toString(skin);
        return s;
    }

    SkinMetrics metrics() const {
        SkinMetrics m;
        const auto raw = meta.value(QStringLiteral("metrics")).toObject();
        m.fadeW = raw.value(QStringLiteral("fade_w")).toInt(22);
        m.padH = raw.value(QStringLiteral("pad_h")).toInt(12);
        m.padV = raw.value(QStringLiteral("pad_v")).toInt(7);
        m.giftIconSize = raw.value(QStringLiteral("gift_icon_size")).toInt(32);
        m.giftIconGap = raw.value(QStringLiteral("gift_icon_gap")).toInt(8);
        return m;
    }

    QJsonObject surfaceLayout(const QString& surface) const {
        return meta.value(QStringLiteral("layouts")).toObject().value(surface).toObject();
    }

    RoleTextStyle roleStyle(const QString& surface, const QString& role) const {
        const auto roles = surfaceLayout(surface).value(QStringLiteral("roles")).toObject();
        return RoleTextStyle::fromObject(roles.value(role).toObject());
    }

    int fitRole(const QString& surface, const QString& role, const QString& text,
                int maxW, int maxH, qreal scale = 1.0) const {
        const auto st = roleStyle(surface, role);
        const int cap = std::max(st.minPx, int(std::lround(st.maxPx * scale)));
        const QString sample = text.isEmpty() ? (st.fitRef.isEmpty() ? QStringLiteral("国") : st.fitRef) : text;
        return fitFontPixelSize(sample, maxW, maxH, st.fontFamily, st.bold, cap, st.minPx);
    }

    QColor color(const QString& key, const QColor& fallback = QColor(255, 255, 255)) const {
        return parseColor(meta.value(QStringLiteral("colors")).toObject().value(key), fallback);
    }

    void paintTextShadow(QPainter* p, const QRect& rect, const QString& text, const RoleTextStyle& style, int px) const {
        const QColor fill = color(QStringLiteral("text_fill"), QColor(255, 255, 255));
        const QColor shadow = color(QStringLiteral("text_shadow"), QColor(0, 0, 0, 160));
        p->setFont(style.font(px));
        p->setPen(shadow);
        p->drawText(rect.translated(1, 1), style.qtAlign(), text);
        p->setPen(fill);
        p->drawText(rect, style.qtAlign(), text);
    }

    LoadedStill presentImage(const QString& path, int boxH, qreal uiScale = 1.0) const {
        return loadStillPath(path, boxH, screenDpr(), uiScale);
    }
};

struct SkinEntry {
    QString id;
    QString name;
};

// 皮肤目录：resources/skin/<tool>/<id>/skin.json。始终保证有 default 一项。
static QVector<SkinEntry> listSkins(const QString& appRoot, const QString& tool) {
    QVector<SkinEntry> out;
    const QDir toolDir(QDir(appRoot).filePath(QStringLiteral("resources/skin/%1").arg(tool)));
    const QStringList ids = toolDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& id : ids) {
        QString name = id;
        QFile f(toolDir.filePath(id + QStringLiteral("/skin.json")));
        if (f.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) {
                name = doc.object().value(QStringLiteral("name")).toString(id);
            }
        }
        out.append(SkinEntry{id, name});
    }
    if (out.isEmpty()) out.append(SkinEntry{QStringLiteral("default"), QStringLiteral("默认")});
    return out;
}

static QString skinConfigKey(const QString& tool) {
    return QStringLiteral("skin.%1").arg(tool);
}

}  // namespace liveaio::resources
