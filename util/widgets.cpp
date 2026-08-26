// util/widgets.cpp — 共享主题与主题联动控件库（pages 与 tools 共用同一份实现）。
//
// 视觉基线为旧 PySide 版本的 util/theme.py + util/widgets.py + pages/main_page.py。
// 单 TU 编译：pages_main.cpp / tools_main.cpp 直接 include 本文件。
#ifndef LIVEAIO_UTIL_WIDGETS_CPP
#define LIVEAIO_UTIL_WIDGETS_CPP

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRectF>
#include <QScrollArea>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantAnimation>
#include <QVector>
#include <QWidget>
#include <QtConcurrent>
#include <QtGlobal>

#include <functional>
#include <memory>

#if defined(QT_OPENGLWIDGETS_LIB)
#include <QOpenGLWidget>
#define LIVEAIO_HAS_OPENGLWIDGET 1
#endif

namespace liveaio::util {

// ─────────────────────────────────────────────
// 主题：旧 util/theme.py 的四套精确色值
// ─────────────────────────────────────────────
struct ThemePalette {
    QString bg;
    QString sidebar;
    QString card;
    QString hover;
    QString active;
    QString activeLine;
    QString text;
    QString textMuted;
    QString border;
    QString winEdge;
    QString closeHover;
    QString btnHover;
};

inline const QVector<QPair<QString, ThemePalette>>& themeCatalog() {
    static const QVector<QPair<QString, ThemePalette>> catalog = {
        {QStringLiteral("拿铁奶咖"), ThemePalette{
             QStringLiteral("#eff1f5"), QStringLiteral("#e6e9ef"), QStringLiteral("#ffffff"),
             QStringLiteral("#dce0e8"), QStringLiteral("#ccd0da"), QStringLiteral("#1e66f5"),
             QStringLiteral("#4c4f69"), QStringLiteral("#5c5f77"), QStringLiteral("#ccd0da"),
             QStringLiteral("#bcc0cc"), QStringLiteral("#d20f39"), QStringLiteral("#ccd0da")}},
        {QStringLiteral("深焙摩卡"), ThemePalette{
             QStringLiteral("#1e1e2e"), QStringLiteral("#181825"), QStringLiteral("#313244"),
             QStringLiteral("#45475a"), QStringLiteral("#585b70"), QStringLiteral("#89b4fa"),
             QStringLiteral("#cdd6f4"), QStringLiteral("#a6adc8"), QStringLiteral("#313244"),
             QStringLiteral("#11111b"), QStringLiteral("#f38ba8"), QStringLiteral("#45475a")}},
        {QStringLiteral("极夜深蓝"), ThemePalette{
             QStringLiteral("#2e3440"), QStringLiteral("#252b35"), QStringLiteral("#3b4252"),
             QStringLiteral("#434c5e"), QStringLiteral("#4c566a"), QStringLiteral("#88c0d0"),
             QStringLiteral("#eceff4"), QStringLiteral("#d8dee9"), QStringLiteral("#434c5e"),
             QStringLiteral("#1d2430"), QStringLiteral("#bf616a"), QStringLiteral("#434c5e")}},
        {QStringLiteral("日晷护眼"), ThemePalette{
             QStringLiteral("#fdf6e3"), QStringLiteral("#eee8d5"), QStringLiteral("#ffffff"),
             QStringLiteral("#e5dfc8"), QStringLiteral("#d9d2b5"), QStringLiteral("#268bd2"),
             QStringLiteral("#586e75"), QStringLiteral("#657b83"), QStringLiteral("#e2dcc8"),
             QStringLiteral("#d9d2b5"), QStringLiteral("#dc322f"), QStringLiteral("#e2dcc8")}},
    };
    return catalog;
}

inline QStringList themeNames() {
    QStringList out;
    for (const auto& entry : themeCatalog()) out.append(entry.first);
    return out;
}

inline const QString& defaultThemeName() {
    static const QString name = QStringLiteral("拿铁奶咖");
    return name;
}

// 旧配置里可能存的是 light/dark，迁移到对应主题。
inline QString normalizeThemeName(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("拿铁奶咖");
    }
    if (trimmed.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("深焙摩卡");
    }
    for (const auto& entry : themeCatalog()) {
        if (entry.first == trimmed) return trimmed;
    }
    return defaultThemeName();
}

inline const ThemePalette& paletteByName(const QString& name) {
    const QString resolved = normalizeThemeName(name);
    for (const auto& entry : themeCatalog()) {
        if (entry.first == resolved) return entry.second;
    }
    return themeCatalog().first().second;
}

struct ThemeState {
    QString name = defaultThemeName();
    ThemePalette palette = themeCatalog().first().second;
    std::function<void(const QString&)> persist;
    QVector<QPair<QPointer<QObject>, std::function<void(const QString&)>>> callbacks;
};

inline ThemeState& themeState() {
    static ThemeState state;
    return state;
}

inline const ThemePalette& theme() { return themeState().palette; }
inline QString currentThemeName() { return themeState().name; }

// 主题持久化交给宿主：pages 走 core config.set，tools 走本地 config.json。
inline void setThemePersistHook(std::function<void(const QString&)> hook) {
    themeState().persist = std::move(hook);
}

// ctx 为空时回调会被丢弃，避免窗口销毁后回调野指针。
inline void onThemeChange(QObject* ctx, std::function<void(const QString&)> cb) {
    if (!ctx || !cb) return;
    themeState().callbacks.append({QPointer<QObject>(ctx), std::move(cb)});
}

inline void notifyThemeChanged() {
    auto& list = themeState().callbacks;
    const QString name = themeState().name;
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list[i].first.isNull()) {
            list.removeAt(i);
            continue;
        }
        list[i].second(name);
    }
}

