// tools/leaf_tool.cpp — 捡叶子小游戏：设置页 + 透明悬浮窗（双碰撞箱 + 共享贴图）。

namespace liveaio::tools::leaf {

static const QString kGeoKey = QStringLiteral("leaf_window_geometry");
static const QString kTrashPosKey = QStringLiteral("leaf_trash_pos");
static const QString kMaxLeavesKey = QStringLiteral("leaf_max_leaves");
static const QString kLeafScaleKey = QStringLiteral("leaf_scale_percent");
static const QString kTrashScaleKey = QStringLiteral("leaf_trash_scale_percent");
static const QString kViewportSizeKey = QStringLiteral("leaf_last_viewport_size");
static const QString kLeafImage = QStringLiteral("resources/image/zaidoopro-painting-8032889.png");
static const QString kTrashImage = QStringLiteral("resources/image/trash_can.png");
static const QString kTrashOpenImage = QStringLiteral("resources/image/trash_can_open.png");

// 物理/绘制尺寸（cm）。刚体是沿贴图主轴延展的长条八边形。
static constexpr qreal kLeafSideCm = 1.0;
static constexpr qreal kRigidHalfLengthFrac = 0.45;
static constexpr qreal kRigidHalfWidthFrac = 0.14;
static constexpr qreal kRigidBevelFrac = 0.055;
static constexpr qreal kSoftRadiusFrac = 0.24;
static constexpr qreal kSoftHalfLenFrac = 0.34;
static constexpr qreal kArtAngleRad = -0.7853981633974483;  // -45°
static constexpr qreal kTrashWCm = 2.0;
static constexpr qreal kTrashHCm = 2.67;
static constexpr qreal kTrashHitPadCm = 0.25;
static constexpr qreal kTrashNearPadCm = 0.85;
static constexpr qreal kLidAnimSec = 0.12;
static constexpr qreal kHaloAnimSec = 0.10;
static constexpr qreal kLeafHaloOuterCm = 0.22;
static constexpr qreal kTrashHaloOuterCm = 0.28;
static constexpr qreal kTrashHaloAmount = 0.72;
static constexpr qreal kLeafHaloAmount = 0.90;
static constexpr qreal kWorldPadCm = 0.15;
static constexpr int kMinLeaves = 0;
static constexpr int kMaxLeavesHard = 240;
static constexpr int kTickMs = 16;
static constexpr int kPositionIters = 8;
static constexpr int kResizeSettleIters = 18;
static constexpr int kDragPositionIters = 3;
static constexpr int kMaxDragSubsteps = 8;
static constexpr qreal kGravity = 1400.0;
static constexpr qreal kDamping = 0.905;
static constexpr qreal kAngDamping = 0.750;
static constexpr qreal kMaxAngVel = 0.9;
static constexpr qreal kMaxSpeed = 165.0;
static constexpr qreal kCollisionTorque = 0.001;
static constexpr qreal kPositionSlopPx = 0.25;
static constexpr qreal kPositionMaxCorrectionFrac = 0.65;
static constexpr qreal kDragStepFrac = 0.70;
static constexpr qreal kDragPushRatio = 0.55;
static constexpr qreal kFriction = 0.32;
static constexpr qreal kRestitution = 0.0;
static constexpr qreal kFloorFriction = 0.72;
static constexpr qreal kPi = 3.14159265358979323846;

static qreal screenDpi() {
    static qreal dpi = 0.0;
    if (dpi <= 0.0) {
        auto* screen = QApplication::primaryScreen();
        dpi = screen ? screen->logicalDotsPerInch() : 96.0;
    }
    return dpi;
}

static qreal cmToPxF(qreal cm) { return screenDpi() / 2.54 * cm; }
static int cmToPx(qreal cm) { return std::max(1, static_cast<int>(std::lround(cmToPxF(cm)))); }

static qreal leafVisualPx(qreal scale = 1.0) { return cmToPxF(kLeafSideCm) * scale; }
static qreal rigidHalfLength(qreal scale = 1.0) {
    return leafVisualPx(scale) * kRigidHalfLengthFrac;
}
static qreal rigidHalfWidth(qreal scale = 1.0) {
    return leafVisualPx(scale) * kRigidHalfWidthFrac;
}
static qreal rigidBevel(qreal scale = 1.0) {
    return leafVisualPx(scale) * kRigidBevelFrac;
}
static qreal rigidRadius(qreal scale = 1.0) {
    return std::hypot(rigidHalfLength(scale), rigidHalfWidth(scale));
}
static qreal softR(qreal scale = 1.0) { return leafVisualPx(scale) * kSoftRadiusFrac; }
static qreal softHalf(qreal scale = 1.0) { return leafVisualPx(scale) * kSoftHalfLenFrac; }

static qreal leafPackSide(qreal leafScale = 1.0) {
    // 最坏倾角下仍能包住八边形刚体的轴对齐正方形。
    return 2.0 * rigidRadius(leafScale);
}

static int maxLeavesForSize(const QSize& contentSize, qreal leafScale = 1.0) {
    const qreal pad = cmToPxF(kWorldPadCm);
    const qreal w = std::max(0.0, contentSize.width() - 2.0 * pad);
    const qreal h = std::max(0.0, contentSize.height() - 2.0 * pad);
    const qreal cell = std::max(1.0, leafPackSide(leafScale));
    const int cols = static_cast<int>(std::floor(w / cell));
    const int rows = static_cast<int>(std::floor(h / cell));
    return std::clamp(cols * rows, kMinLeaves, kMaxLeavesHard);
}

static int scalePercent(const QString& key) {
    return std::clamp(configValue(key, 100).toInt(), 50, 200);
}

static int capacityLower(int theoretical) {
    return static_cast<int>(std::floor(std::max(0, theoretical) * 0.20));
}

static int capacityUpper(int theoretical) {
    return static_cast<int>(std::floor(std::max(0, theoretical) * 0.80));
}

static QSize savedViewportSize() {
    const QVariantMap m = configValue(kViewportSizeKey).toMap();
    const int defaultSide = cmToPx(12.0);
    return QSize(std::max(1, m.value(QStringLiteral("w"), defaultSide).toInt()),
                 std::max(1, m.value(QStringLiteral("h"),
                                     static_cast<int>(defaultSide * 1.25)).toInt()));
}

static int configuredMaxLeaves(int theoretical) {
    const int lower = capacityLower(theoretical);
    const int upper = std::max(lower, capacityUpper(theoretical));
    return std::clamp(configValue(kMaxLeavesKey, upper).toInt(), lower, upper);
}

static QPixmap cropOpaque(const QPixmap& src) {
    if (src.isNull()) return src;
    const QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32);
    int minx = img.width(), miny = img.height(), maxx = -1, maxy = -1;
    for (int y = 0; y < img.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) < 20) continue;
            minx = std::min(minx, x);
            miny = std::min(miny, y);
            maxx = std::max(maxx, x);
            maxy = std::max(maxy, y);
        }
    }
    if (maxx < minx) return src;
    QRect r(minx, miny, maxx - minx + 1, maxy - miny + 1);
    r.adjust(-4, -4, 4, 4);
    return QPixmap::fromImage(img.copy(r.intersected(img.rect())));
}

static QVector<int> boxBlurAlpha(const QVector<int>& src, int w, int h, int radius) {
    if (radius <= 0 || w <= 0 || h <= 0) return src;
    QVector<int> horizontal(w * h);
    QVector<int> out(w * h);
    for (int y = 0; y < h; ++y) {
        int sum = 0;
        for (int x = -radius; x <= radius; ++x) {
            sum += src[y * w + std::clamp(x, 0, w - 1)];
        }
        for (int x = 0; x < w; ++x) {
            horizontal[y * w + x] = sum / (radius * 2 + 1);
            sum -= src[y * w + std::clamp(x - radius, 0, w - 1)];
            sum += src[y * w + std::clamp(x + radius + 1, 0, w - 1)];
        }
    }
    for (int x = 0; x < w; ++x) {
        int sum = 0;
        for (int y = -radius; y <= radius; ++y) {
            sum += horizontal[std::clamp(y, 0, h - 1) * w + x];
        }
        for (int y = 0; y < h; ++y) {
            out[y * w + x] = sum / (radius * 2 + 1);
            sum -= horizontal[std::clamp(y - radius, 0, h - 1) * w + x];
            sum += horizontal[std::clamp(y + radius + 1, 0, h - 1) * w + x];
        }
    }
    return out;
}

