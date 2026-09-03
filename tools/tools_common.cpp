// tools/tools_common.cpp — Tools 插件共享设施：core 客户端、配置读写、工具窗基类。
// 主题与控件来自 util/widgets.cpp，工具窗不再写死颜色。

#include <QApplication>
#include <QCheckBox>
#include <QChildEvent>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QEnterEvent>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineF>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTcpSocket>
#include <QTimer>
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
using liveaio::util::deferNextTick;
using liveaio::util::WidgetDeferredDestroy;

static constexpr const char* kCoreHost = "127.0.0.1";
static constexpr quint16 kCorePort = 19877;

static QString g_appRoot;
static std::function<void(const QJsonObject&)> g_sendPacket;
static QVariantMap g_configCache;
static bool g_configCacheLoaded = false;

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
        {QStringLiteral("leaf"), QStringLiteral("捡叶子"),
         QStringLiteral("送礼堆叶子，拖到垃圾桶消除"), QStringLiteral("🍃")},
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

static void mergeConfigValues(const QVariantMap& values) {
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        g_configCache.insert(it.key(), it.value());
    }
    g_configCacheLoaded = true;
}

static QVariant configValue(const QString& key, const QVariant& fallback = {}) {
    if (g_configCacheLoaded) {
        const auto it = g_configCache.constFind(key);
        if (it != g_configCache.constEnd()) return it.value();
    }
    const QVariantMap map = readConfigMap();
    const auto it = map.constFind(key);
    return it == map.constEnd() ? fallback : it.value();
}

// core 是 config.json 的唯一写者；乐观更新缓存，未连上时入队 ready 后 flush。
static void writeConfigValue(const QString& key, const QVariant& value) {
    g_configCache.insert(key, value);
    g_configCacheLoaded = true;
    if (g_sendPacket) g_sendPacket(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("config.set")},
        {QStringLiteral("key"), key},
        {QStringLiteral("value"), QJsonValue::fromVariant(value)},
    });
}

static void installConfigBridge() {
    mergeConfigValues(readConfigMap());
    liveaio::util::setConfigAccessors(
        [](const QString& key, const QVariant& fallback) { return configValue(key, fallback); },
        [](const QString& key, const QVariant& value) { writeConfigValue(key, value); });
    // 主题由主界面驱动（LiveAIO_ToolsApplyTheme），工具侧不重复写配置。
    liveaio::util::setThemePersistHook({});
}

class CoreClient final : public QObject {
public:
    explicit CoreClient(QObject* parent = nullptr) : QObject(parent), socket_(new QTcpSocket(this)) {
        g_sendPacket = [this](const QJsonObject& packet) { queueOrSend(packet); };
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

    void send(const QJsonObject& packet) { queueOrSend(packet); }

private:
    void queueOrSend(const QJsonObject& packet) {
        const QString op = packet.value(QStringLiteral("op")).toString();
        if (op == QStringLiteral("config.set") && !ready_) {
            pendingSets_.append(packet);
            return;
        }
        if (socket_->state() != QAbstractSocket::ConnectedState) {
            if (op == QStringLiteral("config.set")) pendingSets_.append(packet);
            return;
        }
        QByteArray out = QJsonDocument(packet).toJson(QJsonDocument::Compact);
        out.push_back('\n');
        socket_->write(out);
    }

    void onReady() {
        ready_ = true;
        send(QJsonObject{{QStringLiteral("op"), QStringLiteral("config.get")}});
        flushPendingSets();
    }

    void flushPendingSets() {
        if (!ready_ || socket_->state() != QAbstractSocket::ConnectedState) return;
        const auto pending = pendingSets_;
        pendingSets_.clear();
        for (const QJsonObject& p : pending) send(p);
    }

    bool handleConfigPacket(const QJsonObject& packet) {
        const QString op = packet.value(QStringLiteral("op")).toString();
        if (op == QStringLiteral("config.value")) {
            if (packet.contains(QStringLiteral("values"))) {
                mergeConfigValues(packet.value(QStringLiteral("values")).toObject().toVariantMap());
            } else if (packet.contains(QStringLiteral("key"))) {
                g_configCache.insert(packet.value(QStringLiteral("key")).toString(),
                                     packet.value(QStringLiteral("value")).toVariant());
                g_configCacheLoaded = true;
            }
            return true;
        }
        if (op == QStringLiteral("config.ok")) {
            const QString key = packet.value(QStringLiteral("key")).toString();
            if (!key.isEmpty()) {
                g_configCache.insert(key, packet.value(QStringLiteral("value")).toVariant());
                g_configCacheLoaded = true;
            }
            return true;
        }
        return false;
    }

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
            const QString op = packet.value(QStringLiteral("op")).toString();
            if (op == QStringLiteral("ready")) onReady();
            if (handleConfigPacket(packet)) continue;
            if (packetCallback_) packetCallback_(packet);
        }
    }

    QTcpSocket* socket_;
    QByteArray buffer_;
    QVector<QJsonObject> pendingSets_;
    bool connected_ = false;
    bool ready_ = false;
    std::function<void(const QJsonObject&)> packetCallback_;
    std::function<void(bool)> statusCallback_;
};

class ToolsSession;

class ToolRuntimeBase : public QObject {
public:
    explicit ToolRuntimeBase(QObject* parent = nullptr) : QObject(parent) {}
    virtual QString toolId() const = 0;
    virtual bool isOverlayActive() const { return false; }
    virtual void onCorePacket(const QJsonObject&) {}
    virtual void onCoreStatus(bool) {}
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
    virtual void refreshTheme() {}
    virtual void onPanelClosing() {}
    // 工具窗样式写在窗自身，禁止写到 qApp，以免盖掉主窗 shellQss。
    virtual void applyChromeStyle() { setStyleSheet(liveaio::util::toolQss()); }

