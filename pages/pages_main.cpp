#include "pages_common.cpp"
#include "home_page.cpp"
#include "tools_page.cpp"
#include "settings_page.cpp"
#include "main_page.cpp"

#include "liveaio_pages_api.h"

#include <QAtomicPointer>
#include <QCoreApplication>
#include <QMetaObject>

namespace {
// Go 侧可能再次请求显示界面。Qt 不允许一个进程里先后跑两个 QApplication，
// 所以这里保留主窗口指针，让第二次请求只是抬窗。
QAtomicPointer<liveaio::pages::MainWindow> g_mainWindow = nullptr;
}  // namespace

extern "C" LIVEAIO_PAGES_API int LiveAIO_PagesRun(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LiveAIO"));
    app.setStyle(QStringLiteral("Fusion"));
    // 关闭工具窗或收起主窗都不能结束应用：整个进程只有这一个 QApplication。
    app.setQuitOnLastWindowClosed(false);

    const QString envRoot = qEnvironmentVariable("LIVEAIO_ROOT");
    liveaio::pages::g_appRoot = envRoot.isEmpty() ? QDir::currentPath() : envRoot;
    QDir::setCurrent(liveaio::pages::g_appRoot);

    auto* core = new liveaio::pages::CoreClient(&app);
    liveaio::pages::installConfigBridge(core);

    liveaio::util::applyThemeName(
        liveaio::pages::configValue(QStringLiteral("theme"),
                                    liveaio::util::defaultThemeName()).toString());
    liveaio::pages::g_minimizeToTray =
        liveaio::pages::configValue(QStringLiteral("minimize_to_tray"), true).toBool();

    auto* win = new liveaio::pages::MainWindow(core);
    core->setPacketCallback([win](const QJsonObject& packet) { win->onCorePacket(packet); });
    core->setStatusCallback([win](bool connected) { win->onCoreStatus(connected); });
    core->connectToCore();

    win->show();
    win->requestInitialState();
    g_mainWindow.storeRelease(win);
    const int code = app.exec();
    g_mainWindow.storeRelease(nullptr);
    return code;
}

extern "C" LIVEAIO_PAGES_API int LiveAIO_PagesShow(void) {
    liveaio::pages::MainWindow* win = g_mainWindow.loadAcquire();
    if (!win || !QCoreApplication::instance()) return 0;
    // 调用方是 Go 的任意线程，必须回到 UI 线程再动窗口。
    QMetaObject::invokeMethod(win, [win]() { win->raiseFromTray(); }, Qt::QueuedConnection);
    return 1;
}