static QPixmap makeHaloPixmap(const QPixmap& src, qreal finalMinSidePx, qreal outerPx) {
    if (src.isNull() || finalMinSidePx <= 0.0 || outerPx <= 0.0) return {};
    const QImage input = src.toImage().convertToFormat(QImage::Format_ARGB32);
    const qreal sourceScale = std::min(input.width(), input.height()) / finalMinSidePx;
    const int pad = std::max(2, static_cast<int>(std::ceil(outerPx * sourceScale)));
    const int blur = std::max(1, static_cast<int>(std::ceil(pad * 0.72)));
    const int w = input.width() + pad * 2;
    const int h = input.height() + pad * 2;
    QVector<int> alpha(w * h, 0);
    for (int y = 0; y < input.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(input.constScanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            alpha[(y + pad) * w + x + pad] = qAlpha(line[x]);
        }
    }
    // 两次方框模糊近似柔和高斯，资源加载时只计算一次。
    alpha = boxBlurAlpha(alpha, w, h, blur);
    alpha = boxBlurAlpha(alpha, w, h, std::max(1, blur / 2));
    QImage halo(w, h, QImage::Format_ARGB32);
    halo.fill(Qt::transparent);
    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(halo.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int a = std::clamp(static_cast<int>(alpha[y * w + x] * 0.82), 0, 220);
            line[x] = qRgba(255, 226, 166, a);
        }
    }
    return QPixmap::fromImage(halo);
}

static void stepToward(qreal* v, qreal target, qreal dt, qreal sec) {
    const qreal step = dt / std::max(0.001, sec);
    if (*v < target) *v = std::min(target, *v + step);
    else *v = std::max(target, *v - step);
}

static void drawCenteredHalo(QPainter& p, const QRectF& dst, const QPixmap& haloPm,
                             qreal amount, qreal expandPx) {
    if (amount <= 0.01 || haloPm.isNull() || expandPx <= 0.5) return;
    p.save();
    p.setOpacity(amount);
    p.drawPixmap(dst.adjusted(-expandPx, -expandPx, expandPx, expandPx).toRect(), haloPm);
    p.restore();
}

struct LeafSharedAssets {
    QPixmap leafPm;
    QPixmap leafHaloPm;
    QPixmap trashPm;
    QPixmap trashOpenPm;
    QPixmap trashHaloPm;
    QPixmap trashOpenHaloPm;

    static LeafSharedAssets& instance() {
        static LeafSharedAssets* g = nullptr;
        if (!g) {
            g = new LeafSharedAssets;
            g->load();
        }
        return *g;
    }

    static QString resolvePath(const QString& rel) {
        const QString primary = QDir(g_appRoot).filePath(rel);
        if (QFile::exists(primary)) return primary;
        // 兼容旧布局 image/...
        const QString alt = QDir(g_appRoot).filePath(rel.section(QLatin1Char('/'), 1));
        if (QFile::exists(alt)) return alt;
        return primary;
    }

    void load() {
        leafPm = QPixmap(resolvePath(kLeafImage));
        trashPm = QPixmap(resolvePath(kTrashImage));
        trashOpenPm = QPixmap(resolvePath(kTrashOpenImage));
        if (leafPm.isNull()) {
            leafPm = QPixmap(64, 64);
            leafPm.fill(QColor(80, 160, 60));
        } else {
            leafPm = cropOpaque(leafPm);
            const int side = std::max(64, static_cast<int>(std::lround(leafVisualPx() * 8.0)));
            if (leafPm.width() > side || leafPm.height() > side) {
                leafPm = leafPm.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        }
        const int tw = std::max(64, static_cast<int>(std::lround(cmToPxF(kTrashWCm) * 4.0)));
        const int th = std::max(80, static_cast<int>(std::lround(cmToPxF(kTrashHCm) * 4.0)));
        auto scaleTrash = [tw, th](QPixmap& pm, const QColor& fallback) {
            if (pm.isNull()) {
                pm = QPixmap(tw, th);
                pm.fill(fallback);
                return;
            }
            pm = pm.scaled(tw, th, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        };
        scaleTrash(trashPm, QColor(210, 205, 195));
        scaleTrash(trashOpenPm, QColor(210, 205, 195));
        leafHaloPm = makeHaloPixmap(leafPm, leafVisualPx(), cmToPxF(kLeafHaloOuterCm));
        trashHaloPm = makeHaloPixmap(
            trashPm, cmToPxF(std::min(kTrashWCm, kTrashHCm)), cmToPxF(kTrashHaloOuterCm));
        trashOpenHaloPm = makeHaloPixmap(
            trashOpenPm, cmToPxF(std::min(kTrashWCm, kTrashHCm)), cmToPxF(kTrashHaloOuterCm));
    }
};

class LeafScalePreview final : public QWidget {
public:
    explicit LeafScalePreview(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(148);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setScales(int leafPercent, int trashPercent) {
        leafScale_ = std::clamp(leafPercent, 50, 200) / 100.0;
        trashScale_ = std::clamp(trashPercent, 50, 200) / 100.0;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        QColor panel = palette().color(QPalette::Base);
        panel.setAlpha(95);
        QColor line = palette().color(QPalette::Mid);
        line.setAlpha(100);
        p.setPen(QPen(line, 1.0));
        p.setBrush(panel);
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 9.0, 9.0);

        const auto& assets = LeafSharedAssets::instance();
        const qreal pxPerCm = 25.0;
        const qreal leafSide = kLeafSideCm * pxPerCm * leafScale_;
        const QSizeF trashSize(kTrashWCm * pxPerCm * trashScale_,
                               kTrashHCm * pxPerCm * trashScale_);
        const qreal baseline = height() - 13.0;
        const QRectF leafRect(width() * 0.28 - leafSide * 0.5,
                              baseline - leafSide, leafSide, leafSide);
        const QRectF trashRect(width() * 0.72 - trashSize.width() * 0.5,
                               baseline - trashSize.height(),
                               trashSize.width(), trashSize.height());
        p.setPen(QPen(line, 1.0));
        p.drawLine(QPointF(12.0, baseline), QPointF(width() - 12.0, baseline));
        if (!assets.leafPm.isNull()) p.drawPixmap(leafRect.toRect(), assets.leafPm);
        if (!assets.trashPm.isNull()) p.drawPixmap(trashRect.toRect(), assets.trashPm);
    }

private:
    qreal leafScale_ = 1.0;
    qreal trashScale_ = 1.0;
};

static QPointF closestPointOnSegment(const QPointF& a, const QPointF& b, const QPointF& p) {
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    if (len2 < 1e-8) return a;
    const qreal t = std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0);
    return a + ab * t;
}

enum class LeafState { Idle, Dragging, Removing };

struct LeafBody {
    QPointF pos;
    QPointF vel;
    qreal angle = 0.0;
    qreal angVel = 0.0;
    qreal alpha = 1.0;
    qreal removeT = 0.0;
    LeafState state = LeafState::Idle;
    quint64 id = 0;
};

class LeafCanvas final : public QWidget {
public:
    explicit LeafCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        leafScale_ = scalePercent(kLeafScaleKey) / 100.0;
        trashScale_ = scalePercent(kTrashScaleKey) / 100.0;
        const int savedTheory = maxLeavesForSize(savedViewportSize(), leafScale_);
        preferredMaxLeaves_ = configuredMaxLeaves(savedTheory);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
        LeafSharedAssets::instance();
        loadTrashPos();
        tick_ = new QTimer(this);
        tick_->setInterval(kTickMs);
        QObject::connect(tick_, &QTimer::timeout, this, [this]() { onTick(); });
        tick_->start();
    }

    int aliveCount() const {
        int n = 0;
        for (const LeafBody& L : leaves_) {
            if (L.state != LeafState::Removing) ++n;
        }
        return n;
    }