// 只切换当前主题并广播，不写配置（用于从 core 同步下来的值）。
inline void applyThemeName(const QString& name) {
    const QString resolved = normalizeThemeName(name);
    themeState().name = resolved;
    themeState().palette = paletteByName(resolved);
    notifyThemeChanged();
}

// 用户主动切换：写配置 + 广播。
inline void setTheme(const QString& name) {
    const QString resolved = normalizeThemeName(name);
    if (resolved == themeState().name) return;
    themeState().name = resolved;
    themeState().palette = paletteByName(resolved);
    if (themeState().persist) themeState().persist(resolved);
    notifyThemeChanged();
}

// ─────────────────────────────────────────────
// 配置读写：由宿主注入，控件层不直接碰 core / 文件
// ─────────────────────────────────────────────
struct ConfigAccess {
    std::function<QVariant(const QString&, const QVariant&)> get;
    std::function<void(const QString&, const QVariant&)> set;
};

inline ConfigAccess& configAccess() {
    static ConfigAccess access;
    return access;
}

inline void setConfigAccessors(std::function<QVariant(const QString&, const QVariant&)> get,
                              std::function<void(const QString&, const QVariant&)> set) {
    configAccess().get = std::move(get);
    configAccess().set = std::move(set);
}

inline QVariant configGet(const QString& key, const QVariant& fallback = {}) {
    if (configAccess().get) return configAccess().get(key, fallback);
    return fallback;
}

inline void configSet(const QString& key, const QVariant& value) {
    if (configAccess().set) configAccess().set(key, value);
}

// ─────────────────────────────────────────────
// QSS 片段：对应旧 theme.py 的 qss_* 辅助
// ─────────────────────────────────────────────
inline QString qssOutlined(int h = 36) {
    const ThemePalette& C = theme();
    return QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1.5px solid %2;"
        " border-radius: 8px; font-size: 13px; font-weight: 600;"
        " min-height: %3px; padding: 0 16px; }"
        "QPushButton:hover { background: %4; border: 1.5px solid %2; }"
    ).arg(C.card, C.activeLine, QString::number(h), C.hover);
}

// 与 qssLineEdit 同高同边框：并排输入框+按钮时用这个，避免 1.5px/min-height 把按钮撑高。
inline QString qssOutlinedBesideEdit(int h = 36) {
    const ThemePalette& C = theme();
    return QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %2;"
        " border-radius: 6px; font-size: 13px; font-weight: 600;"
        " height: %3px; max-height: %3px; min-height: %3px; padding: 0 14px; }"
        "QPushButton:hover { background: %4; border: 1px solid %2; }"
    ).arg(C.card, C.activeLine, QString::number(h), C.hover);
}

inline QString qssDisabled(int h = 36) {
    const ThemePalette& C = theme();
    return QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 8px;"
        " font-size: 13px; min-height: %3px; padding: 0 16px; }"
    ).arg(C.border, C.textMuted, QString::number(h));
}

inline QString qssSuccess(int h = 36) {
    const ThemePalette& C = theme();
    return QStringLiteral(
        "QPushButton { background: %1; color: #ffffff; border: none; border-radius: 8px;"
        " font-size: 13px; font-weight: 600; min-height: %2px; padding: 0 16px; }"
    ).arg(C.activeLine, QString::number(h));
}

inline QString qssDanger(int h = 36) {
    const ThemePalette& C = theme();
    return QStringLiteral(
        "QPushButton { background: %1; color: #ffffff; border: none; border-radius: 8px;"
        " font-size: 13px; font-weight: 600; min-height: %2px; padding: 0 16px; }"
        "QPushButton:hover { background: %3; }"
    ).arg(C.closeHover, QString::number(h), C.active);
}

inline QString qssBack() {
    const ThemePalette& C = theme();
    return QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none;"
        " font-size: 13px; padding: 4px 8px; }"
        "QPushButton:hover { color: %2; }"
    ).arg(C.textMuted, C.activeLine);
}

inline QString qssMutedLabel(int size = 13) {
    return QStringLiteral("background: transparent; font-size: %1px; color: %2;")
        .arg(QString::number(size), theme().textMuted);
}

inline QString qssErrorLabel(int size = 13) {
    return QStringLiteral("background: transparent; font-size: %1px; color: %2;")
        .arg(QString::number(size), theme().closeHover);
}

inline QString qssAccentLabel(int size = 13) {
    return QStringLiteral("background: transparent; font-size: %1px; color: %2;")
        .arg(QString::number(size), theme().activeLine);
}

inline QString qssLineEdit() {
    const ThemePalette& C = theme();
    return QStringLiteral(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
        " padding: 0 10px; font-size: 13px;"
        " height: 36px; max-height: 36px; min-height: 36px; }"
        "QLineEdit:focus { border-color: %4; }"
    ).arg(C.card, C.text, C.border, C.activeLine);
}

// 主窗口壳几何常量（圆角半径）。
inline constexpr int kWindowShadowMargin = 0;
inline constexpr int kWindowCornerRadius = 10;