    void setPanelCloseHandler(std::function<void(const QString&)> handler) {
        panelCloseHandler_ = std::move(handler);
    }

protected:
    void sendCore(const QJsonObject& packet) const {
        if (core_) core_->send(packet);
    }
    CoreClient* core() const { return core_; }

    void closeEvent(QCloseEvent* event) override {
        onPanelClosing();
        if (panelCloseHandler_) panelCloseHandler_(toolId());
        event->accept();
        hide();
        deleteLater();
    }

private:
    CoreClient* core_;
    std::function<void(const QString&)> panelCloseHandler_;
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
            liveaio::util::suppressButtonFocus(btn);
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
        ensureTab(index);
        stack_->setCurrentIndex(index);
        for (int i = 0; i < tabs_.size(); ++i) {
            tabs_[i]->setProperty("active", i == index);
            tabs_[i]->style()->unpolish(tabs_[i]);
            tabs_[i]->style()->polish(tabs_[i]);
        }
    }

    void addTabPlaceholder() {
        auto* ph = new QWidget(stack_);
        ph->setObjectName(QStringLiteral("TabPlaceholder"));
        stack_->addWidget(ph);
        tabPlaceholders_.append(ph);
        tabBuilt_.append(false);
    }

    void replaceTabPlaceholder(int index, QWidget* page) {
        if (index < 0 || index >= tabPlaceholders_.size()) {
            addTabPage(page);
            return;
        }
        QWidget* ph = tabPlaceholders_.value(index);
        if (!ph) {
            addTabPage(page);
            return;
        }
        const int idx = stack_->indexOf(ph);
        stack_->removeWidget(ph);
        ph->deleteLater();
        tabPlaceholders_[index] = nullptr;
        stack_->insertWidget(idx, page);
    }

protected:
    void addTabPage(QWidget* page) { stack_->addWidget(page); }

    void ensureTab(int index) {
        if (index < 0 || index >= tabFactories_.size() || !tabFactories_[index]) return;
        if (tabBuilt_.value(index)) return;
        tabBuilt_[index] = true;
        if (tabFactories_[index]) replaceTabPlaceholder(index, tabFactories_[index]());
    }

    void setTabFactory(int index, std::function<QWidget*()> factory) {
        if (index >= tabFactories_.size()) tabFactories_.resize(index + 1);
        tabFactories_[index] = std::move(factory);
    }

    QStackedWidget* stack_ = nullptr;
    QVector<QPushButton*> tabs_;
    QVector<QWidget*> tabPlaceholders_;
    QVector<std::function<QWidget*()>> tabFactories_;
    QVector<bool> tabBuilt_;
};

// ─────────────────────────────────────────────
// 悬浮窗外框：顶栏 + 三侧边框以左上圆圈为圆心做圆形 clip 波纹展开
// （旧 tools/danmu_tool._DanmuRoot 与 overtime_tool._OvertimeRoot 的共同部分）
// ─────────────────────────────────────────────
class RippleOverlayRoot : public QWidget {
public:
    static constexpr int kBorderW = 2;
    static constexpr int kTopbarH = 32;
    static constexpr int kResizeHit = 18;
    static constexpr int kBtnW = 32;
    static constexpr int kIconDraw = 18;
    static constexpr int kCircleOff = 0;
    static constexpr qreal kIconStroke = 1.35;
    static constexpr qreal kIconBoxRadius = 2.0;
    static constexpr qreal kIconBoxInset = 4.8;
    static constexpr int kAnimMs = 320;
    static constexpr int kLockAnimMs = 180;
    static constexpr int kHoverAnimMs = 120;

    explicit RippleOverlayRoot(QMainWindow* win, QWidget* parent = nullptr)
        : QWidget(parent), win_(win) {
        setMouseTracking(true);
        setStyleSheet(QStringLiteral("background: transparent;"));
        freeze_ = std::make_unique<liveaio::util::OverlayResizeFreeze>(
            win, [this]() { handleResizeResume(); });

        leftBox_ = new QWidget(this);
        auto* leftLay = new QHBoxLayout(leftBox_);
        leftLay->setContentsMargins(kCircleOff, 0, 0, 0);
        leftLay->setSpacing(0);
        leftLay->setAlignment(Qt::AlignVCenter);

        frame_ = new FrameBoxButton(leftBox_);
        QObject::connect(frame_, &QPushButton::clicked, this, [this]() {
            if (onFrameClicked_) onFrameClicked_();
        });
        leftLay->addWidget(frame_);

        lock_ = new LockBoxButton(leftBox_);
        QObject::connect(lock_, &QPushButton::clicked, this, [this]() {
            if (onLockClicked_) onLockClicked_();
        });
        leftLay->addWidget(lock_);

        hideTimer_ = new QTimer(this);
        hideTimer_->setSingleShot(true);
        hideTimer_->setInterval(2000);
        QObject::connect(hideTimer_, &QTimer::timeout, this, [this]() {
            if (transitioning_) return;
            if (!pointerNearControls()) hideControlsNow();
        });

        content_ = new QWidget(this);
        content_->setAttribute(Qt::WA_TranslucentBackground);
        content_->setAttribute(Qt::WA_TransparentForMouseEvents);
        content_->setStyleSheet(QStringLiteral("background: transparent;"));
        watchMouseTree(content_);

        refreshChrome();
        liveaio::util::onThemeChange(this, [this](const QString&) {
            refreshChrome();
            update();
        });
    }

    void setOnFrameClicked(std::function<void()> cb) { onFrameClicked_ = std::move(cb); }
    void setOnLockClicked(std::function<void()> cb) { onLockClicked_ = std::move(cb); }
    void setOnMinimize(std::function<void()> cb) { onMinimize_ = std::move(cb); }
    void setOnClose(std::function<void()> cb) { onClose_ = std::move(cb); }