    int maxLeaves() const { return maxLeaves_; }
    int theoreticalCapacity() const { return theoreticalCapacity_; }
    int capacityMinimum() const { return capacityLower(theoreticalCapacity_); }
    int capacityMaximum() const {
        return std::max(capacityMinimum(), capacityUpper(theoreticalCapacity_));
    }
    int pendingCount() const { return pendingSpawns_; }
    int leafScalePercent() const { return qRound(leafScale_ * 100.0); }
    int trashScalePercent() const { return qRound(trashScale_ * 100.0); }

    void applySettings(int maxLeaves, int leafPercent, int trashPercent) {
        leafPercent = std::clamp(leafPercent, 50, 200);
        trashPercent = std::clamp(trashPercent, 50, 200);
        leafScale_ = leafPercent / 100.0;
        trashScale_ = trashPercent / 100.0;
        preferredMaxLeaves_ = std::max(0, maxLeaves);
        writeConfigValue(kLeafScaleKey, leafPercent);
        writeConfigValue(kTrashScaleKey, trashPercent);
        writeConfigValue(kMaxLeavesKey, preferredMaxLeaves_);
        notifyGeometryChanged();
    }

    void enqueueSpawn(int count = 1) {
        pendingSpawns_ += std::max(1, count);
        flushSpawns();
        emitStats();
        update();
    }

    void beginResizePause() {
        if (resizePaused_) return;
        resizePaused_ = true;
        if (tick_->isActive()) tick_->stop();
    }

    void commitViewport(const QRect& geometry) {
        suppressResizeCommit_ = true;
        setGeometry(geometry);
        suppressResizeCommit_ = false;
        notifyGeometryChanged();
    }

    void endResizePause() {
        resizePaused_ = false;
        if (!tick_->isActive()) tick_->start();
    }

    bool resizePaused() const { return resizePaused_; }

    bool hitInteractive(const QPointF& pos) const {
        if (draggingTrash_ || dragId_ != 0) return true;
        if (trashNearRect().contains(pos)) return true;
        for (int i = leaves_.size() - 1; i >= 0; --i) {
            const LeafBody& L = leaves_[i];
            if (L.state == LeafState::Removing) continue;
            if (hitSoft(L, pos)) return true;
        }
        return false;
    }

    void setStatsCallback(std::function<void()> cb) { statsCb_ = std::move(cb); }