// 主窗口壳 QSS：旧 main_page.build_qss + settings_page.build_setting_qss。
inline QString shellQss() {
    const ThemePalette& C = theme();
    return QStringLiteral(
        "QWidget { background: transparent; color: %1;"
        " font-family: 'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif;"
        " font-size: 14px; outline: none; }"
        "#WindowCard { background: %2; border-radius: %12px; border: 1px solid %3; }"
        "#TitleBar { background: transparent; border-radius: %12px %12px 0 0; }"
        "#AppTitle { color: %5; font-size: 12px; background: transparent; }"
        "#WinBtn { background: transparent; border: none; border-radius: 0px; color: %5;"
        " font-size: 13px; min-width: 46px; max-width: 46px;"
        " min-height: 36px; max-height: 36px; }"
        "#WinBtn:hover { background: %6; color: %1; border-radius: 0px; }"
        "#Sidebar { background: transparent; }"
        "#SidebarDivider { background: %3; min-width: 1px; max-width: 1px; }"
        "#ToggleBtn, #NavBtn, #SettingsBtn { background: transparent; border: none;"
        " border-radius: 0px; text-align: left; color: %5; }"
        "#ToggleBtn:hover { background: %8; color: %1; }"
        "#NavBtn:hover, #SettingsBtn:hover { background: %8; color: %1; }"
        "#NavBtn[active=\"true\"], #SettingsBtn[active=\"true\"] { background: %9; color: %1; }"
        "#ScrollContainer { background: transparent; }"
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 4px; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 2px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "#ContentArea { background: transparent; }"
        "#Card { background: %10; border-radius: 10px; border: 1px solid %4; }"
        "#PageTitle { font-size: 22px; font-weight: 600; color: %1; background: transparent; }"
        "#PageSubtitle { font-size: 13px; color: %5; background: transparent; }"
        "#SettingsSidebar { background: %7; border-bottom: 1px solid %4; }"
        "#SettingNavBtn { background: transparent; border: none;"
        " border-bottom: 2px solid transparent; padding: 0 16px; color: %5; font-size: 13px; }"
        "#SettingNavBtn:hover { background: %8; color: %1;"
        " border-bottom: 2px solid transparent; }"
        "#SettingNavBtn[active=\"true\"] { background: transparent; color: %1;"
        " font-weight: 600; border-bottom: 2px solid %11; }"
        "#SettingContent { background: transparent; }"
        "#SettingCard { background: %10; border-radius: 10px; border: 1px solid %4; }"
        "#SettingCardTitle { font-size: 13px; font-weight: 600; color: %5; background: transparent; }"
        "#SettingPageTitle { font-size: 20px; font-weight: 600; color: %1; background: transparent; }"
    ).arg(C.text, C.bg, C.winEdge, C.border, C.textMuted, C.btnHover,
          C.sidebar, C.hover, C.active, C.card, C.activeLine,
          QString::number(kWindowCornerRadius));
}

// 旧 _danmu_spin_qss：自绘上下箭头的紧凑 QSpinBox。
inline QString spinBoxQss() {
    const ThemePalette& C = theme();
    return QStringLiteral(
        "QSpinBox { background: %1; color: %2; border: 1px solid %3; border-radius: 5px;"
        " font-size: 12px; padding: 2px 4px; padding-right: 18px; min-height: 28px; }"
        "QSpinBox QLineEdit { background: %1; color: %2; border: none; padding: 0 2px;"
        " selection-background-color: %4; }"
        "QSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right;"
        " background: %5; border: none; margin: 0; padding: 0; width: 16px; height: 14px;"
        " border-top-right-radius: 4px; }"
        "QSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right;"
        " background: %5; border: none; margin: 0; padding: 0; width: 16px; height: 14px;"
        " border-bottom-right-radius: 4px; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: %6; }"
        "QSpinBox::up-arrow { image: none; width: 0; height: 0;"
        " border-left: 3px solid transparent; border-right: 3px solid transparent;"
        " border-bottom: 4px solid %7; margin-bottom: 1px; }"
        "QSpinBox::down-arrow { image: none; width: 0; height: 0;"
        " border-left: 3px solid transparent; border-right: 3px solid transparent;"
        " border-top: 4px solid %7; margin-top: 1px; }"
    ).arg(C.card, C.text, C.border, C.activeLine, C.hover, C.active, C.textMuted);
}

// 工具窗 QSS：旧 memo/danmu/overtime 设置窗共用的一套。
inline QString toolQss() {
    const ThemePalette& C = theme();
    return QStringLiteral(
        "QWidget { background: transparent; color: %1;"
        " font-family: 'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif;"
        " font-size: 13px; }"
        "#ToolRoot { background: %2; }"
        "#TopBar { background: %3; border-bottom: 1px solid %4; }"
        "#TabBtn { background: transparent; border: none; border-bottom: 2px solid transparent;"
        " padding: 0 16px; color: %5; font-size: 13px; }"
        "#TabBtn:hover { background: %6; }"
        "#TabBtn[active=\"true\"] { color: %1; font-weight: 600;"
        " border-bottom: 2px solid %7; }"
        "#Card { background: %8; border-radius: 10px; border: 1px solid %4; }"
        "#SectionTitle { font-size: 13px; font-weight: 600; color: %5; background: transparent; }"
        "#ToolPageTitle { font-size: 20px; font-weight: 600; color: %1; background: transparent; }"
        "#ToolTip { font-size: 12px; color: %5; background: transparent; }"
        "QLabel { background: transparent; }"
        "QLineEdit { background: %8; color: %1; border: 1px solid %4; border-radius: 6px;"
        " padding: 0 10px; font-size: 13px; }"
        "QLineEdit:focus { border-color: %7; }"
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 4px; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ).arg(C.text, C.bg, C.sidebar, C.border, C.textMuted, C.hover, C.activeLine, C.card)
        + spinBoxQss();
}

// ─────────────────────────────────────────────
// 布局小工具（旧 home_page._step_card / _scroll_page / memo._row）
// ─────────────────────────────────────────────
inline QScrollArea* scrollPage(QWidget* content) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; }"));
    scroll->setWidget(content);
    return scroll;
}

inline QHBoxLayout* labelRow(const QString& text, QWidget* widget) {
    auto* row = new QHBoxLayout;
    auto* lbl = new QLabel(text);
    lbl->setStyleSheet(QStringLiteral("background: transparent;"));
    row->addWidget(lbl);
    row->addStretch();
    row->addWidget(widget);
    return row;
}

struct StepCard {
    QFrame* card = nullptr;
    QVBoxLayout* body = nullptr;
    QLabel* badge = nullptr;
    QLabel* title = nullptr;