    void setChromeState(bool borderShown, bool locked, bool transitioning) {
        borderShown_ = borderShown;
        locked_ = locked;
        transitioning_ = transitioning;
        if (transitioning) {
            hideTimer_->stop();
            frame_->setProgress(borderShown ? 1.0 : 0.0, true);
            lock_->setLocked(locked);
        } else {
            frame_->setProgress(borderShown ? 1.0 : 0.0, false);
            lock_->setLocked(locked);
            lock_->setAccentProgress(borderShown ? 1.0 : 0.0);
        }
        refreshControlVisibility();
    }

    bool resizeFrozen() const { return freeze_->frozen(); }
    QWidget* content() const { return content_; }
    qreal radius() const { return r_; }

    // 空白处点穿到后面的窗口；只拦边框/按钮，以及子类声明的内容命中。
    bool wantsMouseAt(const QPoint& local) const {
        if (dragging_ || resizing_) return true;
        if (auto* grab = QWidget::mouseGrabber()) {
            if (grab == this || isAncestorOf(grab) || grab == win_) return true;
        }
        if (leftBox_ && leftBox_->isVisible() && leftBox_->geometry().contains(local)) {
            return true;
        }
        const QPoint center(static_cast<int>(centerX()), static_cast<int>(centerY()));
        if (QLineF(local, center).length() <= proximityRadiusPx()) return true;
        if (!locked_) {
            if (borderShown_ && local.y() >= 0 && local.y() < kTopbarH) return true;
            if (edgeAt(local) != Edge::None) return true;
            if (minBtnRect().contains(local) || closeBtnRect().contains(local)) return true;
        }
        if (content_ && content_->geometry().contains(local)) {
            const QPoint cp(local.x() - content_->x(), local.y() - content_->y());
            if (contentWantsMouse(cp)) return true;
        }
        return false;
    }

    void syncHoverCursor(const QPoint& local) {
        updateProximity(local);
        updateBorderActionHover(local);
        if (locked_) return;
        const Edge edge = edgeAt(local);
        if (edge != Edge::None) {
            applyCursor(cursorFor(edge), local);
            return;
        }
        if (local.y() >= 0 && local.y() < kTopbarH) {
            applyCursor(Qt::ArrowCursor, local);
        }
    }

    qreal maxRadius() const {
        const qreal cx = centerX();
        const qreal cy = centerY();
        return std::max(std::max(std::hypot(cx, cy), std::hypot(width() - cx, cy)),
                        std::max(std::hypot(cx, height() - cy),
                                 std::hypot(width() - cx, height() - cy)));
    }

    void setRadius(qreal r) {
        r_ = r;
        const qreal maxR = std::max(maxRadius(), 1.0);
        const qreal progress = std::clamp(r_ / maxR, 0.0, 1.0);
        frame_->setProgress(progress, false);
        lock_->setAccentProgress(progress);
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
    // 拖拽中低帧率刷新内容（默认走完整布局；加班机等可改成轻量布局）。
    virtual void onContentGeometryWhileResizing() { onContentGeometryChanged(); }
    virtual void onResizeBegin() {}
    virtual void onResizeResume() {}
    // 叶子等需要缩放同帧更新碰撞/绘制时返回 true，跳过节流。
    virtual bool wantsSyncResizeLayout() const { return false; }
    virtual bool contentWantsMouse(const QPoint&) const { return false; }

    QMainWindow* hostWindow() const { return win_; }
    bool resizing() const { return resizing_; }

    void paintEvent(QPaintEvent*) override {
        if (r_ <= 0) return;
        const auto& C = theme();
        const qreal cx = centerX();
        const qreal cy = centerY();

        QPainter p(this);
        // 拖拽缩放时关抗锯齿，降低半透明窗圆形 clip 的重绘成本。
        p.setRenderHint(QPainter::Antialiasing, !resizing_);
        QPainterPath clip;
        clip.addEllipse(cx - r_, cy - r_, r_ * 2, r_ * 2);
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

        // 最小化/关闭画在边框 clip 内，与波纹同帧渲染，避免独立控件 mask 闪烁。
        paintBorderActionButtons(p);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        const int leftTotal = kCircleOff + kBtnW * 2;
        leftBox_->setGeometry(0, 0, leftTotal, kTopbarH);
        content_->setGeometry(0, kTopbarH, width(), height() - kTopbarH);
        // 边框已展开时半径跟随窗口，否则放大后圆形 clip 会裁掉新边框。
        if (freeze_->frozen() && r_ > kTopbarH * 0.5) r_ = maxRadius();
        update();
        if (freeze_->frozen() && !wantsSyncResizeLayout()) {
            requestThrottledContentLayout();
        } else {
            onContentGeometryChanged();
        }
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ChildAdded) {
            if (auto* w = qobject_cast<QWidget*>(static_cast<QChildEvent*>(event)->child())) {
                watchMouseTree(w);
            }
            return QWidget::eventFilter(watched, event);
        }
        auto* w = qobject_cast<QWidget*>(watched);
        if (!w || !content_ || locked_) return QWidget::eventFilter(watched, event);
        if (w != content_ && !content_->isAncestorOf(w)) {
            return QWidget::eventFilter(watched, event);
        }
        const auto type = event->type();
        if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove
            && type != QEvent::MouseButtonRelease) {
            return QWidget::eventFilter(watched, event);
        }
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint local = w->mapTo(this, me->position().toPoint());
        const QPoint global = me->globalPosition().toPoint();
        if (tryChromeMouse(type, local, global, me->buttons(), me->button())) {
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        if (locked_) return;
        if (!win_->isActiveWindow()) win_->raise();
        const QPoint pos = event->position().toPoint();
        if (handleBorderActionPress(pos)) return;
        if (beginResizeAt(pos, event->globalPosition().toPoint())) return;
        if (pos.y() < kTopbarH && r_ > kTopbarH * 0.5) {
            dragging_ = true;
            dragAnchor_ = event->globalPosition().toPoint() - win_->pos();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const QPoint pos = event->position().toPoint();
        if (locked_) {
            updateProximity(pos);
            updateBorderActionHover(pos);
            applyCursor(Qt::ArrowCursor, pos);
            return;
        }
        const QPoint gpos = event->globalPosition().toPoint();
        if (event->buttons() & Qt::LeftButton) {
            updateProximity(pos);
            updateBorderActionHover(pos);
            if (applyResizeDrag(gpos)) return;
            if (dragging_) {
                win_->move(gpos - dragAnchor_);
                return;
            }
        }
        syncHoverCursor(pos);
    }

    void enterEvent(QEnterEvent* event) override {
        QWidget::enterEvent(event);
        updateProximity(event->position().toPoint());
        updateBorderActionHover(event->position().toPoint());
    }

    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        if (hoverAction_ != BorderAction::None) {
            hoverAction_ = BorderAction::None;
            update();
        }
        if (controlsNear_ && !hideTimer_->isActive()) hideTimer_->start();
    }