    void notifyGeometryChanged() {
        ensureTrashPosition();
        updateWorld();
        cullOverflowImmediate();
        clampTrashToWorld();
        squeezeIntoBounds();
        update();
        emitStats();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (!resizePaused_ && !suppressResizeCommit_) notifyGeometryChanged();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const auto& assets = LeafSharedAssets::instance();
        const qreal side = leafVisualPx(leafScale_);

        for (const LeafBody& L : leaves_) {
            if (L.state == LeafState::Dragging) continue;
            drawLeaf(p, L, side, assets.leafPm, assets.leafHaloPm, 0.0);
        }

        drawTrash(p, assets);

        for (const LeafBody& L : leaves_) {
            if (L.state != LeafState::Dragging) continue;
            drawLeaf(p, L, side, assets.leafPm, assets.leafHaloPm, leafHalo_);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        const QPointF pos = event->position();
        // 垃圾桶图层在闲置叶子之上：先点垃圾桶。
        if (trashGrabRect().contains(pos)) {
            draggingTrash_ = true;
            dragOffset_ = trashCenter() - pos;
            setCursor(Qt::ClosedHandCursor);
            update();
            return;
        }
        for (int i = leaves_.size() - 1; i >= 0; --i) {
            LeafBody& L = leaves_[i];
            if (L.state == LeafState::Removing) continue;
            if (!hitSoft(L, pos)) continue;
            L.state = LeafState::Dragging;
            L.vel = {};
            L.angVel = 0.0;
            dragId_ = L.id;
            dragOffset_ = L.pos - pos;
            dragTarget_ = L.pos;
            dragVelocity_ = {};
            dragTargetValid_ = true;
            update();
            return;
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const QPointF pos = event->position();
        const bool hover = trashNearRect().contains(pos);
        if (hoverTrash_ != hover) {
            hoverTrash_ = hover;
            if (!(event->buttons() & Qt::LeftButton)) update();
        }
        if (!(event->buttons() & Qt::LeftButton)) {
            setCursor(hover ? Qt::OpenHandCursor : Qt::ArrowCursor);
            return;
        }
        if (draggingTrash_) {
            setTrashCenter(pos + dragOffset_);
            update();
            return;
        }
        if (dragId_ == 0) return;
        dragTarget_ = pos + dragOffset_;
        dragTargetValid_ = true;
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        if (draggingTrash_) {
            draggingTrash_ = false;
            saveTrashPos();
            hoverTrash_ = trashNearRect().contains(event->position());
            setCursor(hoverTrash_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
            update();
            return;
        }
        if (dragId_ == 0) return;
        for (LeafBody& L : leaves_) {
            if (L.id != dragId_) continue;
            if (trashHitRect().contains(L.pos)) {
                L.state = LeafState::Removing;
                L.vel = {};
                L.angVel = 0.0;
                L.removeT = 0.0;
            } else {
                L.state = LeafState::Idle;
                L.vel = dragVelocity_ * 0.35;
                clampSpeed(L);
            }
            break;
        }
        dragId_ = 0;
        dragTargetValid_ = false;
        dragVelocity_ = {};
        hoverTrash_ = trashNearRect().contains(event->position());
        emitStats();
        update();
    }

    void leaveEvent(QEvent* event) override {
        QWidget::leaveEvent(event);
        if (draggingTrash_) return;
        if (hoverTrash_) {
            hoverTrash_ = false;
            update();
        }
    }

private:
    static void drawLeaf(QPainter& p, const LeafBody& L, qreal side, const QPixmap& pm,
                         const QPixmap& haloPm, qreal halo) {
        if (L.alpha <= 0.01) return;
        p.save();
        p.translate(L.pos);
        p.rotate(L.angle * 180.0 / kPi);
        const qreal removeEase = L.removeT * (2.0 - L.removeT);
        const qreal scale = (L.state == LeafState::Removing)
            ? std::max(0.18, 1.0 - removeEase * 0.82)
            : 1.0;
        const qreal drawSide = side * scale;
        const QRectF dst(-drawSide * 0.5, -drawSide * 0.5, drawSide, drawSide);
        if (halo > 0.01) {
            drawCenteredHalo(p, dst, haloPm, halo * kLeafHaloAmount * L.alpha,
                             cmToPxF(kLeafHaloOuterCm) * side / leafVisualPx());
        }
        p.setOpacity(L.alpha);
        p.drawPixmap(dst.toRect(), pm);
        p.restore();
    }

    void drawTrash(QPainter& p, const LeafSharedAssets& assets) const {
        const QRectF trash = trashDrawRect();
        const QRect dst = trash.toRect();
        if (trashHalo_ > 0.01) {
            const qreal expand = cmToPxF(kTrashHaloOuterCm) * trashScale_;
            const qreal amt = trashHalo_ * kTrashHaloAmount;
            if (lidOpen_ < 0.999) {
                drawCenteredHalo(p, trash, assets.trashHaloPm, amt * (1.0 - lidOpen_),
                                 expand);
            }
            if (lidOpen_ > 0.001) {
                drawCenteredHalo(p, trash, assets.trashOpenHaloPm, amt * lidOpen_,
                                 expand);
            }
        }
        if (lidOpen_ < 0.999 && !assets.trashPm.isNull()) {
            p.setOpacity(1.0 - lidOpen_);
            p.drawPixmap(dst, assets.trashPm);
        }
        if (lidOpen_ > 0.001 && !assets.trashOpenPm.isNull()) {
            p.setOpacity(lidOpen_);
            p.drawPixmap(dst, assets.trashOpenPm);
        }
        p.setOpacity(1.0);
    }

    void emitStats() {
        if (statsCb_) statsCb_();
    }

    QRectF worldRect() const {
        const qreal pad = cmToPxF(kWorldPadCm);
        return QRectF(pad, pad, width() - 2.0 * pad, height() - 2.0 * pad);
    }

    QSizeF trashSize() const {
        return QSizeF(cmToPxF(kTrashWCm) * trashScale_,
                      cmToPxF(kTrashHCm) * trashScale_);
    }

    QPointF trashCenter() const {
        const QRectF w = worldRect();
        const QSizeF sz = trashSize();
        if (w.width() <= 1.0 || w.height() <= 1.0) {
            return QPointF(width() * 0.5, height() * 0.75);
        }
        QPointF c = trashPosReady_
            ? trashCenterPx_
            : QPointF(w.center().x(), w.bottom() - sz.height() * 0.5);
        const qreal hx = sz.width() * 0.5;
        const qreal hy = sz.height() * 0.5;
        if (w.width() < sz.width()) c.setX(w.center().x());
        else c.setX(std::clamp(c.x(), w.left() + hx, w.right() - hx));
        if (w.height() < sz.height()) c.setY(w.center().y());
        else c.setY(std::clamp(c.y(), w.top() + hy, w.bottom() - hy));
        return c;
    }

    void setTrashCenter(const QPointF& cIn) {
        const QRectF w = worldRect();
        const QSizeF sz = trashSize();
        if (w.width() <= 1.0 || w.height() <= 1.0) return;
        const qreal hx = sz.width() * 0.5;
        const qreal hy = sz.height() * 0.5;
        QPointF c = cIn;
        if (w.width() < sz.width()) c.setX(w.center().x());
        else c.setX(std::clamp(c.x(), w.left() + hx, w.right() - hx));
        if (w.height() < sz.height()) c.setY(w.center().y());
        else c.setY(std::clamp(c.y(), w.top() + hy, w.bottom() - hy));
        trashCenterPx_ = c;
        trashPosReady_ = true;
    }

    void clampTrashToWorld() {
        ensureTrashPosition();
        setTrashCenter(trashCenterPx_);
    }

    QRectF trashDrawRect() const {
        const QPointF c = trashCenter();
        const QSizeF sz = trashSize();
        return QRectF(c.x() - sz.width() * 0.5, c.y() - sz.height() * 0.5,
                      sz.width(), sz.height());
    }

    QRectF trashGrabRect() const {
        QRectF r = trashDrawRect();
        if (lidOpen_ < 0.45) r.setTop(r.top() + r.height() * 0.28);
        return r;
    }

    QRectF trashHitRect() const {
        const qreal pad = cmToPxF(kTrashHitPadCm);
        return trashGrabRect().adjusted(-pad, -pad, pad, pad);
    }

    QRectF trashNearRect() const {
        const qreal pad = cmToPxF(kTrashNearPadCm);
        return trashDrawRect().adjusted(-pad, -pad, pad, pad);
    }

    bool trashArmed() const {
        if (draggingTrash_ || hoverTrash_) return true;
        if (dragId_ == 0) return false;
        for (const LeafBody& L : leaves_) {
            if (L.id != dragId_) continue;
            return trashNearRect().contains(L.pos);
        }
        return false;
    }

    void stepLid(qreal dt) {
        stepToward(&lidOpen_, trashArmed() ? 1.0 : 0.0, dt, kLidAnimSec);
    }

    void stepHalo(qreal dt) {
        const qreal armed = trashArmed() ? 1.0 : 0.0;
        stepToward(&trashHalo_, armed, dt, kHaloAnimSec);
        stepToward(&leafHalo_, (dragId_ != 0) ? 1.0 : 0.0, dt, kHaloAnimSec);
    }

    void loadTrashPos() {
        const QVariantMap m = configValue(kTrashPosKey).toMap();
        if (m.value(QStringLiteral("mode")).toString() == QStringLiteral("px")) {
            trashCenterPx_ = QPointF(m.value(QStringLiteral("x")).toDouble(),
                                     m.value(QStringLiteral("y")).toDouble());
            trashPosReady_ = true;
            return;
        }
        if (!m.isEmpty()) {
            legacyTrashNorm_ = QPointF(
                std::clamp(m.value(QStringLiteral("x"), 0.5).toDouble(), 0.0, 1.0),
                std::clamp(m.value(QStringLiteral("y"), 1.0).toDouble(), 0.0, 1.0));
            hasLegacyTrashNorm_ = true;
        }
    }

    void ensureTrashPosition() {
        if (trashPosReady_) return;
        const QRectF w = worldRect();
        const QSizeF sz = trashSize();
        if (w.width() <= 1.0 || w.height() <= 1.0) return;
        const bool migrateLegacy = hasLegacyTrashNorm_;
        if (hasLegacyTrashNorm_) {
            trashCenterPx_ = QPointF(w.left() + legacyTrashNorm_.x() * w.width(),
                                     w.top() + legacyTrashNorm_.y() * w.height());
        } else {
            trashCenterPx_ = QPointF(w.center().x(), w.bottom() - sz.height() * 0.5);
        }
        trashPosReady_ = true;
        setTrashCenter(trashCenterPx_);
        hasLegacyTrashNorm_ = false;
        if (migrateLegacy) saveTrashPos();
    }

    void saveTrashPos() const {
        writeConfigValue(kTrashPosKey, QVariantMap{
            {QStringLiteral("mode"), QStringLiteral("px")},
            {QStringLiteral("x"), trashCenterPx_.x()},
            {QStringLiteral("y"), trashCenterPx_.y()},
        });
    }

    qreal worldAngle(const LeafBody& L) const { return kArtAngleRad + L.angle; }

    void softSegmentEnds(const LeafBody& L, QPointF* a, QPointF* b) const {
        const qreal ang = worldAngle(L);
        const QPointF axis(std::cos(ang), std::sin(ang));
        *a = L.pos - axis * softHalf(leafScale_);
        *b = L.pos + axis * softHalf(leafScale_);
    }

    struct Octagon {
        QPointF v[8];
    };

    Octagon rigidOctagon(const LeafBody& L) const {
        const qreal hx = rigidHalfLength(leafScale_);
        const qreal hy = rigidHalfWidth(leafScale_);
        const qreal bevel = std::min(rigidBevel(leafScale_), hy * 0.85);
        const QPointF local[8] = {
            {-hx + bevel, -hy}, {hx - bevel, -hy},
            {hx, -hy + bevel},  {hx, hy - bevel},
            {hx - bevel, hy},   {-hx + bevel, hy},
            {-hx, hy - bevel},  {-hx, -hy + bevel},
        };
        const qreal ang = worldAngle(L);
        const qreal c = std::cos(ang);
        const qreal s = std::sin(ang);
        Octagon out;
        for (int i = 0; i < 8; ++i) {
            out.v[i] = L.pos + QPointF(local[i].x() * c - local[i].y() * s,
                                      local[i].x() * s + local[i].y() * c);
        }
        return out;
    }

    void rigidExtents(const LeafBody& L, qreal* extX, qreal* extY) const {
        const Octagon poly = rigidOctagon(L);
        *extX = 0.0;
        *extY = 0.0;
        for (const QPointF& p : poly.v) {
            *extX = std::max(*extX, std::abs(p.x() - L.pos.x()));
            *extY = std::max(*extY, std::abs(p.y() - L.pos.y()));
        }
    }

    bool polygonContact(const LeafBody& A, const LeafBody& B, qreal padding,
                        QPointF* normal, qreal* overlap) const {
        const QPointF centerDelta = B.pos - A.pos;
        const qreal maxDistance = rigidRadius(leafScale_) * 2.0 + padding;
        if (QPointF::dotProduct(centerDelta, centerDelta) >= maxDistance * maxDistance) {
            return false;
        }

        const Octagon a = rigidOctagon(A);
        const Octagon b = rigidOctagon(B);
        qreal best = 1.0e30;
        QPointF bestAxis;
        auto testAxes = [&](const Octagon& source) {
            // 对边平行，每个八边形只需测试前四条边的法线。
            for (int edge = 0; edge < 4; ++edge) {
                const QPointF d = source.v[(edge + 1) % 8] - source.v[edge];
                const qreal len = std::hypot(d.x(), d.y());
                if (len <= 1e-6) continue;
                const QPointF axis(-d.y() / len, d.x() / len);
                qreal minA = QPointF::dotProduct(a.v[0], axis);
                qreal maxA = minA;
                qreal minB = QPointF::dotProduct(b.v[0], axis);
                qreal maxB = minB;
                for (int i = 1; i < 8; ++i) {
                    const qreal pa = QPointF::dotProduct(a.v[i], axis);
                    const qreal pb = QPointF::dotProduct(b.v[i], axis);
                    minA = std::min(minA, pa);
                    maxA = std::max(maxA, pa);
                    minB = std::min(minB, pb);
                    maxB = std::max(maxB, pb);
                }
                const qreal axisOverlap =
                    std::min(maxA, maxB) - std::max(minA, minB) + padding;
                if (axisOverlap <= 0.0) return false;
                if (axisOverlap < best) {
                    best = axisOverlap;
                    bestAxis = axis;
                }
            }
            return true;
        };
        if (!testAxes(a) || !testAxes(b)) return false;
        if (QPointF::dotProduct(centerDelta, bestAxis) < 0.0) bestAxis = -bestAxis;
        if (normal) *normal = bestAxis;
        if (overlap) *overlap = best;
        return true;
    }

    bool hitSoft(const LeafBody& L, const QPointF& pos) const {
        QPointF a, b;
        softSegmentEnds(L, &a, &b);
        return QLineF(pos, closestPointOnSegment(a, b, pos)).length()
            <= softR(leafScale_);
    }

    void updateWorld() {
        theoreticalCapacity_ = maxLeavesForSize(size(), leafScale_);
        const int lower = capacityLower(theoreticalCapacity_);
        const int upper = std::max(lower, capacityUpper(theoreticalCapacity_));
        maxLeaves_ = std::clamp(preferredMaxLeaves_, lower, upper);
        writeConfigValue(kViewportSizeKey, QVariantMap{
            {QStringLiteral("w"), width()},
            {QStringLiteral("h"), height()},
        });
    }

    void clampToWorld(LeafBody& L) const {
        const QRectF w = worldRect();
        qreal ex = 0.0, ey = 0.0;
        rigidExtents(L, &ex, &ey);
        L.pos.setX(std::clamp(L.pos.x(), w.left() + ex, w.right() - ex));
        L.pos.setY(std::clamp(L.pos.y(), w.top() + ey, w.bottom() - ey));
    }

    void flushSpawns() {
        if (resizePaused_) return;
        bool spawned = false;
        while (pendingSpawns_ > 0 && aliveCount() < maxLeaves_) {
            if (!trySpawnOne()) break;
            --pendingSpawns_;
            spawned = true;
        }
        if (spawned) {
            emitStats();
            update();
        }
    }

    bool rigidOverlap(const LeafBody& A, const LeafBody& B) const {
        // 刷新时留 1px 余量，第一帧也不会生成成相交状态。
        return polygonContact(A, B, 1.0, nullptr, nullptr);
    }

    bool trySpawnOne() {
        const QRectF w = worldRect();
        const qreal margin = rigidRadius(leafScale_);
        if (w.width() < margin * 4.0 || w.height() < margin * 4.0) return false;
        const qreal spawnHeight = std::min(w.height() * 0.28, w.height() - 2.0 * margin);
        if (spawnHeight <= 0.0) return false;
        for (int attempt = 0; attempt < 36; ++attempt) {
            LeafBody cand;
            cand.pos = QPointF(
                w.left() + margin + QRandomGenerator::global()->generateDouble()
                                        * std::max(1.0, w.width() - 2.0 * margin),
                w.top() + margin + QRandomGenerator::global()->generateDouble()
                                       * spawnHeight);
            cand.angle = (QRandomGenerator::global()->generateDouble() - 0.5) * 1.6;
            bool overlaps = false;
            for (const LeafBody& o : leaves_) {
                if (o.state == LeafState::Removing) continue;
                if (rigidOverlap(cand, o)) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps) continue;
            cand.vel = QPointF((QRandomGenerator::global()->generateDouble() - 0.5) * 40.0, 20.0);
            cand.angVel = (QRandomGenerator::global()->generateDouble() - 0.5) * 2.4;
            cand.id = ++nextId_;
            leaves_.append(cand);
            return true;
        }
        // 顶部刷新区暂时没有空位：保留 pending，等叶子落下后再重试。
        return false;
    }

    void cullOverflowImmediate() {
        while (aliveCount() > maxLeaves_) {
            int victim = -1;
            quint64 oldest = ~quint64(0);
            for (int i = 0; i < leaves_.size(); ++i) {
                if (leaves_[i].state == LeafState::Removing) continue;
                if (leaves_[i].state == LeafState::Dragging) continue;
                if (leaves_[i].id < oldest) {
                    oldest = leaves_[i].id;
                    victim = i;
                }
            }
            if (victim < 0) break;
            leaves_.removeAt(victim);
        }
    }

    static void clampSpeed(LeafBody& L) {
        const qreal sp = std::hypot(L.vel.x(), L.vel.y());
        if (sp > kMaxSpeed) L.vel *= (kMaxSpeed / sp);
    }

    bool contactData(const LeafBody& A, const LeafBody& B, QPointF* normal,
                     qreal* overlap) const {
        return polygonContact(A, B, 0.0, normal, overlap);
    }

    void solvePositions(int iters, qreal maxCorrection, bool draggedContactsOnly = false) {
        for (int it = 0; it < iters; ++it) {
            for (int i = 0; i < leaves_.size(); ++i) {
                LeafBody& A = leaves_[i];
                if (A.state == LeafState::Removing) continue;
                for (int j = i + 1; j < leaves_.size(); ++j) {
                    LeafBody& B = leaves_[j];
                    if (B.state == LeafState::Removing) continue;
                    const bool aDrag = A.state == LeafState::Dragging;
                    const bool bDrag = B.state == LeafState::Dragging;
                    if (draggedContactsOnly && !aDrag && !bDrag) continue;
                    QPointF n;
                    qreal overlap = 0.0;
                    if (!contactData(A, B, &n, &overlap)) continue;
                    const qreal corr = std::min(
                        std::max(0.0, overlap - kPositionSlopPx), maxCorrection);
                    if (corr <= 0.0 || (aDrag && bDrag)) continue;
                    if (aDrag) {
                        B.pos += n * (corr * kDragPushRatio);
                    } else if (bDrag) {
                        A.pos -= n * (corr * kDragPushRatio);
                    } else {
                        A.pos -= n * (corr * 0.5);
                        B.pos += n * (corr * 0.5);
                        if (it == 0) {
                            const qreal turn = corr * kCollisionTorque;
                            A.angVel -= turn;
                            B.angVel += turn;
                        }
                    }
                }
            }
            for (LeafBody& L : leaves_) {
                if (L.state != LeafState::Removing) clampToWorld(L);
            }
        }
    }

    void solveVelocities() {
        for (int i = 0; i < leaves_.size(); ++i) {
            LeafBody& A = leaves_[i];
            if (A.state == LeafState::Removing) continue;
            for (int j = i + 1; j < leaves_.size(); ++j) {
                LeafBody& B = leaves_[j];
                if (B.state == LeafState::Removing) continue;
                QPointF n;
                qreal overlap = 0.0;
                if (!contactData(A, B, &n, &overlap)) continue;
                const bool aDrag = A.state == LeafState::Dragging;
                const bool bDrag = B.state == LeafState::Dragging;
                if (aDrag && bDrag) continue;
                const QPointF aVel = aDrag ? dragVelocity_ : A.vel;
                const QPointF bVel = bDrag ? dragVelocity_ : B.vel;
                const QPointF rel = bVel - aVel;
                const qreal vn = QPointF::dotProduct(rel, n);
                const QPointF vt = rel - n * vn;
                if (!aDrag && !bDrag) {
                    if (vn < 0.0) {
                        // 轻微耗散法向速度，避免密集堆叠把冲量来回传递成颤动。
                        const qreal impulse = -vn * (1.0 + kRestitution) * 0.42;
                        A.vel -= n * impulse;
                        B.vel += n * impulse;
                    }
                    A.vel += vt * (kFriction * 0.5);
                    B.vel -= vt * (kFriction * 0.5);
                } else if (aDrag) {
                    if (vn < 0.0) B.vel += n * (-vn * 0.50);
                    B.vel -= vt * kFriction;
                } else {
                    if (vn < 0.0) A.vel -= n * (-vn * 0.50);
                    A.vel += vt * kFriction;
                }
                if (!aDrag) clampSpeed(A);
                if (!bDrag) clampSpeed(B);
            }
        }
    }

    void solveBoundaryVelocities() {
        const QRectF w = worldRect();
        for (LeafBody& L : leaves_) {
            if (L.state == LeafState::Removing || L.state == LeafState::Dragging) continue;
            qreal ex = 0.0, ey = 0.0;
            rigidExtents(L, &ex, &ey);
            if (L.pos.x() <= w.left() + ex + 0.5 && L.vel.x() < 0.0) {
                L.vel.setX(-L.vel.x() * kRestitution);
                L.vel.setY(L.vel.y() * kFloorFriction);
            }
            if (L.pos.x() >= w.right() - ex - 0.5 && L.vel.x() > 0.0) {
                L.vel.setX(-L.vel.x() * kRestitution);
                L.vel.setY(L.vel.y() * kFloorFriction);
            }
            if (L.pos.y() >= w.bottom() - ey - 0.5 && L.vel.y() > 0.0) {
                L.vel.setY(-L.vel.y() * kRestitution);
                L.vel.setX(L.vel.x() * kFloorFriction);
                if (std::abs(L.vel.y()) < 18.0) L.vel.setY(0.0);
                if (std::abs(L.vel.x()) < 6.0) L.vel.setX(0.0);
            }
            clampSpeed(L);
        }
    }

    qreal maxOverlapWith(const LeafBody& dragged) const {
        qreal worst = 0.0;
        for (const LeafBody& other : leaves_) {
            if (&other == &dragged || other.state == LeafState::Removing) continue;
            QPointF n;
            qreal overlap = 0.0;
            if (contactData(dragged, other, &n, &overlap)) worst = std::max(worst, overlap);
        }
        return worst;
    }

    void advanceDragged(qreal dt) {
        if (!dragTargetValid_ || dragId_ == 0) return;
        for (LeafBody& L : leaves_) {
            if (L.id != dragId_) continue;
            const QPointF start = L.pos;
            QPointF delta = dragTarget_ - L.pos;
            const qreal distance = std::hypot(delta.x(), delta.y());
            const qreal maxStep =
                std::max(1.0, rigidHalfWidth(leafScale_) * kDragStepFrac);
            const int steps = std::clamp(
                static_cast<int>(std::ceil(distance / maxStep)), 1, kMaxDragSubsteps);
            if (distance > maxStep * steps) delta *= (maxStep * steps / distance);
            const QPointF step = delta / steps;
            for (int s = 0; s < steps; ++s) {
                const QPointF before = L.pos;
                L.pos += step;
                clampToWorld(L);
                solvePositions(kDragPositionIters,
                               rigidHalfWidth(leafScale_) * kPositionMaxCorrectionFrac, true);
                if (maxOverlapWith(L) > rigidHalfWidth(leafScale_) * 0.55) {
                    L.pos = before;
                    break;
                }
            }
            const QPointF actualVel = (L.pos - start) / std::max(0.001, dt);
            dragVelocity_ = dragVelocity_ * 0.65 + actualVel * 0.35;
            L.vel = dragVelocity_;
            return;
        }
    }

    void squeezeIntoBounds() {
        for (LeafBody& L : leaves_) {
            if (L.state == LeafState::Removing) continue;
            clampToWorld(L);
        }
        solvePositions(kResizeSettleIters,
                       rigidHalfWidth(leafScale_) * kPositionMaxCorrectionFrac);
        solveVelocities();
        solveBoundaryVelocities();
    }

    void onTick() {
        if (resizePaused_) return;
        const qreal dt = kTickMs / 1000.0;
        bool dirty = pendingSpawns_ > 0;
        bool statsDirty = false;
        flushSpawns();
        advanceDragged(dt);

        for (LeafBody& L : leaves_) {
            if (L.state == LeafState::Removing) {
                L.removeT = std::min(1.0, L.removeT + dt / 0.34);
                const QRectF trash = trashDrawRect();
                const QPointF mouth(trash.center().x(), trash.top() + trash.height() * 0.18);
                const qreal follow = 1.0 - std::exp(-12.0 * dt);
                L.pos += (mouth - L.pos) * follow;
                L.angle += ((L.id & 1) ? 5.2 : -5.2) * dt;
                L.alpha = 1.0 - L.removeT * L.removeT;
                dirty = true;
                continue;
            }
            if (L.state == LeafState::Dragging) {
                dirty = true;
                continue;
            }
            L.vel.setY(L.vel.y() + kGravity * dt);
            L.pos += L.vel * dt;
            L.angle += L.angVel * dt;
            L.vel *= kDamping;
            L.angVel *= kAngDamping;
            L.angVel = std::clamp(L.angVel, -kMaxAngVel, kMaxAngVel);
            clampSpeed(L);
            if (std::hypot(L.vel.x(), L.vel.y()) < 20.0) {
                L.vel *= 0.90;
                L.angVel *= 0.88;
                if (std::hypot(L.vel.x(), L.vel.y()) < 6.0) L.vel = {};
                if (std::abs(L.angVel) < 0.02) L.angVel = 0.0;
            }
            dirty = true;
        }

        solvePositions(kPositionIters,
                       rigidHalfWidth(leafScale_) * kPositionMaxCorrectionFrac);
        solveVelocities();
        solveBoundaryVelocities();

        const qreal prevLid = lidOpen_;
        const qreal prevTrashHalo = trashHalo_;
        const qreal prevLeafHalo = leafHalo_;
        stepLid(dt);
        stepHalo(dt);
        if (std::abs(lidOpen_ - prevLid) > 0.002
            || std::abs(trashHalo_ - prevTrashHalo) > 0.002
            || std::abs(leafHalo_ - prevLeafHalo) > 0.002) {
            dirty = true;
        }

        for (int i = leaves_.size() - 1; i >= 0; --i) {
            if (leaves_[i].state == LeafState::Removing && leaves_[i].removeT >= 1.0) {
                leaves_.removeAt(i);
                dirty = true;
                statsDirty = true;
            }
        }

        if (dirty) {
            update();
        }
        if (statsDirty) emitStats();
    }

    QTimer* tick_ = nullptr;
    QVector<LeafBody> leaves_;
    int pendingSpawns_ = 0;
    int maxLeaves_ = kMinLeaves;
    int theoreticalCapacity_ = 0;
    int preferredMaxLeaves_ = 0;
    qreal leafScale_ = 1.0;
    qreal trashScale_ = 1.0;
    quint64 nextId_ = 1;
    quint64 dragId_ = 0;
    bool draggingTrash_ = false;
    bool hoverTrash_ = false;
    qreal lidOpen_ = 0.0;
    qreal trashHalo_ = 0.0;
    qreal leafHalo_ = 0.0;
    QPointF dragOffset_;
    QPointF dragTarget_;
    QPointF dragVelocity_;
    bool dragTargetValid_ = false;
    QPointF trashCenterPx_;
    QPointF legacyTrashNorm_{0.5, 1.0};
    bool hasLegacyTrashNorm_ = false;
    bool trashPosReady_ = false;
    bool resizePaused_ = false;
    bool suppressResizeCommit_ = false;
    std::function<void()> statsCb_;
};

class LeafRoot final : public RippleOverlayRoot {
public:
    explicit LeafRoot(QMainWindow* win) : RippleOverlayRoot(win) {
        content()->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        canvas_ = new LeafCanvas(content());
        canvas_->commitViewport(content()->rect());
    }

    LeafCanvas* canvas() const { return canvas_; }

protected:
    bool wantsSyncResizeLayout() const override { return false; }

    bool contentWantsMouse(const QPoint& contentLocal) const override {
        if (!canvas_ || resizing() || canvas_->resizePaused()) return false;
        return canvas_->hitInteractive(canvas_->mapFrom(content(), contentLocal));
    }

    void onResizeBegin() override {
        if (canvas_) canvas_->beginResizePause();
    }

    void onContentGeometryChanged() override {
        if (canvas_ && !resizing()) canvas_->commitViewport(content()->rect());
    }

    void onContentGeometryWhileResizing() override {}

    void onResizeResume() override {
        if (canvas_) canvas_->endResizePause();
    }

private:
    LeafCanvas* canvas_ = nullptr;
};

class LeafOverlayController final : public QObject {
public:
    explicit LeafOverlayController(QObject* parent = nullptr) : QObject(parent) {}

    bool isMounted() const {
        return OverlayHostService::instance().isToolActive(OverlayToolId::Leaf);
    }

    LeafCanvas* canvas() const { return root_ ? root_->canvas() : nullptr; }

    void show(std::function<void()> onClosed) {
        auto& host = OverlayHostService::instance();
        root_ = nullptr;
        auto* shell = host.shell(OverlayToolId::Leaf);
        root_ = new LeafRoot(shell);
        const int def = cmToPx(12.0);
        host.show(OverlayToolId::Leaf, QStringLiteral("捡叶子"), kGeoKey,
                  cmToPx(6.0), cmToPx(7.0), def, static_cast<int>(def * 1.25), root_,
                  [this, onClosed]() {
                      root_ = nullptr;
                      if (onClosed) onClosed();
                  });
    }

private:
    LeafRoot* root_ = nullptr;
};

class LeafToolRuntime final : public ToolRuntimeBase {
public:
    explicit LeafToolRuntime(QObject* parent, std::function<void()> tryRelease)
        : ToolRuntimeBase(parent), tryRelease_(std::move(tryRelease)) {}

    QString toolId() const override { return QStringLiteral("leaf"); }

    bool isOverlayActive() const override {
        return overlayCtrl_ && overlayCtrl_->isMounted();
    }

    LeafCanvas* canvas() const {
        return overlayCtrl_ ? overlayCtrl_->canvas() : nullptr;
    }

    LeafOverlayController* controller() {
        if (!overlayCtrl_) overlayCtrl_ = new LeafOverlayController(this);
        return overlayCtrl_;
    }

    void toggleOverlay(std::function<void()> onClosed) {
        auto& host = OverlayHostService::instance();
        if (host.isToolActive(OverlayToolId::Leaf)) {
            host.teardown(OverlayToolId::Leaf);
            return;
        }
        controller()->show([this, onClosed]() {
            if (onClosed) onClosed();
            if (tryRelease_) tryRelease_();
        });
    }

    void spawnTest(int count = 1) {
        if (!isOverlayActive()) toggleOverlay(nullptr);
        if (auto* c = controller()->canvas()) c->enqueueSpawn(count);
    }

    void applySettings(int maxLeaves, int leafPercent, int trashPercent) {
        if (auto* c = canvas()) c->applySettings(maxLeaves, leafPercent, trashPercent);
    }

private:
    LeafOverlayController* overlayCtrl_ = nullptr;
    std::function<void()> tryRelease_;
};

class LeafToolWindow final : public ToolWindowBase {
public:
    static constexpr int kWidth = 500;
    static constexpr int kHeight = 720;

    explicit LeafToolWindow(CoreClient* core, LeafToolRuntime* runtime)
        : ToolWindowBase(core), runtime_(runtime) {
        setWindowTitle(QStringLiteral("捡叶子"));
        setFixedSize(kWidth, kHeight);
        build();
        refreshTheme();
        liveaio::util::onThemeChange(this, [this](const QString&) { refreshTheme(); });
        statsTimer_ = new QTimer(this);
        statsTimer_->setInterval(200);
        QObject::connect(statsTimer_, &QTimer::timeout, this, [this]() { refreshStats(); });
        statsTimer_->start();
    }

    QString toolId() const override { return QStringLiteral("leaf"); }
    void onCorePacket(const QJsonObject&) override {}

    void refreshTheme() override {
        applyChromeStyle();
        refreshOpenBtn();
        styleSpawnBtn();
        if (simWidget_) simWidget_->refreshTheme();
        if (hint_) {
            hint_->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                     .arg(theme().textMuted));
        }
        if (stats_) {
            stats_->setStyleSheet(QStringLiteral("color: %1; background: transparent; font-size: 13px;")
                                      .arg(theme().text));
        }
        if (simDesc_) {
            simDesc_->setStyleSheet(QStringLiteral("color: %1; background: transparent; font-size: 12px;")
                                        .arg(theme().textMuted));
        }
    }

    void applyChromeStyle() override { setStyleSheet(liveaio::util::toolQss()); }

private:
    void build() {
        auto* root = new QWidget(this);
        root->setObjectName(QStringLiteral("ToolRoot"));
        setCentralWidget(root);
        auto* lay = new QVBoxLayout(root);
        lay->setContentsMargins(28, 24, 28, 24);
        lay->setSpacing(16);

        auto* title = new QLabel(QStringLiteral("捡叶子"), root);
        title->setObjectName(QStringLiteral("ToolPageTitle"));
        lay->addWidget(title);

        hint_ = new QLabel(
            QStringLiteral("打开悬浮窗后可用「模拟送礼」或「投放测试」生成叶子，拖到垃圾桶消除。"),
            root);
        hint_->setWordWrap(true);
        hint_->setObjectName(QStringLiteral("ToolTip"));
        lay->addWidget(hint_);

        auto* card = new QFrame(root);
        card->setObjectName(QStringLiteral("Card"));
        auto* cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(20, 16, 20, 16);
        cardLay->setSpacing(12);

        stats_ = new QLabel(QStringLiteral("场上 0 / 上限 — · 队列 0"), card);
        cardLay->addWidget(stats_);

        auto* row = new QHBoxLayout;
        row->setSpacing(10);
        spawnBtn_ = new QPushButton(QStringLiteral("投放测试"), card);
        spawnBtn_->setCursor(Qt::PointingHandCursor);
        spawnBtn_->setFixedHeight(34);
        QObject::connect(spawnBtn_, &QPushButton::clicked, this, [this]() {
            if (!runtime_) return;
            runtime_->spawnTest(1);
            refreshOpenBtn();
            refreshStats();
        });
        openBtn_ = new QPushButton(QStringLiteral("打开悬浮窗"), card);
        openBtn_->setCursor(Qt::PointingHandCursor);
        openBtn_->setFixedHeight(34);
        QObject::connect(openBtn_, &QPushButton::clicked, this, [this]() { toggleOverlay(); });
        row->addWidget(spawnBtn_);
        row->addStretch();
        row->addWidget(openBtn_);
        cardLay->addLayout(row);
        lay->addWidget(card);

        auto* settingsCard = new QFrame(root);
        settingsCard->setObjectName(QStringLiteral("Card"));
        auto* settingsLay = new QVBoxLayout(settingsCard);
        settingsLay->setContentsMargins(20, 16, 20, 16);
        settingsLay->setSpacing(10);
        auto* settingsTitle = new QLabel(QStringLiteral("容量与尺寸"), settingsCard);
        settingsTitle->setObjectName(QStringLiteral("CardTitle"));
        settingsLay->addWidget(settingsTitle);

        preview_ = new LeafScalePreview(settingsCard);
        settingsLay->addWidget(preview_);

        auto* settingsGrid = new QGridLayout;
        settingsGrid->setHorizontalSpacing(12);
        settingsGrid->setVerticalSpacing(8);
        maxLeavesSpin_ = new QSpinBox(settingsCard);
        leafScaleSpin_ = new QSpinBox(settingsCard);
        trashScaleSpin_ = new QSpinBox(settingsCard);
        leafScaleSpin_->setRange(50, 200);
        trashScaleSpin_->setRange(50, 200);
        leafScaleSpin_->setSingleStep(5);
        trashScaleSpin_->setSingleStep(5);
        leafScaleSpin_->setSuffix(QStringLiteral("%"));
        trashScaleSpin_->setSuffix(QStringLiteral("%"));
        leafScaleSpin_->setValue(scalePercent(kLeafScaleKey));
        trashScaleSpin_->setValue(scalePercent(kTrashScaleKey));
        const int initialTheory = maxLeavesForSize(
            savedViewportSize(), leafScaleSpin_->value() / 100.0);
        maxLeavesSpin_->setRange(capacityLower(initialTheory),
                                 std::max(capacityLower(initialTheory),
                                          capacityUpper(initialTheory)));
        maxLeavesSpin_->setValue(configuredMaxLeaves(initialTheory));
        preview_->setScales(leafScaleSpin_->value(), trashScaleSpin_->value());
        settingsGrid->addWidget(new QLabel(QStringLiteral("最多同时出现"), settingsCard), 0, 0);
        settingsGrid->addWidget(maxLeavesSpin_, 0, 1);
        settingsGrid->addWidget(new QLabel(QStringLiteral("叶子尺寸"), settingsCard), 1, 0);
        settingsGrid->addWidget(leafScaleSpin_, 1, 1);
        settingsGrid->addWidget(new QLabel(QStringLiteral("垃圾桶尺寸"), settingsCard), 2, 0);
        settingsGrid->addWidget(trashScaleSpin_, 2, 1);
        settingsGrid->setColumnStretch(0, 1);
        settingsLay->addLayout(settingsGrid);
        lay->addWidget(settingsCard);

        QObject::connect(maxLeavesSpin_, &QSpinBox::valueChanged,
                         this, [this](int) { applySettingsFromControls(); });
        QObject::connect(leafScaleSpin_, &QSpinBox::valueChanged,
                         this, [this](int) { applySettingsFromControls(); });
        QObject::connect(trashScaleSpin_, &QSpinBox::valueChanged,
                         this, [this](int) { applySettingsFromControls(); });

        auto* simCard = new QFrame(root);
        simCard->setObjectName(QStringLiteral("Card"));
        auto* simLay = new QVBoxLayout(simCard);
        simLay->setContentsMargins(20, 16, 20, 16);
        simLay->setSpacing(10);
        auto* simTitle = new QLabel(QStringLiteral("模拟送礼"), simCard);
        simTitle->setObjectName(QStringLiteral("CardTitle"));
        simLay->addWidget(simTitle);
        simWidget_ = new ot::SimGiftWidget(simCard);
        simWidget_->setClosedTip(QStringLiteral("请先打开捡叶子悬浮窗"));
        simWidget_->setOnPush([this](const QString& gift, int count) { pushSimGift(gift, count); });
        simLay->addWidget(simWidget_);
        simDesc_ = new QLabel(
            QStringLiteral("向已打开的捡叶子悬浮窗推送模拟礼物，数量对应投放叶子片数"),
            simCard);
        simDesc_->setWordWrap(true);
        simDesc_->setObjectName(QStringLiteral("ToolTip"));
        simLay->addWidget(simDesc_);
        lay->addWidget(simCard);

        lay->addStretch();
        refreshOpenBtn();
        refreshStats();
    }

    void pushSimGift(const QString& gift, int count) {
        if (!runtime_ || !runtime_->isOverlayActive()) return;
        const int n = std::max(1, count);
        runtime_->spawnTest(n);
        sendCore(QJsonObject{
            {QStringLiteral("op"), QStringLiteral("tool.leaf.sim_gift")},
            {QStringLiteral("gift"), gift},
            {QStringLiteral("count"), n},
            {QStringLiteral("user"), QStringLiteral("LiveAIO")},
        });
        refreshStats();
    }

    void toggleOverlay() {
        if (!runtime_) return;
        QPointer<LeafToolWindow> guard(this);
        runtime_->toggleOverlay([guard]() {
            if (!guard) return;
            guard->refreshOpenBtn();
            guard->refreshStats();
        });
        refreshOpenBtn();
        refreshStats();
    }

    void refreshOpenBtn() {
        const bool open = runtime_ && runtime_->isOverlayActive();
        openBtn_->setText(open ? QStringLiteral("关闭悬浮窗") : QStringLiteral("打开悬浮窗"));
        if (simWidget_) simWidget_->setPushEnabled(open);
        const auto& C = theme();
        if (open) {
            openBtn_->setStyleSheet(QStringLiteral(
                "QPushButton { background: %1; color: #fff; border: 1.5px solid transparent;"
                " border-radius: 8px; font-size: 13px; font-weight: 600; padding: 0 16px; }"
                "QPushButton:hover { background: %2; }"
            ).arg(C.closeHover, C.active));
        } else {
            openBtn_->setStyleSheet(QStringLiteral(
                "QPushButton { background: %1; color: %2; border: 1.5px solid %2;"
                " border-radius: 8px; font-size: 13px; font-weight: 600; padding: 0 16px; }"
                "QPushButton:hover { background: %3; }"
            ).arg(C.card, C.activeLine, C.hover));
        }
    }

    void styleSpawnBtn() {
        const auto& C = theme();
        spawnBtn_->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 1.5px solid %3;"
            " border-radius: 8px; font-size: 13px; font-weight: 600; padding: 0 14px; }"
            "QPushButton:hover { background: %4; color: %5; }"
        ).arg(C.card, C.textMuted, C.border, C.hover, C.text));
    }