    void refreshTheme() const {
        const ThemePalette& C = theme();
        if (badge) {
            badge->setStyleSheet(QStringLiteral(
                "background: %1; color: #fff; border-radius: 12px;"
                " font-size: 12px; font-weight: 700;"
            ).arg(C.activeLine));
        }
        if (title) {
            title->setStyleSheet(QStringLiteral(
                "font-size: 14px; font-weight: 600; color: %1; background: transparent;"
            ).arg(C.text));
        }
    }
};

inline StepCard stepCard(int num, const QString& titleText) {
    StepCard out;
    out.card = new QFrame;
    out.card->setObjectName(QStringLiteral("Card"));
    out.body = new QVBoxLayout(out.card);
    out.body->setContentsMargins(20, 16, 20, 16);
    out.body->setSpacing(12);

    auto* header = new QHBoxLayout;
    out.badge = new QLabel(QString::number(num));
    out.badge->setFixedSize(24, 24);
    out.badge->setAlignment(Qt::AlignCenter);
    out.title = new QLabel(titleText);
    header->addWidget(out.badge);
    header->addSpacing(8);
    header->addWidget(out.title);
    header->addStretch();
    out.body->addLayout(header);
    out.refreshTheme();
    return out;
}

// ─────────────────────────────────────────────
// ThemedComboBox — 自绘圆角下拉，主题即时联动
// ─────────────────────────────────────────────
class DropPopup final : public QFrame {
public:
    DropPopup() : QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        lay_ = new QVBoxLayout(this);
        lay_->setContentsMargins(4, 4, 4, 4);
        lay_->setSpacing(2);
    }

    void setItems(const QStringList& items, const QString& current,
                  const std::function<void(const QString&)>& onSelect, int width) {
        while (QLayoutItem* item = lay_->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        const ThemePalette& C = theme();
        QStringList ordered;
        ordered << current;
        for (const QString& t : items) {
            if (t != current) ordered << t;
        }
        for (const QString& text : ordered) {
            auto* btn = new QPushButton(text, this);
            btn->setFlat(true);
            btn->setCursor(Qt::PointingHandCursor);
            const bool isCurrent = (text == current);
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { background: transparent; color: %1; border: none;"
                " border-radius: 5px; text-align: left; padding: 0 10px; height: 34px;"
                " font-weight: %2; }"
                "QPushButton:hover { background: %3; }"
            ).arg(C.text, isCurrent ? QStringLiteral("600") : QStringLiteral("400"), C.hover));
            QObject::connect(btn, &QPushButton::clicked, this, [this, text, onSelect]() {
                hide();
                if (onSelect) onSelect(text);
            });
            lay_->addWidget(btn);
        }
        adjustSize();
        setFixedWidth(width);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const ThemePalette& C = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(C.card));
        p.drawRoundedRect(rect(), kRadius, kRadius);

        const qreal inset = kBorder / 2.0;
        QPen pen(QColor(C.activeLine));
        pen.setWidthF(kBorder);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(inset, inset, width() - kBorder, height() - kBorder),
                          kRadius, kRadius);
    }

private:
    static constexpr int kRadius = 8;
    static constexpr int kBorder = 2;
    QVBoxLayout* lay_ = nullptr;
};

class ThemedComboBox final : public QWidget {
public:
    explicit ThemedComboBox(QWidget* parent = nullptr) : QWidget(parent) {
        popup_ = new DropPopup;
        btn_ = new QPushButton(this);
        btn_->setCursor(Qt::PointingHandCursor);
        QObject::connect(btn_, &QPushButton::clicked, this, [this]() { togglePopup(); });

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(btn_);

        refreshTheme();
        onThemeChange(this, [this](const QString&) { refreshTheme(); });
    }

    ~ThemedComboBox() override {
        delete popup_;
        popup_ = nullptr;
    }

    void addItems(const QStringList& items) {
        items_ = items;
        lazyLoaded_ = true;
        if (!items_.isEmpty()) setCurrent(items_.first(), false);
    }

    // 首次展开下拉时再读盘（如 listSkins），避免工具窗构造期扫目录。
    void setLazyItemsLoader(std::function<QStringList()> loader) {
        lazyLoader_ = std::move(loader);
        lazyLoaded_ = false;
        items_.clear();
        current_.clear();
        btn_->setText(QStringLiteral("   ▾"));
    }

    QString currentText() const { return current_; }

    void setCurrentText(const QString& text) {
        if (items_.contains(text)) setCurrent(text, false);
    }

    void setOnChange(std::function<void(const QString&)> cb) { onChange_ = std::move(cb); }

    void setFixedHeight(int h) {
        btn_->setFixedHeight(h);
        QWidget::setFixedHeight(h);
    }

    void setMinimumWidth(int w) {
        btn_->setMinimumWidth(w);
        QWidget::setMinimumWidth(w);
    }

    void setFixedSize(int w, int h) {
        btn_->setFixedSize(w, h);
        QWidget::setFixedSize(w, h);
    }

    // 旧 overtime._SettingsCombo：细边框、11px、内边距收紧。
    void setCompact(bool compact) {
        compact_ = compact;
        refreshTheme();
    }

    void refreshTheme() {
        const ThemePalette& C = theme();
        if (compact_) {
            btn_->setStyleSheet(QStringLiteral(
                "QPushButton { background: %1; color: %2; border: 1px solid %3;"
                " border-radius: 4px; text-align: left; padding: 0 6px; font-size: 11px; }"
                "QPushButton:hover { border-color: %3; background: %4; }"
            ).arg(C.card, C.text, C.activeLine, C.hover));
            return;
        }
        btn_->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 2px solid %3;"
            " border-radius: 6px; text-align: left; padding: 0 10px; font-size: 13px; }"
            "QPushButton:hover { border-color: %3; background: %4; }"
        ).arg(C.card, C.text, C.activeLine, C.hover));
    }

