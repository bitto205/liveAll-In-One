// tools/tools_common.cpp — Tools 插件共享设施：core 客户端、配置读写、工具窗基类。
// 主题与控件来自 util/widgets.cpp，工具窗不再写死颜色。

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QObject>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTcpSocket>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

#include "../util/widgets.cpp"

namespace liveaio::tools {

using liveaio::util::StepCard;
using liveaio::util::ThemedToggle;
using liveaio::util::labelRow;
using liveaio::util::qssDanger;
using liveaio::util::qssDisabled;
using liveaio::util::qssOutlined;
using liveaio::util::qssMutedLabel;
using liveaio::util::scrollPage;
using liveaio::util::theme;
using liveaio::util::toolQss;

static constexpr const char* kCoreHost = "127.0.0.1";
static constexpr quint16 kCorePort = 19877;

static QString g_appRoot;
static std::function<void(const QJsonObject&)> g_sendPacket;

struct ToolMeta {
    QString id;
    QString title;
    QString desc;
    QString icon;
};

static const QList<ToolMeta>& toolCatalog() {
    static const QList<ToolMeta> catalog = {
        {QStringLiteral("memo"), QStringLiteral("备忘录"),
         QStringLiteral("将礼物、关注、点赞记录为可消除的列表条目"), QStringLiteral("📋")},
        {QStringLiteral("danmu"), QStringLiteral("弹幕机"),
         QStringLiteral("透明悬浮弹幕显示窗口"), QStringLiteral("💬")},
        {QStringLiteral("overtime"), QStringLiteral("加班机"),
         QStringLiteral("透明悬浮加班显示窗口"), QStringLiteral("⏱")},
    };
    return catalog;
}

static QVariantMap readConfigMap() {
    QFile file(QDir(g_appRoot).filePath(QStringLiteral("config.json")));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object().toVariantMap();
}

static QVariant configValue(const QString& key, const QVariant& fallback = {}) {
    const QVariantMap map = readConfigMap();
    const auto it = map.constFind(key);
    return it == map.constEnd() ? fallback : it.value();
}

// core 是 config.json 的唯一写者；未连上时才回退到本地写入。
static void writeConfigValue(const QString& key, const QVariant& value) {
    if (g_sendPacket) {
        g_sendPacket(QJsonObject{
            {QStringLiteral("op"), QStringLiteral("config.set")},
            {QStringLiteral("key"), key},
            {QStringLiteral("value"), QJsonValue::fromVariant(value)},
        });
        return;
    }
    QVariantMap map = readConfigMap();
    map.insert(key, value);
    QFile file(QDir(g_appRoot).filePath(QStringLiteral("config.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Indented));
}

static void installConfigBridge() {
    liveaio::util::setConfigAccessors(
        [](const QString& key, const QVariant& fallback) { return configValue(key, fallback); },
        [](const QString& key, const QVariant& value) { writeConfigValue(key, value); });
    // 主题由主界面驱动（LiveAIO_ToolsApplyTheme），工具侧不重复写配置。
    liveaio::util::setThemePersistHook({});
}

class CoreClient final : public QObject {
public:
    explicit CoreClient(QObject* parent = nullptr) : QObject(parent), socket_(new QTcpSocket(this)) {
        g_sendPacket = [this](const QJsonObject& packet) { send(packet); };
        QObject::connect(socket_, &QTcpSocket::readyRead, this, [this]() { onReadyRead(); });
        QObject::connect(socket_, &QTcpSocket::connected, this, [this]() {
            connected_ = true;
            if (statusCallback_) statusCallback_(true);
        });
        QObject::connect(socket_, &QTcpSocket::disconnected, this, [this]() {
            connected_ = false;
            ready_ = false;
            if (statusCallback_) statusCallback_(false);
        });
    }

    void connectToCore() { socket_->connectToHost(QString::fromLatin1(kCoreHost), kCorePort); }
    void setPacketCallback(std::function<void(const QJsonObject&)> cb) { packetCallback_ = std::move(cb); }
    void setStatusCallback(std::function<void(bool)> cb) { statusCallback_ = std::move(cb); }
    bool isReady() const { return ready_; }

    void send(const QJsonObject& packet) {
        if (socket_->state() != QAbstractSocket::ConnectedState) return;
        QByteArray out = QJsonDocument(packet).toJson(QJsonDocument::Compact);
        out.push_back('\n');
        socket_->write(out);
    }

private:
    void onReadyRead() {
        buffer_.append(socket_->readAll());
        while (true) {
            const int idx = buffer_.indexOf('\n');
            if (idx < 0) break;
            QByteArray line = buffer_.left(idx).trimmed();
            buffer_.remove(0, idx + 1);
            if (line.isEmpty()) continue;
            QJsonParseError err;
            const auto doc = QJsonDocument::fromJson(line, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
            QJsonObject packet = doc.object();
            if (packet.value(QStringLiteral("op")).toString() == QStringLiteral("ready")) ready_ = true;
            if (packetCallback_) packetCallback_(packet);
        }
    }

    QTcpSocket* socket_;
    QByteArray buffer_;
    bool connected_ = false;
    bool ready_ = false;
    std::function<void(const QJsonObject&)> packetCallback_;
    std::function<void(bool)> statusCallback_;
};

class ToolWindowBase : public QMainWindow {
public:
    explicit ToolWindowBase(CoreClient* core, QWidget* parent = nullptr)
        : QMainWindow(parent, Qt::Window), core_(core) {
        setAttribute(Qt::WA_QuitOnClose, false);
    }
    virtual QString toolId() const = 0;
    virtual void onCorePacket(const QJsonObject& packet) = 0;
    virtual void onCoreStatus(bool connected) { Q_UNUSED(connected); }
    virtual void refreshTheme() { setStyleSheet(toolQss()); }

protected:
    void sendCore(const QJsonObject& packet) const {
        if (core_) core_->send(packet);
    }
    CoreClient* core() const { return core_; }

private:
    CoreClient* core_;
};

// 旧 memo/danmu/overtime 设置窗共用的顶部 Tab 壳。
class TabbedToolWindow : public ToolWindowBase {
public:
    TabbedToolWindow(CoreClient* core, const QStringList& tabNames, int barHeight = 44)
        : ToolWindowBase(core) {
        auto* root = new QWidget(this);
        root->setObjectName(QStringLiteral("ToolRoot"));
        setCentralWidget(root);

        auto* lay = new QVBoxLayout(root);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        auto* topbar = new QWidget(root);
        topbar->setObjectName(QStringLiteral("TopBar"));
        topbar->setFixedHeight(barHeight);
        auto* tb = new QHBoxLayout(topbar);
        tb->setContentsMargins(8, 0, 8, 0);
        tb->setSpacing(0);
        for (int i = 0; i < tabNames.size(); ++i) {
            auto* btn = new QPushButton(tabNames.at(i), topbar);
            btn->setObjectName(QStringLiteral("TabBtn"));
            btn->setFixedHeight(barHeight);
            btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            btn->setCursor(Qt::PointingHandCursor);
            QObject::connect(btn, &QPushButton::clicked, this, [this, i]() { switchTab(i); });
            tabs_.append(btn);
            tb->addWidget(btn);
        }
        tb->addStretch();
        lay->addWidget(topbar);

        stack_ = new QStackedWidget(root);
        lay->addWidget(stack_);
    }

    void switchTab(int index) {
        stack_->setCurrentIndex(index);
        for (int i = 0; i < tabs_.size(); ++i) {
            tabs_[i]->setProperty("active", i == index);
            tabs_[i]->style()->unpolish(tabs_[i]);
            tabs_[i]->style()->polish(tabs_[i]);
        }
    }

protected:
    void addTabPage(QWidget* page) { stack_->addWidget(page); }

    QStackedWidget* stack_ = nullptr;
    QVector<QPushButton*> tabs_;
};

// ─────────────────────────────────────────────
// 悬浮窗外框：顶栏 + 三侧边框以左上圆圈为圆心做圆形 clip 波纹展开
// （旧 tools/danmu_tool._DanmuRoot 与 overtime_tool._OvertimeRoot 的共同部分）
// ─────────────────────────────────────────────
class RippleOverlayRoot : public QWidget {
public:
    static constexpr int kBorderW = 2;
    static constexpr int kTopbarH = 32;
    static constexpr int kResizeHit = 8;
    static constexpr int kCircleD = 14;
    static constexpr int kCircleOff = 5;
    static constexpr int kAnimMs = 280;
    static constexpr int kBtnW = 32;

    explicit RippleOverlayRoot(QMainWindow* win, QWidget* parent = nullptr)
        : QWidget(parent), win_(win) {
        setMouseTracking(true);
        setStyleSheet(QStringLiteral("background: transparent;"));
        freeze_ = std::make_unique<liveaio::util::OverlayResizeFreeze>(
            win, [this]() { handleResizeResume(); });

        btnBox_ = new QWidget(this);
        auto* btnLay = new QHBoxLayout(btnBox_);
        btnLay->setContentsMargins(0, 0, 0, 0);
        btnLay->setSpacing(0);
        minBtn_ = new QPushButton(QStringLiteral("─"), btnBox_);
        minBtn_->setToolTip(QStringLiteral("收起边框并置底（保持渲染）"));
        closeBtn_ = new QPushButton(QStringLiteral("✕"), btnBox_);
        for (auto* btn : {minBtn_, closeBtn_}) {
            btn->setCursor(Qt::ArrowCursor);
            btnLay->addWidget(btn);
        }
        btnBox_->setVisible(false);
        QObject::connect(minBtn_, &QPushButton::clicked, this, [this]() {
            if (onMinimize_) onMinimize_();
        });
        QObject::connect(closeBtn_, &QPushButton::clicked, this, [this]() { win_->close(); });

        circle_ = new CircleButton(this);
        circle_->move(kCircleOff, kCircleOff);
        circle_->raise();
        QObject::connect(circle_, &QPushButton::clicked, this, [this]() {
            if (onCircleClicked_) onCircleClicked_();
        });

        content_ = new QWidget(this);
        content_->setAttribute(Qt::WA_TranslucentBackground);
        content_->setAttribute(Qt::WA_TransparentForMouseEvents);
        content_->setStyleSheet(QStringLiteral("background: transparent;"));

        refreshChrome();
        liveaio::util::onThemeChange(this, [this](const QString&) {
            refreshChrome();
            update();
        });
    }

    void setOnCircleClicked(std::function<void()> cb) { onCircleClicked_ = std::move(cb); }
    void setOnMinimize(std::function<void()> cb) { onMinimize_ = std::move(cb); }

    bool resizeFrozen() const { return freeze_->frozen(); }
    QWidget* content() const { return content_; }
    qreal radius() const { return r_; }

    qreal maxRadius() const {
        const qreal c = centerOffset();
        return std::max(std::max(std::hypot(c, c), std::hypot(width() - c, c)),
                        std::max(std::hypot(c, height() - c),
                                 std::hypot(width() - c, height() - c)));
    }

    void setRadius(qreal r) {
        r_ = r;
        // 用可见性而不是 setMask 控制按钮，避免半透明窗上 mask 抖动。
        btnBox_->setVisible(r >= std::hypot(width() - centerOffset(),
                                            kTopbarH - centerOffset()));
        update();
    }

protected:
    enum class Edge { None, Left, Right, Bottom, BottomLeft, BottomRight };

    // 子类按自身比例约束改写；默认自由缩放。
    virtual QRect resizeGeometry(Edge edge, const QPoint& delta, const QRect& start) const {
        QRect geo = start;
        if (edge == Edge::Right || edge == Edge::BottomRight) geo.setRight(geo.right() + delta.x());
        if (edge == Edge::Left || edge == Edge::BottomLeft) geo.setLeft(geo.left() + delta.x());
        if (edge == Edge::Bottom || edge == Edge::BottomLeft || edge == Edge::BottomRight) {
            geo.setBottom(geo.bottom() + delta.y());
        }
        return geo;
    }

    virtual void onContentGeometryChanged() {}
    virtual void onResizeResume() {}

    QMainWindow* hostWindow() const { return win_; }
    bool resizing() const { return resizing_; }

    void paintEvent(QPaintEvent*) override {
        if (r_ <= 0) return;
        const auto& C = theme();
        const qreal c = centerOffset();

        QPainter p(this);
        // 拖拽缩放时关抗锯齿，降低半透明窗圆形 clip 的重绘成本。
        p.setRenderHint(QPainter::Antialiasing, !resizing_);
        QPainterPath clip;
        clip.addEllipse(c - r_, c - r_, r_ * 2, r_ * 2);
        p.setClipPath(clip);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(C.sidebar));
        p.drawRect(0, 0, width(), kTopbarH);

        QPen pen(QColor(C.sidebar), kBorderW);
        pen.setCapStyle(Qt::FlatCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const int hw = std::max(1, kBorderW / 2);
        p.drawLine(hw, 0, hw, height());
        p.drawLine(width() - hw, 0, width() - hw, height());
        p.drawLine(0, height() - hw, width(), height() - hw);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        const int btnTotal = kBtnW * 2;
        btnBox_->setGeometry(width() - btnTotal, 0, btnTotal, kTopbarH);
        content_->setGeometry(0, kTopbarH, width(), height() - kTopbarH);
        if (!freeze_->frozen()) onContentGeometryChanged();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        if (!win_->isActiveWindow()) win_->raise();
        const QPoint pos = event->position().toPoint();
        const Edge edge = edgeAt(pos);
        if (edge != Edge::None) {
            freeze_->begin();
            resizing_ = true;
            resizeEdge_ = edge;
            resizeStartGeo_ = win_->geometry();
            resizeStartPos_ = event->globalPosition().toPoint();
            return;
        }
        if (pos.y() < kTopbarH && r_ > kTopbarH * 0.5) {
            dragging_ = true;
            dragAnchor_ = event->globalPosition().toPoint() - win_->pos();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const QPoint gpos = event->globalPosition().toPoint();
        if (event->buttons() & Qt::LeftButton) {
            if (resizing_) {
                const QRect geo = resizeGeometry(
                    resizeEdge_, gpos - resizeStartPos_, resizeStartGeo_);
                if (geo.width() >= win_->minimumWidth()
                    && geo.height() >= win_->minimumHeight()) {
                    win_->setGeometry(geo);
                }
                return;
            }
            if (dragging_) {
                win_->move(gpos - dragAnchor_);
                return;
            }
        }
        setCursor(cursorFor(edgeAt(event->position().toPoint())));
    }

    void mouseReleaseEvent(QMouseEvent*) override {
        const bool wasResize = resizing_;
        dragging_ = false;
        resizing_ = false;
        resizeEdge_ = Edge::None;
        setCursor(Qt::ArrowCursor);
        if (wasResize) freeze_->end();
    }

private:
    // 左上角圆圈：始终可见，点击切换边框。
    class CircleButton final : public QPushButton {
    public:
        explicit CircleButton(QWidget* parent) : QPushButton(parent) {
            setFixedSize(kCircleD, kCircleD);
            setCursor(Qt::PointingHandCursor);
            setFlat(true);
            setStyleSheet(QStringLiteral("background: transparent; border: none;"));
            setToolTip(QStringLiteral("显示/隐藏边框"));
        }

    protected:
        void paintEvent(QPaintEvent*) override {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(theme().border));
            p.drawEllipse(0, 0, kCircleD, kCircleD);
        }
    };

    static qreal centerOffset() { return kCircleOff + kCircleD / 2.0; }

    void refreshChrome() {
        const auto& C = theme();
        const QString style = QStringLiteral(
            "QPushButton { background: transparent; border: none; color: %1; font-size: 12px;"
            " min-width: %2px; max-width: %2px; min-height: %3px; max-height: %3px; }"
            "QPushButton:hover { background: %4; color: %5; }"
        ).arg(C.textMuted, QString::number(kBtnW), QString::number(kTopbarH),
              C.btnHover, C.text);
        minBtn_->setStyleSheet(style);
        closeBtn_->setStyleSheet(style);
        circle_->update();
    }

    void handleResizeResume() {
        onContentGeometryChanged();
        onResizeResume();
    }

    Edge edgeAt(const QPoint& pos) const {
        const bool left = pos.x() < kResizeHit;
        const bool right = pos.x() > width() - kResizeHit;
        const bool bottom = pos.y() > height() - kResizeHit;
        if (left && bottom) return Edge::BottomLeft;
        if (right && bottom) return Edge::BottomRight;
        if (left) return Edge::Left;
        if (right) return Edge::Right;
        if (bottom) return Edge::Bottom;
        return Edge::None;
    }

    static Qt::CursorShape cursorFor(Edge edge) {
        switch (edge) {
        case Edge::Left:
        case Edge::Right: return Qt::SizeHorCursor;
        case Edge::Bottom: return Qt::SizeVerCursor;
        case Edge::BottomLeft: return Qt::SizeBDiagCursor;
        case Edge::BottomRight: return Qt::SizeFDiagCursor;
        default: return Qt::ArrowCursor;
        }
    }

    QMainWindow* win_ = nullptr;
    QWidget* btnBox_ = nullptr;
    QWidget* content_ = nullptr;
    QPushButton* minBtn_ = nullptr;
    QPushButton* closeBtn_ = nullptr;
    CircleButton* circle_ = nullptr;
    std::unique_ptr<liveaio::util::OverlayResizeFreeze> freeze_;
    std::function<void()> onCircleClicked_;
    std::function<void()> onMinimize_;
    qreal r_ = 0.0;
    bool dragging_ = false;
    QPoint dragAnchor_;
    bool resizing_ = false;
    Edge resizeEdge_ = Edge::None;
    QRect resizeStartGeo_;
    QPoint resizeStartPos_;
};

// 半透明悬浮窗：几何持久化 + 波纹展开动画，供弹幕机/加班机复用。
class RippleOverlayWindow : public QMainWindow {
public:
    RippleOverlayWindow(const QString& title, const QString& geoKey)
        : QMainWindow(nullptr, Qt::FramelessWindowHint | Qt::Window), geoKey_(geoKey) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_QuitOnClose, false);
        liveaio::util::enableCaptureTransparency(this);
        setWindowTitle(title);

        anim_ = new QVariantAnimation(this);
        anim_->setDuration(RippleOverlayRoot::kAnimMs);
        anim_->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            animR_ = v.toReal();
            root_->setRadius(animR_);
        });
    }

    void setOnClosed(std::function<void()> cb) { onClosed_ = std::move(cb); }

    void toggleFrame() {
        shown_ = !shown_;
        anim_->stop();
        anim_->setStartValue(animR_);
        anim_->setEndValue(shown_ ? std::max(root_->maxRadius(), 1.0) : 0.0);
        anim_->start();
    }

    void minimizeOverlay() {
        if (shown_) toggleFrame();
        lower();
    }

protected:
    void attachRoot(RippleOverlayRoot* root) {
        root_ = root;
        setCentralWidget(root_);
        root_->setOnCircleClicked([this]() { toggleFrame(); });
        root_->setOnMinimize([this]() { minimizeOverlay(); });
    }

    RippleOverlayRoot* root() const { return root_; }
    bool frameShown() const { return shown_; }
    bool animRunning() const { return anim_->state() == QAbstractAnimation::Running; }

    void syncRadiusAfterResize() {
        if (shown_ && !animRunning()) animR_ = root_->maxRadius();
        root_->setRadius(animR_);
    }

    void restoreGeometryFromConfig(int defW, int defH) {
        const QVariantMap saved = configValue(geoKey_).toMap();
        if (saved.isEmpty()) {
            resize(defW, defH);
            return;
        }
        const int x = saved.value(QStringLiteral("x")).toInt();
        const int y = saved.value(QStringLiteral("y")).toInt();
        const int w = qMax(minimumWidth(), saved.value(QStringLiteral("w"), defW).toInt());
        const int h = qMax(minimumHeight(), saved.value(QStringLiteral("h"), defH).toInt());
        bool onScreen = false;
        for (auto* screen : QApplication::screens()) {
            if (screen->availableGeometry().contains(QRect(x, y, 1, 1))) {
                onScreen = true;
                break;
            }
        }
        if (onScreen) setGeometry(x, y, w, h);
        else resize(w, h);
    }

    void showEvent(QShowEvent* event) override {
        QMainWindow::showEvent(event);
        if (!firstShow_) return;
        firstShow_ = false;
        // 布局完成后才知道 maxRadius，延后一帧初始化半径。
        QTimer::singleShot(0, this, [this]() {
            qreal r = root_->maxRadius();
            if (r <= 0) r = 800.0;
            animR_ = r;
            shown_ = true;
            root_->setRadius(r);
        });
    }

    void hideEvent(QHideEvent* event) override {
        QMainWindow::hideEvent(event);
        const QRect geo = geometry();
        writeConfigValue(geoKey_, QVariantMap{
            {QStringLiteral("x"), geo.x()},
            {QStringLiteral("y"), geo.y()},
            {QStringLiteral("w"), geo.width()},
            {QStringLiteral("h"), geo.height()},
        });
    }

    void closeEvent(QCloseEvent* event) override {
        event->ignore();
        hide();
        if (onClosed_) onClosed_();
    }

private:
    QString geoKey_;
    RippleOverlayRoot* root_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    qreal animR_ = 0.0;
    bool shown_ = true;
    bool firstShow_ = true;
    std::function<void()> onClosed_;
};

// 旧 memo._section：卡片 + 标题 + 若干「文字 / 控件」行。
static QFrame* sectionCard(const QString& title, const QVector<QPair<QString, QWidget*>>& rows,
                           QWidget* parent = nullptr) {
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("Card"));
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);
    auto* t = new QLabel(title, card);
    t->setObjectName(QStringLiteral("SectionTitle"));
    lay->addWidget(t);
    for (const auto& row : rows) lay->addLayout(labelRow(row.first, row.second));
    return card;
}

}  // namespace liveaio::tools