    QSize capacityReferenceSize() const {
        if (runtime_) {
            if (auto* c = runtime_->canvas(); c && c->size().isValid()) return c->size();
        }
        return savedViewportSize();
    }

    int syncCapacityRange() {
        if (!maxLeavesSpin_ || !leafScaleSpin_) return 0;
        const int theoretical = maxLeavesForSize(
            capacityReferenceSize(), leafScaleSpin_->value() / 100.0);
        const int lower = capacityLower(theoretical);
        const int upper = std::max(lower, capacityUpper(theoretical));
        const int value = std::clamp(maxLeavesSpin_->value(), lower, upper);
        QSignalBlocker blocker(maxLeavesSpin_);
        maxLeavesSpin_->setRange(lower, upper);
        maxLeavesSpin_->setValue(value);
        return value;
    }

    void applySettingsFromControls() {
        if (!maxLeavesSpin_ || !leafScaleSpin_ || !trashScaleSpin_) return;
        const int maxLeaves = syncCapacityRange();
        const int leafPercent = leafScaleSpin_->value();
        const int trashPercent = trashScaleSpin_->value();
        writeConfigValue(kMaxLeavesKey, maxLeaves);
        writeConfigValue(kLeafScaleKey, leafPercent);
        writeConfigValue(kTrashScaleKey, trashPercent);
        if (preview_) preview_->setScales(leafPercent, trashPercent);
        if (runtime_) runtime_->applySettings(maxLeaves, leafPercent, trashPercent);
        refreshStats();
    }