private:
    static constexpr int kOvershoot = 2;

    void setCurrent(const QString& text, bool emitChange) {
        const QString old = current_;
        current_ = text;
        btn_->setText(text + QStringLiteral("   ▾"));
        if (emitChange && old != text && onChange_) onChange_(text);
    }

    void togglePopup() {
        if (popup_->isVisible()) {
            popup_->hide();
            return;
        }
        if (!lazyLoaded_ && lazyLoader_) {
            addItems(lazyLoader_());
        }
        popup_->setItems(items_, current_, [this](const QString& t) { setCurrent(t, true); },
                         width() + kOvershoot * 2);
        popup_->move(mapToGlobal(QPoint(-kOvershoot, -kOvershoot)));
        popup_->show();
    }

    QStringList items_;
    QString current_;
    QPushButton* btn_ = nullptr;
    DropPopup* popup_ = nullptr;
    bool compact_ = false;
    bool lazyLoaded_ = true;
    std::function<QStringList()> lazyLoader_;
    std::function<void(const QString&)> onChange_;
};

// ─────────────────────────────────────────────
// ThemedToggle — 48×26 自绘滑条开关，读写 config
// ─────────────────────────────────────────────
class ThemedToggle final : public QWidget {
public:
    explicit ThemedToggle(const QString& cfgKey, bool defaultValue = true, QWidget* parent = nullptr)
        : QWidget(parent), key_(cfgKey) {
        value_ = configGet(cfgKey, defaultValue).toBool();
        pos_ = value_ ? 1.0 : 0.0;
        setFixedSize(48, 26);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);

        anim_ = new QVariantAnimation(this);
        anim_->setDuration(160);
        anim_->setEasingCurve(QEasingCurve::InOutCubic);
        QObject::connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            pos_ = v.toReal();
            update();
        });
        onThemeChange(this, [this](const QString&) { update(); });
    }

    bool value() const { return value_; }

    void setValue(bool v, bool persist = true) {
        if (v == value_) return;
        value_ = v;
        if (persist) configSet(key_, v);
        pos_ = v ? 1.0 : 0.0;
        update();
    }

    void setOnToggled(std::function<void(bool)> cb) { onToggled_ = std::move(cb); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const ThemePalette& C = theme();
        const int w = width();
        const int h = height();
        const int m = 3;
        const int d = h - m * 2;
        const qreal r = h / 2.0;

        const QColor on(C.activeLine);
        const QColor off(C.border);
        const QColor bg(
            static_cast<int>(off.red() + (on.red() - off.red()) * pos_),
            static_cast<int>(off.green() + (on.green() - off.green()) * pos_),
            static_cast<int>(off.blue() + (on.blue() - off.blue()) * pos_));

        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(0, 0, w, h, r, r);

        const qreal x = m + pos_ * (w - m * 2 - d);
        p.setBrush(QColor(QStringLiteral("#ffffff")));
        p.drawEllipse(static_cast<int>(x), m, d, d);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        value_ = !value_;
        configSet(key_, value_);
        anim_->stop();
        anim_->setStartValue(pos_);
        anim_->setEndValue(value_ ? 1.0 : 0.0);
        anim_->start();
        if (onToggled_) onToggled_(value_);
    }

private:
    QString key_;
    bool value_ = false;
    qreal pos_ = 0.0;
    QVariantAnimation* anim_ = nullptr;
    std::function<void(bool)> onToggled_;
};

// ─────────────────────────────────────────────
// 窗口壳：关闭按钮 / 标题栏 / 导航按钮 / 侧栏
// ─────────────────────────────────────────────
inline constexpr int kSidebarExpanded = 220;
inline constexpr int kSidebarCollapsed = 64;
inline constexpr int kSidebarAnimMs = 220;

// 自绘：QSS 的 border-radius 对 hover 背景裁剪不可靠，这里手绘右上角圆弧。
class CloseButton final : public QPushButton {
public:
    explicit CloseButton(QWidget* parent = nullptr)
        : QPushButton(QStringLiteral("✕"), parent) {
        setObjectName(QStringLiteral("WinBtn_close"));
        setFixedSize(kW, kH);
        setCursor(Qt::ArrowCursor);
        setFlat(true);
        setAttribute(Qt::WA_Hover);
        setStyleSheet(QStringLiteral("border: none; background: transparent;"));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const ThemePalette& C = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const bool hovered = underMouse();
        if (hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(C.closeHover));
            QPainterPath path;
            path.moveTo(0, 0);
            path.lineTo(kW - kR, 0);
            path.arcTo(kW - kR * 2, 0, kR * 2, kR * 2, 90, -90);
            path.lineTo(kW, kH);
            path.lineTo(0, kH);
            path.closeSubpath();
            p.drawPath(path);
        }

        p.setPen(hovered ? QColor(QStringLiteral("#ffffff")) : QColor(C.textMuted));
        QFont f = font();
        f.setPointSize(11);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("✕"));
    }

private:
    static constexpr int kW = 46;
    static constexpr int kH = 36;
    static constexpr int kR = 10;
};

class TitleBar final : public QWidget {
public:
    static constexpr int kHeight = 36;

    explicit TitleBar(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("TitleBar"));
        setAttribute(Qt::WA_StyledBackground, true);
        setFixedHeight(kHeight);

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(14, 0, 0, 0);
        lay->setSpacing(0);

        auto* title = new QLabel(QStringLiteral("LiveAIO"), this);
        title->setObjectName(QStringLiteral("AppTitle"));
        lay->addWidget(title);
        lay->addStretch();

        auto* minBtn = new QPushButton(QStringLiteral("─"), this);
        minBtn->setObjectName(QStringLiteral("WinBtn"));
        minBtn->setCursor(Qt::ArrowCursor);
        QObject::connect(minBtn, &QPushButton::clicked, this, [this]() {
            if (window()) window()->showMinimized();
        });
        lay->addWidget(minBtn);