    void mouseReleaseEvent(QMouseEvent*) override {
        endResizeDrag();
    }

private:
    enum class BorderAction { None, Minimize, Close };

    static QPen crispIconPen(const QColor& color) {
        QPen pen(color, kIconStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        pen.setCosmetic(false);
        return pen;
    }

    static void beginCrispIconPaint(QPainter& p, const QWidget* w) {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, false);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        const qreal padX = (w->width() - kIconDraw) / 2.0;
        const qreal padY = (w->height() - kIconDraw) / 2.0;
        p.translate(padX, padY);
    }

    static QRectF iconBox() {
        return QRectF(kIconBoxInset, kIconBoxInset,
                      kIconDraw - kIconBoxInset * 2.0,
                      kIconDraw - kIconBoxInset * 2.0);
    }

    static QColor accentIconColor(qreal accent) {
        const QColor expanded(theme().text);
        const QColor collapsed(148, 148, 148);
        const qreal t = std::clamp(accent, 0.0, 1.0);
        return QColor(
            static_cast<int>(collapsed.red() + (expanded.red() - collapsed.red()) * t),
            static_cast<int>(collapsed.green() + (expanded.green() - collapsed.green()) * t),
            static_cast<int>(collapsed.blue() + (expanded.blue() - collapsed.blue()) * t));
    }

    static void drawCrispRoundBox(QPainter& p, const QRectF& box, qreal radius,
                                  const QColor& color, qreal fillStrength = 0.0) {
        p.setPen(crispIconPen(color));
        if (fillStrength > 0.001) {
            QColor fill = color;
            fill.setAlphaF(std::clamp(fillStrength, 0.0, 1.0));
            p.setBrush(fill);
        } else {
            p.setBrush(Qt::NoBrush);
        }
        p.drawRoundedRect(box, radius, radius);
    }

    static void startHoverAnimation(QVariantAnimation* anim, qreal from, qreal to) {
        anim->stop();
        anim->setStartValue(from);
        anim->setEndValue(to);
        anim->start();
    }

    static void polishChromeButton(QPushButton* btn) {
        liveaio::util::polishFlatChromeButton(btn);
    }

    QRect minBtnRect() const { return QRect(width() - kBtnW * 2, 0, kBtnW, kTopbarH); }
    QRect closeBtnRect() const { return QRect(width() - kBtnW, 0, kBtnW, kTopbarH); }

    bool borderActionsEnabled() const {
        return borderShown_ && !transitioning_ && !locked_;
    }

    BorderAction hitBorderAction(const QPoint& pos) const {
        if (pos.y() < 0 || pos.y() >= kTopbarH) return BorderAction::None;
        if (closeBtnRect().contains(pos)) return BorderAction::Close;
        if (minBtnRect().contains(pos)) return BorderAction::Minimize;
        return BorderAction::None;
    }

    bool handleBorderActionPress(const QPoint& pos) {
        if (!borderActionsEnabled()) return false;
        switch (hitBorderAction(pos)) {
        case BorderAction::Minimize:
            if (onMinimize_) onMinimize_();
            return true;
        case BorderAction::Close:
            // 工具关闭按钮直接注销宿主槽，不绕一轮窗口关闭事件。
            if (onClose_) onClose_();
            else win_->close();
            return true;
        default:
            return false;
        }
    }

    void updateBorderActionHover(const QPoint& pos) {
        const BorderAction next = borderActionsEnabled() ? hitBorderAction(pos) : BorderAction::None;
        if (next == hoverAction_) return;
        hoverAction_ = next;
        update(QRect(width() - kBtnW * 2, 0, kBtnW * 2, kTopbarH));
    }

