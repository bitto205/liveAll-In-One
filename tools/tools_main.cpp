#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QAbstractItemView>
#include <QEasingCurve>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHeaderView>
#include <QLinearGradient>
#include <QMessageBox>
#include <QMetaType>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRandomGenerator>
#include <QScreen>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTimer>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

#include "tools_common.cpp"
#include "../resources/cpp/gift.cpp"
#include "../resources/cpp/skin.cpp"

namespace liveaio::tools {
using ToolSkin = liveaio::resources::ToolSkin;
using liveaio::resources::resolveGiftIconPath;
using liveaio::util::OverlayHost;
using liveaio::util::OverlayResizeFreeze;
using liveaio::util::ThemedComboBox;
using liveaio::util::prepareAppAlphaFormat;
}  // namespace liveaio::tools

#include "memo_tool.cpp"
#include "danmu_tool.cpp"
#include "overtime_tool.cpp"
#include "liveaio_tools_api.h"

namespace liveaio::tools {

// 同进程插件：工具窗注册表与 core 连接由这里持有，关闭后自动注销。
class ToolsSession final : public QObject {
public:
    static ToolsSession& instance() {
        static ToolsSession* session = new ToolsSession(qApp);
        return *session;
    }

    bool openTool(const QString& id) {
        if (auto* existing = opened_.value(id)) {
            existing->show();
            existing->raise();
            existing->activateWindow();
            return true;
        }
        ensureCore();
        ToolWindowBase* win = nullptr;
        if (id == QStringLiteral("memo")) win = createMemoTool(core_);
        else if (id == QStringLiteral("danmu")) win = createDanmuTool(core_);
        else if (id == QStringLiteral("overtime")) win = createOvertimeTool(core_);
        if (!win) return false;

        opened_.insert(id, win);
        QObject::connect(win, &QObject::destroyed, this, [this, id]() { opened_.remove(id); });
        win->setAttribute(Qt::WA_DeleteOnClose, true);
        win->show();
        win->raise();
        win->activateWindow();
        return true;
    }

private:
    explicit ToolsSession(QObject* parent) : QObject(parent) {}

    void ensureCore() {
        if (core_) return;
        if (g_appRoot.isEmpty()) {
            g_appRoot = qEnvironmentVariable("LIVEAIO_ROOT");
            if (g_appRoot.isEmpty()) g_appRoot = QDir::currentPath();
        }
        prepareAppAlphaFormat();
        core_ = new CoreClient(this);
        installConfigBridge();
        core_->setPacketCallback([this](const QJsonObject& packet) { dispatchPacket(packet); });
        core_->setStatusCallback([this](bool connected) { dispatchStatus(connected); });
        core_->connectToCore();
    }

    void dispatchPacket(const QJsonObject& packet) {
        const QString op = packet.value(QStringLiteral("op")).toString();
        if (op != QStringLiteral("memo.item") && op != QStringLiteral("danmu.show")
            && op != QStringLiteral("tick") && op != QStringLiteral("ledger")) {
            return;
        }
        for (auto* tool : opened_) {
            if (tool) tool->onCorePacket(packet);
        }
    }

    void dispatchStatus(bool connected) {
        for (auto* tool : opened_) {
            if (tool) tool->onCoreStatus(connected);
        }
    }

    CoreClient* core_ = nullptr;
    QMap<QString, ToolWindowBase*> opened_;
};

}  // namespace liveaio::tools

extern "C" LIVEAIO_TOOLS_API int LiveAIO_ToolsOpen(const char* tool_id) {
    if (!QApplication::instance()) return 1;
    const QString id = QString::fromUtf8(tool_id ? tool_id : "").trimmed();
    if (id.isEmpty()) return 2;
    return liveaio::tools::ToolsSession::instance().openTool(id) ? 0 : 3;
}

extern "C" LIVEAIO_TOOLS_API void LiveAIO_ToolsApplyTheme(const char* theme_name) {
    if (!QApplication::instance()) return;
    const QString name = QString::fromUtf8(theme_name ? theme_name : "").trimmed();
    if (name.isEmpty()) return;
    liveaio::util::applyThemeName(name);
}