        auto* closeBtn = new CloseButton(this);
        QObject::connect(closeBtn, &QPushButton::clicked, this, [this]() {
            if (onClose_) {
                onClose_();
            } else if (window()) {
                window()->close();
            }
        });
        lay->addWidget(closeBtn);
    }

    void setOnClose(std::function<void()> cb) { onClose_ = std::move(cb); }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && window()) {
            dragging_ = true;
            dragOffset_ = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_ && (event->buttons() & Qt::LeftButton) && window()) {
            window()->move(event->globalPosition().toPoint() - dragOffset_);
        }
    }

    void mouseReleaseEvent(QMouseEvent*) override { dragging_ = false; }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !window()) return;
        if (window()->isMaximized()) window()->showNormal();
        else window()->showMaximized();
    }

private:
    bool dragging_ = false;
    QPoint dragOffset_;
    std::function<void()> onClose_;
};

class NavButton final : public QPushButton {
public:
    static constexpr int kHeight = 44;
    static constexpr int kIconPx = 18;

    NavButton(const QString& icon, const QString& label,
              const QString& objName = QStringLiteral("NavBtn"), QWidget* parent = nullptr)
        : QPushButton(parent) {
        setObjectName(objName);
        setFixedHeight(kHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        bar_ = new QFrame(this);
        bar_->setFixedWidth(3);

        icon_ = new QLabel(icon, this);
        icon_->setFixedWidth(kSidebarCollapsed - 3);
        icon_->setAlignment(Qt::AlignCenter);

        label_ = new QLabel(label, this);

        lay->addWidget(bar_);
        lay->addWidget(icon_);
        lay->addWidget(label_);
        lay->addStretch();

        setActive(false);
    }

    void setActive(bool on) {
        active_ = on;
        setProperty("active", on);
        style()->unpolish(this);
        style()->polish(this);
        const ThemePalette& C = theme();
        const QString barColor = on ? C.activeLine : QStringLiteral("transparent");
        const QString txtColor = on ? C.text : C.textMuted;
        bar_->setStyleSheet(QStringLiteral("background: %1;").arg(barColor));
        icon_->setStyleSheet(QStringLiteral(
            "font-size: %1px; background: transparent; color: %2;"
        ).arg(QString::number(kIconPx), txtColor));
        label_->setStyleSheet(QStringLiteral(
            "background: transparent; color: %1; font-size: 14px;"
        ).arg(txtColor));
    }

    bool active() const { return active_; }
    void refreshTheme() { setActive(active_); }

private:
    QFrame* bar_ = nullptr;
    QLabel* icon_ = nullptr;
    QLabel* label_ = nullptr;
    bool active_ = false;
};

struct NavItem {
    QString icon;
    QString name;
};

// 收缩动画：内部 panel 宽度锁在展开值，外壳做宽度动画由 Qt 自动裁剪，
// 因此文字静止、边界像遮板一样扫过；不能对外壳 setFixedWidth 否则会瞬跳。
class Sidebar final : public QWidget {
public:
    Sidebar(const QVector<NavItem>& items, const NavItem& settingsItem, QWidget* parent = nullptr)
        : QWidget(parent) {
        setObjectName(QStringLiteral("Sidebar"));
        setAttribute(Qt::WA_StyledBackground, true);
        setFixedWidth(kSidebarExpanded);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        build(items, settingsItem);
        setupAnim();
    }

    const QVector<NavButton*>& navButtons() const { return navBtns_; }
    NavButton* settingsButton() const { return settingsBtn_; }

    void toggle() {
        expanded_ = !expanded_;
        const int start = width();
        const int end = expanded_ ? kSidebarExpanded : kSidebarCollapsed;
        // 一次回调同时改 min/max，避免双动画不同步导致每帧两次布局。
        anim_->stop();
        anim_->setStartValue(start);
        anim_->setEndValue(end);
        anim_->start();
        toggleIcon_->setText(expanded_ ? QStringLiteral("◀") : QStringLiteral("▶"));
    }

    void setOnAnimActive(std::function<void(bool)> cb) { onAnimActive_ = std::move(cb); }

    void setActiveMain(int index) {
        for (int i = 0; i < navBtns_.size(); ++i) navBtns_[i]->setActive(i == index);
        if (settingsBtn_) settingsBtn_->setActive(false);
    }

    void setActiveSettings() {
        for (auto* btn : navBtns_) btn->setActive(false);
        if (settingsBtn_) settingsBtn_->setActive(true);
    }

    void refreshTheme() {
        const ThemePalette& C = theme();
        toggleIcon_->setStyleSheet(QStringLiteral(
            "font-size: 13px; background: transparent; color: %1;").arg(C.textMuted));
        toggleLabel_->setStyleSheet(QStringLiteral(
            "background: transparent; color: %1; font-size: 13px;").arg(C.textMuted));
        for (auto* btn : navBtns_) btn->refreshTheme();
        if (settingsBtn_) settingsBtn_->refreshTheme();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (panel_) panel_->setGeometry(0, 0, kSidebarExpanded, height());
    }

private:
    void build(const QVector<NavItem>& items, const NavItem& settingsItem) {
        panel_ = new QWidget(this);
        panel_->setGeometry(0, 0, kSidebarExpanded, 600);

        auto* outer = new QVBoxLayout(panel_);
        outer->setContentsMargins(0, 12, 0, 12);
        outer->setSpacing(0);

        auto* toggleBtn = new QPushButton(panel_);
        toggleBtn->setObjectName(QStringLiteral("ToggleBtn"));
        toggleBtn->setFixedHeight(44);
        toggleBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        toggleBtn->setCursor(Qt::PointingHandCursor);
        QObject::connect(toggleBtn, &QPushButton::clicked, this, [this]() { toggle(); });

        auto* tLay = new QHBoxLayout(toggleBtn);
        tLay->setContentsMargins(0, 0, 0, 0);
        tLay->setSpacing(0);
        toggleIcon_ = new QLabel(QStringLiteral("◀"), toggleBtn);
        toggleIcon_->setFixedWidth(kSidebarCollapsed);
        toggleIcon_->setAlignment(Qt::AlignCenter);
        toggleLabel_ = new QLabel(QStringLiteral("收起"), toggleBtn);
        tLay->addWidget(toggleIcon_);
        tLay->addWidget(toggleLabel_);
        tLay->addStretch();

        outer->addWidget(toggleBtn);
        outer->addSpacing(4);

        auto* scroll = new QScrollArea(panel_);
        scroll->setWidgetResizable(false);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        auto* container = new QWidget(scroll);
        container->setObjectName(QStringLiteral("ScrollContainer"));
        container->setFixedWidth(kSidebarExpanded);
        auto* cLay = new QVBoxLayout(container);
        cLay->setContentsMargins(0, 0, 0, 0);
        cLay->setSpacing(1);
        for (const NavItem& item : items) {
            auto* btn = new NavButton(item.icon, item.name, QStringLiteral("NavBtn"), container);
            navBtns_.append(btn);
            cLay->addWidget(btn);
        }
        cLay->addStretch();
        scroll->setWidget(container);
        outer->addWidget(scroll, 1);
        outer->addSpacing(4);

        settingsBtn_ = new NavButton(settingsItem.icon, settingsItem.name,
                                     QStringLiteral("SettingsBtn"), panel_);
        outer->addWidget(settingsBtn_);

        refreshTheme();
    }

