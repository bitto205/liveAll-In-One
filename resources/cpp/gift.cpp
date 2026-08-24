#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace liveaio::resources {

// Gift catalog + icon paths under resources/gift/.
// Go (resources/gift.go) owns business diamonds/id; C++ uses this for UI icons.

inline QString giftDir(const QString& appRoot) {
    return QDir(appRoot).filePath(QStringLiteral("resources/gift"));
}

inline QString giftInfoPath(const QString& appRoot) {
    return QDir(giftDir(appRoot)).filePath(QStringLiteral("gift_info.json"));
}

inline QString giftIconDir(const QString& appRoot) {
    return QDir(giftDir(appRoot)).filePath(QStringLiteral("icon"));
}

inline int giftIdFromCatalog(const QString& appRoot, const QString& giftName) {
    QFile info(giftInfoPath(appRoot));
    if (!info.open(QIODevice::ReadOnly)) return 0;
    const auto doc = QJsonDocument::fromJson(info.readAll());
    return doc.object().value(giftName).toObject().value(QStringLiteral("gift_id")).toInt();
}

inline int giftPriceFromCatalog(const QString& appRoot, const QString& giftName) {
    QFile info(giftInfoPath(appRoot));
    if (!info.open(QIODevice::ReadOnly)) return 0;
    const auto doc = QJsonDocument::fromJson(info.readAll());
    return doc.object().value(giftName).toObject().value(QStringLiteral("price")).toInt();
}

// ResolveIconPath: name.ext then gift_id.ext (same rule as Go resources.IconPath).
inline QString resolveGiftIconPath(const QString& appRoot, const QString& giftName) {
    const QString dir = giftIconDir(appRoot);
    static const QStringList exts = {
        QStringLiteral(".webp"), QStringLiteral(".png"), QStringLiteral(".jpg"),
        QStringLiteral(".jpeg"), QStringLiteral(".gif"),
    };
    for (const auto& ext : exts) {
        const QString p = QDir(dir).filePath(giftName + ext);
        if (QFile::exists(p)) return p;
    }
    const int id = giftIdFromCatalog(appRoot, giftName);
    if (id > 0) {
        for (const auto& ext : exts) {
            const QString p = QDir(dir).filePath(QString::number(id) + ext);
            if (QFile::exists(p)) return p;
        }
    }
    return {};
}

// 礼物名列表（gift_info.json 键，按名称排序），只读一次。
inline const QStringList& giftNamesCached(const QString& appRoot) {
    static QStringList names;
    static bool loaded = false;
    if (loaded) return names;
    loaded = true;
    QFile info(giftInfoPath(appRoot));
    if (info.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(info.readAll());
        names = doc.object().keys();
        names.sort();
    }
    return names;
}

// 按边长缓存礼物图标，避免每次重建规则格都重新解码。
inline QPixmap loadGiftPixmap(const QString& appRoot, const QString& giftName, int side) {
    if (giftName.isEmpty() || side <= 0) return {};
    static QHash<QString, QPixmap> cache;
    const QString key = giftName + QLatin1Char('@') + QString::number(side);
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    QPixmap out;
    const QString path = resolveGiftIconPath(appRoot, giftName);
    if (!path.isEmpty()) {
        QPixmap raw(path);
        if (!raw.isNull()) {
            out = raw.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    cache.insert(key, out);
    return out;
}

}  // namespace liveaio::resources