    void paintBorderActionButtons(QPainter& p) {
        const auto& C = theme();
        auto paintOne = [&](const QRect& rect, const QString& label, bool closeStyle) {
            const bool hovered = borderActionsEnabled()
                && ((closeStyle && hoverAction_ == BorderAction::Close)
                    || (!closeStyle && hoverAction_ == BorderAction::Minimize));
            if (hovered) {
                p.fillRect(rect, QColor(closeStyle ? C.closeHover : C.btnHover));
            }
            const QColor base(C.textMuted);
            const QColor hot(closeStyle ? QStringLiteral("#ffffff") : C.text);
            p.setPen(hovered ? hot : base);
            QFont f = font();
            f.setPixelSize(14);
            f.setStyleStrategy(QFont::PreferAntialias);
            p.setFont(f);
            p.setRenderHint(QPainter::TextAntialiasing, true);
            p.drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, label);
        };
        paintOne(minBtnRect(), QStringLiteral("─"), false);
        paintOne(closeBtnRect(), QStringLiteral("✕"), true);
    }

    // 边框按钮：悬浮框显示时为横线，隐藏时展开为圆角方框。
    class FrameBoxButton final : public QPushButton {
    public:
        explicit FrameBoxButton(QWidget* parent) : QPushButton(parent) {
            setFixedSize(kBtnW, kTopbarH);
            setCursor(Qt::PointingHandCursor);
            polishChromeButton(this);

            hoverAnim_ = new QVariantAnimation(this);
            hoverAnim_->setDuration(kHoverAnimMs);
            hoverAnim_->setEasingCurve(QEasingCurve::OutCubic);
            QObject::connect(hoverAnim_, &QVariantAnimation::valueChanged, this,
                             [this](const QVariant& v) {
                hoverT_ = v.toReal();
                update();
            });
        }

        void setProgress(qreal value, bool animate) {
            const qreal target = std::clamp(value, 0.0, 1.0);
            if (!animate) {
                shapeAnim_.stop();
                shapeT_ = target;
                update();
                return;
            }
            shapeAnim_.stop();
            shapeAnim_.setDuration(kLockAnimMs);
            shapeAnim_.setEasingCurve(QEasingCurve::InOutCubic);
            shapeAnim_.setStartValue(shapeT_);
            shapeAnim_.setEndValue(target);
            if (!shapeConnected_) {
                shapeConnected_ = true;
                QObject::connect(&shapeAnim_, &QVariantAnimation::valueChanged, this,
                                 [this](const QVariant& v) {
                    shapeT_ = v.toReal();
                    update();
                });
            }
            shapeAnim_.start();
        }

    protected:
        bool event(QEvent* e) override {
            if (e->type() == QEvent::Enter) {
                startHoverAnimation(hoverAnim_, hoverT_, 1.0);
            } else if (e->type() == QEvent::Leave) {
                startHoverAnimation(hoverAnim_, hoverT_, 0.0);
            }
            return QPushButton::event(e);
        }

        void paintEvent(QPaintEvent*) override {
            const auto& C = theme();
            QPainter p(this);
            if (hoverT_ > 0.001) {
                QColor fill(C.btnHover);
                fill.setAlphaF(fill.alphaF() * hoverT_);
                p.fillRect(rect(), fill);
            }

            beginCrispIconPaint(p, this);
            // shapeT_=1 边框展开 → 显示「一」；shapeT_=0 边框收起 → 显示「口」
            const qreal boxT = 1.0 - shapeT_;
            const QColor color = accentIconColor(shapeT_);
            const QRectF full = iconBox();
            const qreal halfH = full.height() * boxT / 2.0;
            const QRectF shape(full.left(), full.center().y() - halfH,
                               full.width(), halfH * 2.0);
            if (shape.height() <= kIconStroke) {
                p.setPen(crispIconPen(color));
                p.drawLine(QPointF(full.left(), full.center().y()),
                           QPointF(full.right(), full.center().y()));
            } else {
                const qreal radius = std::min(kIconBoxRadius, shape.height() / 2.0);
                drawCrispRoundBox(p, shape, radius, color);
            }
        }

    private:
        QVariantAnimation shapeAnim_{this};
        QVariantAnimation* hoverAnim_ = nullptr;
        qreal shapeT_ = 1.0;
        qreal hoverT_ = 0.0;
        bool shapeConnected_ = false;
    };

    class LockBoxButton final : public QPushButton {
    public:
        explicit LockBoxButton(QWidget* parent) : QPushButton(parent) {
            setFixedSize(kBtnW, kTopbarH);
            setCursor(Qt::PointingHandCursor);
            setFlat(true);
            polishChromeButton(this);

            lockAnim_ = new QVariantAnimation(this);
            lockAnim_->setDuration(kLockAnimMs);
            lockAnim_->setEasingCurve(QEasingCurve::OutCubic);
            QObject::connect(lockAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
                lockT_ = v.toReal();
                update();
            });

            hoverAnim_ = new QVariantAnimation(this);
            hoverAnim_->setDuration(kHoverAnimMs);
            hoverAnim_->setEasingCurve(QEasingCurve::OutCubic);
            QObject::connect(hoverAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
                hoverT_ = v.toReal();
                update();
            });
        }

        void setLocked(bool locked) {
            if (isLocked_ == locked) return;
            isLocked_ = locked;
            lockAnim_->stop();
            lockAnim_->setStartValue(lockT_);
            lockAnim_->setEndValue(locked ? 1.0 : 0.0);
            lockAnim_->start();
        }

        void setAccentProgress(qreal progress) {
            accentT_ = std::clamp(progress, 0.0, 1.0);
            update();
        }

    protected:
        bool event(QEvent* e) override {
            if (e->type() == QEvent::Enter) {
                startHoverAnimation(hoverAnim_, hoverT_, 1.0);
            } else if (e->type() == QEvent::Leave) {
                startHoverAnimation(hoverAnim_, hoverT_, 0.0);
            }
            return QPushButton::event(e);
        }

        void paintEvent(QPaintEvent*) override {
            const auto& C = theme();
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, false);
            const QRect r = rect();
            if (hoverT_ > 0.001) {
                QColor fill(C.btnHover);
                fill.setAlpha(static_cast<int>(fill.alpha() * hoverT_));
                p.fillRect(r, fill);
            }

            beginCrispIconPaint(p, this);
            const QColor iconColor = accentIconColor(accentT_);
            drawCrispRoundBox(p, iconBox(), kIconBoxRadius, iconColor, lockT_);
        }

    private:
        bool isLocked_ = false;
        qreal accentT_ = 1.0;
        qreal lockT_ = 0.0;
        qreal hoverT_ = 0.0;
        QVariantAnimation* lockAnim_ = nullptr;
        QVariantAnimation* hoverAnim_ = nullptr;
    };

    static qreal centerX() { return kCircleOff + kBtnW / 2.0; }
    static qreal centerY() { return kTopbarH / 2.0; }

    void refreshChrome() {
        frame_->update();
        lock_->update();
        update(QRect(width() - kBtnW * 2, 0, kBtnW * 2, kTopbarH));
    }

    int proximityRadiusPx() const {
        const QScreen* screen = win_ ? win_->screen() : QApplication::primaryScreen();
        const qreal dpi = screen ? screen->logicalDotsPerInch() : 96.0;
        return std::max(48, static_cast<int>(std::lround(dpi * 3.0 / 2.54)));
    }

    bool pointerNearControls() const {
        const QPoint local = mapFromGlobal(QCursor::pos());
        const QPoint center(static_cast<int>(centerX()), static_cast<int>(centerY()));
        return QLineF(local, center).length() <= proximityRadiusPx();
    }

    void updateProximity(const QPoint& local) {
        if (transitioning_) return;
        const QPoint center(static_cast<int>(centerX()), static_cast<int>(centerY()));
        if (QLineF(local, center).length() <= proximityRadiusPx()) {
            hideTimer_->stop();
            controlsNear_ = true;
            refreshControlVisibility();
        } else if (controlsNear_ && !hideTimer_->isActive()) {
            hideTimer_->start();
        }
    }

    void hideControlsNow() {
        if (transitioning_) return;
        controlsNear_ = false;
        refreshControlVisibility();
    }

    void refreshControlVisibility() {
        const bool showChrome = borderShown_ || controlsNear_ || transitioning_;
        frame_->setVisible(showChrome);
        lock_->setVisible(showChrome);
        leftBox_->raise();
        frame_->raise();
        lock_->raise();
        update(QRect(width() - kBtnW * 2, 0, kBtnW * 2, kTopbarH));
    }

    void requestThrottledContentLayout() {
        if (!contentThrottle_) {
            contentThrottle_ = new QTimer(this);
            contentThrottle_->setSingleShot(true);
            QObject::connect(contentThrottle_, &QTimer::timeout, this, [this]() {
                if (freeze_->frozen()) onContentGeometryWhileResizing();
            });
        }
        if (!contentThrottle_->isActive()) contentThrottle_->start(kContentThrottleMs);
    }

    void handleResizeResume() {
        if (contentThrottle_) contentThrottle_->stop();
        onContentGeometryChanged();
        onResizeResume();
    }

    void watchMouseTree(QWidget* w) {
        if (!w) return;
        w->installEventFilter(this);
        w->setMouseTracking(true);
        const auto kids = w->findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
        for (QWidget* c : kids) watchMouseTree(c);
    }

    void applyCursor(Qt::CursorShape shape, const QPoint& local) {
        auto set = [shape](QWidget* w) {
            if (w && w->cursor().shape() != shape) w->setCursor(shape);
        };
        set(this);
        set(win_);
        set(content_);
        if (QWidget* hit = childAt(local)) {
            set(hit);
            for (QWidget* p = hit->parentWidget(); p && p != this; p = p->parentWidget()) {
                set(p);
            }
        }
#ifdef Q_OS_WIN
        LPCWSTR id = IDC_ARROW;
        switch (shape) {
        case Qt::SizeHorCursor: id = IDC_SIZEWE; break;
        case Qt::SizeVerCursor: id = IDC_SIZENS; break;
        case Qt::SizeFDiagCursor: id = IDC_SIZENWSE; break;
        case Qt::SizeBDiagCursor: id = IDC_SIZENESW; break;
        case Qt::OpenHandCursor:
        case Qt::ClosedHandCursor: id = IDC_HAND; break;
        default: break;
        }
        SetCursor(LoadCursor(nullptr, id));
#endif
    }

    bool beginResizeAt(const QPoint& localPos, const QPoint& globalPos) {
        const Edge edge = edgeAt(localPos);
        if (edge == Edge::None) return false;
        freeze_->begin();
        resizing_ = true;
        onResizeBegin();
        resizeEdge_ = edge;
        resizeStartGeo_ = win_->geometry();
        resizeStartPos_ = globalPos;
        applyCursor(cursorFor(edge), localPos);
        grabMouse();
        return true;
    }

    bool applyResizeDrag(const QPoint& globalPos) {
        if (!resizing_) return false;
        const QRect geo = resizeGeometry(resizeEdge_, globalPos - resizeStartPos_, resizeStartGeo_);
        if (geo.width() >= win_->minimumWidth() && geo.height() >= win_->minimumHeight()
            && geo.width() > 0 && geo.height() > 0) {
            win_->setGeometry(geo);
        }
        return true;
    }

    void endResizeDrag() {
        const bool wasResize = resizing_;
        if (wasResize && QWidget::mouseGrabber() == this) releaseMouse();
        dragging_ = false;
        resizing_ = false;
        resizeEdge_ = Edge::None;
        applyCursor(Qt::ArrowCursor, mapFromGlobal(QCursor::pos()));
        if (wasResize) freeze_->end();
    }

    // 内容层（叶子画布等）盖住边框热区时，仍由外壳处理缩放。
    bool tryChromeMouse(QEvent::Type type, const QPoint& local, const QPoint& global,
                        Qt::MouseButtons buttons, Qt::MouseButton button) {
        if (type == QEvent::MouseButtonPress) {
            if (button != Qt::LeftButton) return false;
            if (locked_) return false;
            if (!win_->isActiveWindow()) win_->raise();
            if (handleBorderActionPress(local)) return true;
            return beginResizeAt(local, global);
        }
        if (type == QEvent::MouseMove) {
            updateProximity(local);
            updateBorderActionHover(local);
            if (locked_) return false;
            if (buttons & Qt::LeftButton) {
                if (applyResizeDrag(global)) return true;
            }
            if (resizing_) return true;
            const Edge edge = edgeAt(local);
            if (edge != Edge::None && !(buttons & Qt::LeftButton)) {
                applyCursor(cursorFor(edge), local);
                return true;
            }
            return false;
        }
        if (type == QEvent::MouseButtonRelease) {
            if (!resizing_) return false;
            endResizeDrag();
            return true;
        }
        return false;
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

    static constexpr int kContentThrottleMs = 80;  // 拖拽中内容约 12fps

    QMainWindow* win_ = nullptr;
    QWidget* leftBox_ = nullptr;
    QWidget* content_ = nullptr;
    FrameBoxButton* frame_ = nullptr;
    LockBoxButton* lock_ = nullptr;
    QTimer* hideTimer_ = nullptr;
    QTimer* contentThrottle_ = nullptr;
    std::unique_ptr<liveaio::util::OverlayResizeFreeze> freeze_;
    std::function<void()> onFrameClicked_;
    std::function<void()> onLockClicked_;
    std::function<void()> onMinimize_;
    std::function<void()> onClose_;
    qreal r_ = 0.0;
    bool borderShown_ = true;
    bool locked_ = false;
    bool transitioning_ = false;
    bool controlsNear_ = false;
    BorderAction hoverAction_ = BorderAction::None;
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
        setStyleSheet(liveaio::util::popupChromeQss());
        liveaio::util::onThemeChange(this, [this](const QString&) {
            setStyleSheet(liveaio::util::popupChromeQss());
        });

        anim_ = new QVariantAnimation(this);
        anim_->setDuration(RippleOverlayRoot::kAnimMs);
        anim_->setEasingCurve(QEasingCurve::OutQuart);
        QObject::connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            animR_ = v.toReal();
            if (root_) root_->setRadius(animR_);
        });
        QObject::connect(anim_, &QVariantAnimation::finished, this, [this]() {
            if (root_) root_->setChromeState(shown_, locked_, false);
        });
    }

    void setOnClosed(std::function<void()> cb) { onClosed_ = std::move(cb); }

    void setGeometryKey(const QString& key) { geoKey_ = key; }

    RippleOverlayRoot* takeRoot() {
        RippleOverlayRoot* r = root_;
        setCentralWidget(nullptr);
        root_ = nullptr;
        return r;
    }

    void toggleFrame() {
        if (!root_) return;
        if (locked_ && !shown_) return;
        shown_ = !shown_;
        anim_->stop();
        root_->setChromeState(shown_, locked_, true);
        anim_->setStartValue(animR_);
        anim_->setEndValue(shown_ ? std::max(root_->maxRadius(), 1.0) : 0.0);
        anim_->start();
    }

    void setFrameShown(bool shown) {
        if (shown_ == shown || (shown && locked_)) return;
        toggleFrame();
    }

    void setLocked(bool locked) {
        if (locked_ == locked) return;
        const bool wasLocked = locked_;
        locked_ = locked;
        if (locked_ && shown_) {
            shown_ = false;
            anim_->stop();
            if (root_) root_->setChromeState(false, true, true);
            anim_->setStartValue(animR_);
            anim_->setEndValue(0.0);
            anim_->start();
            return;
        }
        if (!locked_ && wasLocked) {
            shown_ = true;
            anim_->stop();
            if (root_) root_->setChromeState(true, false, true);
            anim_->setStartValue(animR_);
            anim_->setEndValue(root_ ? std::max(root_->maxRadius(), 1.0) : 0.0);
            anim_->start();
            return;
        }
        if (root_) root_->setChromeState(shown_, locked_, false);
    }

    bool frameShown() const { return shown_; }
    bool locked() const { return locked_; }

    bool applyChromeCommand(const QString& action) {
        if (action == QStringLiteral("frame.toggle")) setFrameShown(!shown_);
        else if (action == QStringLiteral("frame.show")) setFrameShown(true);
        else if (action == QStringLiteral("frame.hide")) setFrameShown(false);
        else if (action == QStringLiteral("lock.toggle")) setLocked(!locked_);
        else if (action == QStringLiteral("lock")) setLocked(true);
        else if (action == QStringLiteral("unlock")) setLocked(false);
        else return false;
        return true;
    }

    void resetChromeForOpen() {
        anim_->stop();
        shown_ = true;
        locked_ = false;
    }

    void minimizeOverlay() {
        if (shown_) toggleFrame();
        lower();
    }

    void syncRadiusAfterResize() {
        if (!root_) return;
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

protected:
    void attachRoot(RippleOverlayRoot* root) {
        root_ = root;
        setCentralWidget(root_);
        root_->setOnFrameClicked([this]() { toggleFrame(); });
        root_->setOnLockClicked([this]() { setLocked(!locked_); });
        root_->setOnMinimize([this]() { minimizeOverlay(); });
        root_->setChromeState(shown_, locked_, false);
    }

    RippleOverlayRoot* root() const { return root_; }
    bool animRunning() const { return anim_->state() == QAbstractAnimation::Running; }

    void showEvent(QShowEvent* event) override {
        QMainWindow::showEvent(event);
        if (!firstShow_) return;
        firstShow_ = false;
        // 布局完成后才知道 maxRadius，延后一帧初始化半径。
        QTimer::singleShot(0, this, [this]() {
            if (!root_) return;
            qreal r = root_->maxRadius();
            if (r <= 0) r = 800.0;
            animR_ = r;
            shown_ = true;
            root_->setRadius(r);
            root_->setChromeState(true, locked_, false);
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

    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override {
#ifdef Q_OS_WIN
        if (eventType == QByteArrayLiteral("windows_generic_MSG")
            || eventType == QByteArrayLiteral("windows_dispatcher_MSG")) {
            auto* msg = static_cast<MSG*>(message);
            if (msg->message == WM_NCHITTEST && result) {
                if (!root_) {
                    *result = HTTRANSPARENT;
                    return true;
                }
                const QPoint lp = root_->mapFromGlobal(QCursor::pos());
                if (root_->wantsMouseAt(lp)) {
                    root_->syncHoverCursor(lp);
                    return QMainWindow::nativeEvent(eventType, message, result);
                }
                *result = HTTRANSPARENT;
                return true;
            }
            if (msg->message == WM_SETCURSOR && result && root_) {
                const QPoint lp = root_->mapFromGlobal(QCursor::pos());
                if (root_->wantsMouseAt(lp)) {
                    root_->syncHoverCursor(lp);
                    *result = TRUE;
                    return true;
                }
            }
        }
#endif
        return QMainWindow::nativeEvent(eventType, message, result);
    }

private:
    QString geoKey_;
    RippleOverlayRoot* root_ = nullptr;
    QVariantAnimation* anim_ = nullptr;
    qreal animR_ = 0.0;
    bool shown_ = true;
    bool locked_ = false;
    bool firstShow_ = true;
    std::function<void()> onClosed_;
};

class SharedOverlayShell final : public RippleOverlayWindow {
public:
    explicit SharedOverlayShell(const QString& key)
        : RippleOverlayWindow(QStringLiteral("悬浮窗"), key) {}

    void prepare(const QString& title, const QString& geoKey, int minW, int minH, int defW, int defH) {
        resetChromeForOpen();
        setWindowTitle(title);
        setGeometryKey(geoKey);
        setMinimumSize(minW, minH);
        restoreGeometryFromConfig(defW, defH);
    }

    void mountRoot(RippleOverlayRoot* newRoot, WidgetDeferredDestroy& destroyer) {
        if (!newRoot) return;
        RippleOverlayRoot* old = takeRoot();
        if (old && old != newRoot) destroyer.enqueue(old);
        attachRoot(newRoot);
        syncRadiusAfterResize();
    }

    void afterShowContent() {
        syncRadiusAfterResize();
        if (root()) root()->updateGeometry();
    }

    bool hasMountedContent() const { return root() != nullptr; }
};

enum class OverlayToolId { None, Danmu, Overtime, Leaf };

class OverlayHostService final : public QObject {
public:
    static OverlayHostService& instance() {
        static OverlayHostService* host = new OverlayHostService(qApp);
        return *host;
    }

    bool isToolActive(OverlayToolId id) const {
        const Slot* s = slot(id);
        return s && s->shell && s->shell->isVisible() && s->shell->hasMountedContent();
    }

    SharedOverlayShell* shell(OverlayToolId id) {
        Slot* s = slot(id);
        if (!s) return nullptr;
        ensureShell(id, *s);
        return s->shell;
    }

    void show(OverlayToolId tool, const QString& title, const QString& geoKey, int minW, int minH,
              int defW, int defH, RippleOverlayRoot* root, std::function<void()> onClosed) {
        if (!root) return;

        Slot* s = slot(tool);
        if (!s) return;
        if (s->shell && s->shell->hasMountedContent()) detachContent(*s, s->closedCb);
        s->closedCb = std::move(onClosed);

        ensureShell(tool, *s);
        s->shell->prepare(title, geoKey, minW, minH, defW, defH);
        s->shell->setOnClosed([this, tool]() { teardown(tool); });
        s->shell->mountRoot(root, s->destroyer);
        root->setOnClose([this, tool]() { teardown(tool); });
        s->shell->show();
        s->shell->activateWindow();
        QPointer<SharedOverlayShell> guard(s->shell);
        QTimer::singleShot(0, s->shell, [guard]() {
            if (guard) guard->afterShowContent();
        });
    }

    void teardown(OverlayToolId tool, std::function<void()> done = nullptr) {
        Slot* s = slot(tool);
        if (!s) return;
        std::function<void()> cb = done ? done : s->closedCb;
        s->closedCb = nullptr;
        detachContent(*s, cb);
    }

    bool command(OverlayToolId tool, const QString& action) {
        Slot* s = slot(tool);
        if (!s || !s->shell || !isToolActive(tool)) return false;
        if (action == QStringLiteral("close")) {
            teardown(tool);
            return true;
        }
        return s->shell->applyChromeCommand(action);
    }

    int stateBits(OverlayToolId tool) const {
        const Slot* s = slot(tool);
        if (!s || !s->shell || !isToolActive(tool)) return 0;
        return 1 | (s->shell->frameShown() ? 2 : 0) | (s->shell->locked() ? 4 : 0);
    }

private:
    struct Slot {
        explicit Slot(QObject* parent) : destroyer(parent) {}
        SharedOverlayShell* shell = nullptr;
        WidgetDeferredDestroy destroyer;
        std::function<void()> closedCb;
    };

    explicit OverlayHostService(QObject* parent)
        : QObject(parent), danmu_(this), overtime_(this), leaf_(this) {}

    Slot* slot(OverlayToolId id) {
        if (id == OverlayToolId::Danmu) return &danmu_;
        if (id == OverlayToolId::Overtime) return &overtime_;
        if (id == OverlayToolId::Leaf) return &leaf_;
        return nullptr;
    }
    const Slot* slot(OverlayToolId id) const {
        if (id == OverlayToolId::Danmu) return &danmu_;
        if (id == OverlayToolId::Overtime) return &overtime_;
        if (id == OverlayToolId::Leaf) return &leaf_;
        return nullptr;
    }

    // 活跃实例立即注销；轻量壳留在单例宿主池中供再次打开复用。
    // 每个可见顶层窗口仍需要独立原生表面，内容与共享资源不重复常驻。
    void detachContent(Slot& s, std::function<void()> notify) {
        if (s.shell) s.shell->hide();
        RippleOverlayRoot* root = s.shell ? s.shell->takeRoot() : nullptr;
        if (root) root->setOnClose(nullptr);
        if (notify) notify();
        if (root) s.destroyer.enqueue(root);
    }

    void ensureShell(OverlayToolId tool, Slot& s) {
        if (s.shell) return;
        QString key = QStringLiteral("overlay_window_geometry");
        if (tool == OverlayToolId::Danmu) key = QStringLiteral("danmu_window_geometry");
        else if (tool == OverlayToolId::Overtime) key = QStringLiteral("overtime_window_geometry");
        else if (tool == OverlayToolId::Leaf) key = QStringLiteral("leaf_window_geometry");
        s.shell = new SharedOverlayShell(key);
    }

    Slot danmu_;
    Slot overtime_;
    Slot leaf_;
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