    void setupAnim() {
        anim_ = new QVariantAnimation(this);
        anim_->setDuration(kSidebarAnimMs);
        anim_->setEasingCurve(QEasingCurve::InOutQuart);
        QObject::connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            const int w = qRound(v.toReal());
            setMinimumWidth(w);
            setMaximumWidth(w);
        });
        QObject::connect(anim_, &QVariantAnimation::stateChanged, this,
                         [this](QAbstractAnimation::State neu, QAbstractAnimation::State) {
                             if (onAnimActive_) {
                                 onAnimActive_(neu == QAbstractAnimation::Running);
                             }
                         });
    }

    QWidget* panel_ = nullptr;
    QLabel* toggleIcon_ = nullptr;
    QLabel* toggleLabel_ = nullptr;
    QVector<NavButton*> navBtns_;
    NavButton* settingsBtn_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    std::function<void(bool)> onAnimActive_;
    bool expanded_ = true;
};

// ─────────────────────────────────────────────
// 悬浮窗共享件：截图透明、软最小化、拖拽/缩放冻结
// ─────────────────────────────────────────────
class OverlayResizeFreeze {
public:
    explicit OverlayResizeFreeze(QWidget* host, std::function<void()> onResume = {})
        : host_(host), onResume_(std::move(onResume)) {}

    bool frozen() const { return live_; }

    void begin() {
        if (live_ || !host_) return;
        live_ = true;
        paused_.clear();
        for (auto* anim : host_->findChildren<QAbstractAnimation*>()) {
            if (anim->state() == QAbstractAnimation::Running) {
                anim->pause();
                paused_.push_back(anim);
            }
        }
        // 不关 setUpdatesEnabled：边框需高帧率重绘，内容由调用方节流。
    }

    void end() {
        if (!live_ || !host_) return;
        live_ = false;
        for (auto* anim : paused_) {
            if (anim && anim->state() == QAbstractAnimation::Paused) anim->resume();
        }
        paused_.clear();
        if (onResume_) onResume_();
        host_->update();
    }

private:
    QWidget* host_ = nullptr;
    std::function<void()> onResume_;
    bool live_ = false;
    QList<QAbstractAnimation*> paused_;
};

inline bool& appAlphaPrepared() {
    static bool prepared = false;
    return prepared;
}

inline void prepareAppAlphaFormat() {
    if (appAlphaPrepared()) return;
    QSurfaceFormat fmt;
    fmt.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);
    appAlphaPrepared() = true;
}

class AlphaBootstrap final : public QObject {
public:
    explicit AlphaBootstrap(QWidget* window) : QObject(window), window_(window) {
        window->installEventFilter(this);
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj == window_ && event->type() == QEvent::Show && !done_) {
            done_ = true;
            QTimer::singleShot(0, this, [this]() { bootstrap(); });
        }
        return false;
    }

private:
    void bootstrap() {
#ifdef Q_OS_WIN
#if defined(LIVEAIO_HAS_OPENGLWIDGET)
        if (!window_ || !window_->isVisible()) return;
        auto* gl = new QOpenGLWidget(window_);
        gl->setFixedSize(1, 1);
        gl->setAttribute(Qt::WA_TransparentForMouseEvents);
        gl->move(-10, -10);
        gl->show();
        QTimer::singleShot(0, gl, &QObject::deleteLater);
#endif
#endif
    }

    QWidget* window_ = nullptr;
    bool done_ = false;
};

inline void enableCaptureTransparency(QWidget* window) {
    prepareAppAlphaFormat();
#ifdef Q_OS_WIN
    if (!window || window->property("_capture_alpha_ready").toBool()) return;
    window->setProperty("_capture_alpha_ready", true);
    new AlphaBootstrap(window);
#else
    Q_UNUSED(window);
#endif
}

class OverlayHost : public QWidget {
public:
    explicit OverlayHost(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, false);
        setMouseTracking(true);
        enableCaptureTransparency(this);
        freeze_ = std::make_unique<OverlayResizeFreeze>(this, [this]() { if (onResume_) onResume_(); });