    void refreshStats() {
        if (maxLeavesSpin_ && leafScaleSpin_) {
            const int before = maxLeavesSpin_->value();
            const int after = syncCapacityRange();
            if (after != before) {
                writeConfigValue(kMaxLeavesKey, after);
                if (runtime_) {
                    runtime_->applySettings(after, leafScaleSpin_->value(),
                                            trashScaleSpin_->value());
                }
            }
        }
        int alive = 0;
        int maxN = kMinLeaves;
        int pending = 0;
        if (runtime_ && runtime_->isOverlayActive()) {
            if (auto* c = runtime_->controller()->canvas()) {
                alive = c->aliveCount();
                maxN = c->maxLeaves();
                pending = c->pendingCount();
                QPointer<LeafToolWindow> guard(this);
                c->setStatsCallback([guard]() {
                    if (guard) guard->refreshStats();
                });
            }
        }
        if (stats_) {
            stats_->setText(QStringLiteral("场上 %1 / 上限 %2 · 队列 %3")
                                .arg(alive)
                                .arg(maxN)
                                .arg(pending));
        }
        if (simWidget_) {
            simWidget_->setPushEnabled(runtime_ && runtime_->isOverlayActive());
        }
    }

    LeafToolRuntime* runtime_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* stats_ = nullptr;
    QLabel* simDesc_ = nullptr;
    ot::SimGiftWidget* simWidget_ = nullptr;
    LeafScalePreview* preview_ = nullptr;
    QPushButton* spawnBtn_ = nullptr;
    QPushButton* openBtn_ = nullptr;
    QSpinBox* maxLeavesSpin_ = nullptr;
    QSpinBox* leafScaleSpin_ = nullptr;
    QSpinBox* trashScaleSpin_ = nullptr;
    QTimer* statsTimer_ = nullptr;
};

static ToolRuntimeBase* createLeafRuntime(QObject* parent, std::function<void()> tryRelease) {
    return new LeafToolRuntime(parent, std::move(tryRelease));
}

static ToolWindowBase* createLeafTool(CoreClient* core, LeafToolRuntime* runtime) {
    return new LeafToolWindow(core, runtime);
}

}  // namespace liveaio::tools::leaf