        borderAnim_ = new QVariantAnimation(this);
        borderAnim_->setDuration(180);
        QObject::connect(borderAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            animR_ = v.toReal();
            update();
        });
        animR_ = 12.0;
        borderShown_ = true;
    }

    void setOnResume(std::function<void()> cb) { onResume_ = std::move(cb); }
    OverlayResizeFreeze* freeze() const { return freeze_.get(); }
    bool softMinimized() const { return softMinimized_; }

    void setBorderShown(bool shown, bool animate = true) {
        if (shown == borderShown_) return;
        borderShown_ = shown;
        borderAnim_->stop();
        const qreal target = shown ? 12.0 : 0.0;
        if (animate) {
            borderAnim_->setStartValue(animR_);
            borderAnim_->setEndValue(target);
            borderAnim_->start();
        } else {
            animR_ = target;
            update();
        }
    }

    void softMinimize() {
        setBorderShown(false, true);
        softMinimized_ = true;
        lower();
    }

    void restoreFromMinimize() {
        if (!softMinimized_) return;
        softMinimized_ = false;
        setBorderShown(true, true);
        raise();
    }

protected:
    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::WindowStateChange) {
            if (windowState() & Qt::WindowMinimized) {
                QTimer::singleShot(0, this, [this]() {
                    showNormal();
                    setBorderShown(false, false);
                    softMinimized_ = true;
                    lower();
                });
            } else if (softMinimized_) {
                QTimer::singleShot(0, this, [this]() { restoreFromMinimize(); });
            }
        } else if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
            QTimer::singleShot(0, this, [this]() { restoreFromMinimize(); });
        }
        QWidget::changeEvent(event);
    }

    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        if (softMinimized_) QTimer::singleShot(0, this, [this]() { restoreFromMinimize(); });
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (freeze_) freeze_->begin();
            dragging_ = true;
            dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_) move(event->globalPosition().toPoint() - dragOffset_);
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (dragging_ && freeze_) freeze_->end();
        dragging_ = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (softMinimized_) restoreFromMinimize();
            else softMinimize();
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (animR_ > 0.5) {
            p.setPen(QPen(QColor(114, 150, 255, softMinimized_ ? 60 : 160), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), animR_, animR_);
        }
    }

private:
    std::unique_ptr<OverlayResizeFreeze> freeze_;
    std::function<void()> onResume_;
    QVariantAnimation* borderAnim_ = nullptr;
    qreal animR_ = 12.0;
    bool borderShown_ = true;
    bool softMinimized_ = false;
    bool dragging_ = false;
    QPoint dragOffset_;
};

// ─────────────────────────────────────────────
// Async-by-default：主线程分帧 / 工作线程读盘（不碰 QWidget）
// ─────────────────────────────────────────────
inline void deferNextTick(QObject* context, std::function<void()> fn) {
    if (!context) return;
    QTimer::singleShot(0, context, std::move(fn));
}

class ChunkBuilder final : public QObject {
public:
    ChunkBuilder(QObject* parent, int batchSize, int intervalMs)
        : QObject(parent), batchSize_(batchSize), intervalMs_(intervalMs) {
        timer_ = new QTimer(this);
        timer_->setSingleShot(true);
        QObject::connect(timer_, &QTimer::timeout, this, [this]() { runBatch(); });
    }

    void start(int total, std::function<void(int)> buildOne, std::function<void()> onDone) {
        total_ = total;
        index_ = 0;
        buildOne_ = std::move(buildOne);
        onDone_ = std::move(onDone);
        runBatch();
    }

private:
    void runBatch() {
        if (!buildOne_) return;
        const int end = std::min(index_ + batchSize_, total_);
        for (int i = index_; i < end; ++i) buildOne_(i);
        index_ = end;
        if (index_ >= total_) {
            if (onDone_) onDone_();
            buildOne_ = nullptr;
            onDone_ = nullptr;
            return;
        }
        timer_->start(intervalMs_);
    }

    QTimer* timer_ = nullptr;
    int batchSize_ = 8;
    int intervalMs_ = 24;
    int total_ = 0;
    int index_ = 0;
    std::function<void(int)> buildOne_;
    std::function<void()> onDone_;
};

class WidgetDeferredDestroy final : public QObject {
public:
    explicit WidgetDeferredDestroy(QObject* parent = nullptr) : QObject(parent) {
        timer_ = new QTimer(this);
        timer_->setSingleShot(true);
        QObject::connect(timer_, &QTimer::timeout, this, [this]() { drainBatch(); });
    }

    void enqueue(QWidget* widget) {
        if (!widget) return;
        queue_.append(widget);
        schedule();
    }

    void enqueueBatch(const QVector<QWidget*>& widgets) {
        for (QWidget* w : widgets) {
            if (w) queue_.append(w);
        }
        schedule();
    }

    void setBatchSize(int n) { batchSize_ = std::max(1, n); }
    void setIntervalMs(int ms) { intervalMs_ = std::max(8, ms); }

private:
    void schedule() {
        if (!timer_->isActive() && !queue_.isEmpty()) timer_->start(intervalMs_);
    }

    void drainBatch() {
        int n = 0;
        while (!queue_.isEmpty() && n < batchSize_) {
            QPointer<QWidget> w = queue_.takeFirst();
            if (w) w->deleteLater();
            ++n;
        }
        if (!queue_.isEmpty()) timer_->start(intervalMs_);
    }

    QTimer* timer_ = nullptr;
    QVector<QPointer<QWidget>> queue_;
    int batchSize_ = 8;
    int intervalMs_ = 24;
};

inline void readJsonAsync(const QString& path, QObject* context,
                          std::function<void(QJsonObject)> onReady) {
    if (!context || !onReady) return;
    auto* watcher = new QFutureWatcher<QJsonObject>(context);
    QObject::connect(watcher, &QFutureWatcher<QJsonObject>::finished, context,
                     [watcher, onReady]() {
                         onReady(watcher->result());
                         watcher->deleteLater();
                     });
    watcher->setFuture(QtConcurrent::run([path]() {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QJsonObject{};
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
        return (err.error == QJsonParseError::NoError && doc.isObject()) ? doc.object()
                                                                         : QJsonObject{};
    }));
}

}  // namespace liveaio::util

#endif  // LIVEAIO_UTIL_WIDGETS_CPP
