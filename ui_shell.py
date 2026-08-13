"""
ui_shell.py — Python/Qt UI（页面 / 工具窗 / bridge）。

常驻、听帧、托盘：Go liveaio-core.exe。本模块仅 attach core 后跑 Qt。
"""
from __future__ import annotations

import asyncio
import base64
import os
import sys

from PySide6.QtCore import QObject, QThread, QTimer, Qt, QtMsgType, Signal, qInstallMessageHandler
from PySide6.QtGui import QColor, QFont, QIcon, QPainter, QPixmap
from PySide6.QtWidgets import QApplication, QMenu, QSystemTrayIcon

from util.overlay_capture import prepare_app_alpha_format
from util.log_util import ensure_console_logging, ensure_startup_log, get_tagged_logger
from util.paths import app_root, state_file as default_state_file

APP_NAME = "LiveAIO"
logger = get_tagged_logger("main", __name__)
_RUNNING_APP = None


def get_running_app():
    return _RUNNING_APP


def _qt_msg_handler(msg_type, _, msg):
    if "Fixedsys" in msg or "CreateFontFaceFromHDC" in msg:
        return
    if msg_type in (QtMsgType.QtDebugMsg, QtMsgType.QtInfoMsg):
        print(msg)
    elif msg_type == QtMsgType.QtWarningMsg:
        print(f"Qt Warning: {msg}", file=sys.stderr)
    else:
        print(f"Qt: {msg}", file=sys.stderr)


def _set_process_name(name: str) -> None:
    if sys.platform != "win32":
        return
    try:
        import ctypes
        from ctypes import wintypes

        SetProcessDescription = ctypes.windll.kernel32.SetProcessDescription
        SetProcessDescription.argtypes = (wintypes.HANDLE, wintypes.LPCWSTR)
        SetProcessDescription.restype = wintypes.HRESULT
        SetProcessDescription(
            ctypes.windll.kernel32.GetCurrentProcess(), name,
        )
    except Exception:
        pass


def _load_app_icon() -> QIcon | None:
    icon_path = app_root() / "image" / "icon.png"
    if not icon_path.is_file():
        return None
    icon = QIcon(str(icon_path))
    # QIcon.isNull() 只表示对象是否初始化；用 pixmap 可验证资源是否可实际解码渲染。
    if icon.pixmap(64, 64).isNull():
        return None
    return icon


class ListenerThread(QThread):
    message_received = Signal(object)
    status_changed = Signal(bool)

    def __init__(
        self,
        live_id: str,
        route: str = "2",
        state_file: str = "",
        headless: bool = True,
        debug: bool = False,
        force_system: bool = False,
    ):
        super().__init__()
        self._live_id = live_id
        self._route = route
        self._state_file = state_file or str(default_state_file())
        self._headless = headless
        self._debug = debug
        self._force_system = force_system
        self._loop: asyncio.AbstractEventLoop | None = None

    def run(self):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        try:
            ensure_console_logging()
            logger.info("Listener 线程启动，线路=%s", self._route)
            self._loop.run_until_complete(self._listen())
        except RuntimeError as e:
            if "Event loop stopped before Future completed" not in str(e):
                logger.error("Listener 线程异常: %s", e)
        except Exception as e:
            logger.error("Listener 线程异常: %s", e)
        finally:
            if self._route == "3":
                try:
                    from listener.listener3 import _teardown_local_redirector
                    if self._loop and not self._loop.is_closed():
                        self._loop.run_until_complete(_teardown_local_redirector())
                except Exception:
                    pass
            try:
                if self._loop and not self._loop.is_closed():
                    pending = [t for t in asyncio.all_tasks(self._loop) if not t.done()]
                    for t in pending:
                        t.cancel()
                    if pending:
                        self._loop.run_until_complete(
                            asyncio.gather(*pending, return_exceptions=True)
                        )
            except Exception:
                pass
            # Windows Proactor：关 loop 前收尾 asyncgen / executor，给管道 transport 收尾机会
            try:
                if self._loop and not self._loop.is_closed():
                    self._loop.run_until_complete(self._loop.shutdown_asyncgens())
            except Exception:
                pass
            try:
                if self._loop and not self._loop.is_closed() and hasattr(
                    self._loop, "shutdown_default_executor"
                ):
                    self._loop.run_until_complete(self._loop.shutdown_default_executor())
            except Exception:
                pass
            try:
                if self._loop and not self._loop.is_closed():
                    self._loop.run_until_complete(asyncio.sleep(0.05))
            except Exception:
                pass
            try:
                if self._loop and not self._loop.is_closed():
                    self._loop.close()
            except Exception:
                pass
            self._loop = None

    async def _listen(self):
        if self._route == "4":
            from listener.listener4 import start_listener
            await start_listener(
                callback=lambda msg: self.message_received.emit(msg),
                on_status=lambda c: self.status_changed.emit(c),
            )
            return
        if self._route == "3":
            from listener.listener3 import start_listener
            await start_listener(
                callback=lambda msg: self.message_received.emit(msg),
                on_status=lambda c: self.status_changed.emit(c),
            )
            return
        if self._route == "1":
            from listener.listener1 import _run
        else:
            from listener.listener2 import _run
        await _run(
            live_id=self._live_id,
            callback=lambda msg: self.message_received.emit(msg),
            state_file=self._state_file,
            headless=self._headless,
            debug=self._debug,
            on_status=lambda c: self.status_changed.emit(c),
            force_system=self._force_system,
        )

    def stop(self):
        if self._route == "4":
            try:
                from listener.listener4 import request_listener_stop
                if request_listener_stop():
                    return
            except Exception:
                pass
        if self._route == "1":
            try:
                from listener.listener1 import request_listener_stop
                if request_listener_stop():
                    return
            except Exception:
                pass
        if self._route == "2":
            try:
                from listener.listener2 import request_listener_stop
                if request_listener_stop():
                    return
            except Exception:
                pass
        if self._loop and not self._loop.is_closed():
            if self._route == "3":
                import listener.listener3 as l3
                try:
                    fut = asyncio.run_coroutine_threadsafe(l3.shutdown(), self._loop)
                    fut.result(timeout=8)
                except Exception:
                    pass
            if self._loop.is_running():
                self._loop.call_soon_threadsafe(self._loop.stop)


class App(QObject):
    message_received = Signal(object)
    status_changed = Signal(bool)

    def __init__(self, argv: list):
        super().__init__()
        global _RUNNING_APP
        _RUNNING_APP = self
        self._qt = QApplication(argv)
        self._qt.setApplicationName(APP_NAME)
        self._qt.setApplicationDisplayName(APP_NAME)
        self._qt.setOrganizationName(APP_NAME)
        icon = _load_app_icon()
        if icon is not None:
            self._qt.setWindowIcon(icon)
        import config as _cfg
        # 系统托盘由 liveaio-core 原生托管；UI 壳不建 Qt 托盘
        self._minimize_to_tray = bool(_cfg.get("minimize_to_tray", True))
        self._qt.setQuitOnLastWindowClosed(not self._minimize_to_tray)
        _set_process_name(APP_NAME)
        self._thread: ListenerThread | None = None
        self._pending_connect: tuple[str, str] | None = None
        self._stopping = False
        self._win = None  # 懒加载：只有“显示主窗口”时才创建
        self._tray: QSystemTrayIcon | None = None
        self._app_icon = icon
        self._core = None  # bridge.CoreClient | None
        self._core_route: str | None = None
        self._msg_from_core = False
        self._kill_core_on_exit = False  # 仅「完全退出」为 True

        self.message_received.connect(self._on_business_message)
        self.status_changed.connect(self._on_business_status)

        self._ensure_main_window()

    def attach_core_client(self, client) -> None:
        """Bind CoreClient started by main launcher."""
        self._core = client
        if client is None:
            return
        try:
            client.event_received.connect(self._on_core_event)
        except Exception:
            pass
        try:
            from listener.core_feed import bind
            bind(self._push_frame_to_core)
        except Exception:
            pass
        try:
            from bridge.tool_sync import push_all_tool_settings
            push_all_tool_settings()
        except Exception:
            pass
        logger.info("CoreClient attached")

    def _on_core_event(self, env: object) -> None:
        if not isinstance(env, dict):
            return
        op = env.get("op")
        if op == "message":
            from bridge.message_codec import envelope_to_message
            from util.models import CONTROL_STATUS_FINISH, ControlMessage
            msg = envelope_to_message(env)
            if msg is not None:
                self._msg_from_core = True
                try:
                    self.message_received.emit(msg)
                finally:
                    self._msg_from_core = False
                if isinstance(msg, ControlMessage) and msg.status == CONTROL_STATUS_FINISH:
                    logger.info("core: 下播控制，断开监听")
                    self.stop_listener()
            return
        if op == "status":
            self.status_changed.emit(bool(env.get("connected")))
            return
        if op == "error":
            logger.warning("core error: %s %s", env.get("code"), env.get("msg"))
            return
        if op == "tick":
            try:
                from tools.tool_common import dispatch_core_tick
                dispatch_core_tick(env)
            except Exception:
                pass
            return
        if op in ("ledger", "danmu.show", "memo.item"):
            try:
                from tools.tool_common import dispatch_core_tool_event
                dispatch_core_tool_event(env)
            except Exception:
                pass

    def _push_frame_to_core(self, raw: bytes) -> None:
        if self._core is None or not getattr(self._core, "is_ready", False):
            return
        try:
            self._core.send({
                "op": "frame.push",
                "payload_b64": base64.b64encode(raw).decode("ascii"),
            })
        except Exception as e:
            logger.debug("frame.push failed: %s", e)

    def _arm_core_session(self, route: str, live_id: str = "", *, force_system: bool = False) -> None:
        """通知 core 接管听帧（1/2 worker、4 shellipc、3 等 frame.push）。"""
        if self._core is None:
            return
        try:
            from listener.core_feed import bind
            bind(self._push_frame_to_core)
        except Exception:
            pass
        self._core_route = str(route)
        try:
            self._core.send({
                "op": "connect",
                "live_id": live_id or "",
                "route": str(route),
                "force_system": bool(force_system),
            })
        except Exception:
            pass
        try:
            from bridge.tool_sync import push_all_tool_settings
            push_all_tool_settings()
        except Exception:
            pass

    def _setup_tray(self) -> None:
        """创建托盘图标与菜单（不依赖 pages/*）。"""
        if self._tray is not None:
            return

        # 用一个和 MainPage 类似的简易圆点图标，避免 icon.png 不存在时崩
        if self._app_icon is not None:
            tray_icon = self._app_icon
        else:
            size = 32
            pix = QPixmap(size, size)
            pix.fill(Qt.transparent)
            p = QPainter(pix)
            p.setRenderHint(QPainter.Antialiasing)
            p.setBrush(QColor(90, 165, 255))
            p.setPen(Qt.NoPen)
            p.drawEllipse(2, 2, size - 4, size - 4)
            p.setPen(QColor("#ffffff"))
            f = QFont()
            f.setBold(True)
            f.setPixelSize(14)
            p.setFont(f)
            p.drawText(pix.rect(), Qt.AlignCenter, "A")
            p.end()
            tray_icon = QIcon(pix)

        self._tray = QSystemTrayIcon(tray_icon, self)
        self._tray.setToolTip(APP_NAME)

        menu = QMenu()
        show_action = menu.addAction("显示主窗口")
        menu.addSeparator()
        quit_action = menu.addAction("退出")

        show_action.triggered.connect(self._restore_from_tray)
        quit_action.triggered.connect(self._quit_application)

        self._tray.setContextMenu(menu)
        self._tray.activated.connect(self._on_tray_activated)
        self._tray.show()

    def _on_tray_activated(self, reason) -> None:
        if reason == QSystemTrayIcon.DoubleClick:
            self._restore_from_tray()

    def apply_minimize_to_tray_setting(self, enabled: bool) -> None:
        """关闭主窗时是否退出 UI 壳（core 常驻由原生托盘接管）。"""
        self._minimize_to_tray = bool(enabled)
        self._qt.setQuitOnLastWindowClosed(not self._minimize_to_tray)
        if not self._minimize_to_tray:
            self._ensure_main_window()
            if self._win is not None:
                self._win.show()

    def _ensure_main_window(self) -> None:
        """首次创建 MainPage，并把 HomePage 的回调绑定起来。"""
        if self._win is not None:
            return
        from pages.main_page import MainPage
        self._win = MainPage(on_hide_callback=self._release_main_window)
        if self._app_icon is not None:
            self._win.setWindowIcon(self._app_icon)

        from pages.home_page import HomePage
        home = self._win.get_page(HomePage)
        if home:
            home.set_callbacks(
                on_connect=self.connect,
                on_disconnect=self.stop_listener,
            )

    def _release_main_window(self) -> None:
        """关闭主窗：无 UI 需求时退出 Python 壳只留 core；有工具悬浮窗则暂留壳+托盘。"""
        from tools.tool_common import any_tool_ui_visible, bind_tools_page
        bind_tools_page(None)
        win = self._win
        self._win = None
        try:
            if win is not None:
                win.deleteLater()
        except Exception:
            pass

        if self._minimize_to_tray and not any_tool_ui_visible():
            self._exit_ui_leave_core()
            return

        if self._tray is not None:
            if not self._tray.isVisible():
                self._tray.show()
            self._tray.showMessage(
                APP_NAME,
                "主窗已关；工具悬浮窗仍需界面进程。完全无界面请先关工具窗。",
                QSystemTrayIcon.Information,
                2500,
            )

    def _exit_ui_leave_core(self) -> None:
        """退出 UI 壳，不断开 core。再次运行 main 可附着回来。"""
        self._kill_core_on_exit = False
        try:
            self.disconnect_and_wait()
        except Exception:
            pass
        if self._tray is not None:
            try:
                self._tray.hide()
            except Exception:
                pass
            self._tray = None
        if self._core is not None:
            try:
                self._core.detach()
            except Exception:
                pass
            self._core = None
        logger.info("UI 壳退出，liveaio-core 继续运行（请看系统托盘 LiveAIO）")
        QApplication.instance().quit()

    def _on_business_message(self, msg) -> None:
        # 线路 1/3：本地解析 → 喂 core 做工具过滤；线路 2/4 消息已从 core 来
        # 工具业务只吃 core 的 tick / danmu.show / memo.item（见 tools/iface）
        if not self._msg_from_core:
            try:
                from bridge.tool_sync import ingest_message
                ingest_message(msg)
            except Exception:
                pass
        if self._win is not None:
            try:
                self._win.broadcast_message(msg)
            except Exception:
                pass

    def _on_business_status(self, connected: bool) -> None:
        from tools.tool_common import dispatch_status_to_open_tools
        dispatch_status_to_open_tools(connected)
        if self._win is not None:
            try:
                self._win.broadcast_status(connected)
                return
            except Exception:
                pass
        if self._tray is not None:
            text = "已连接" if connected else "已断开"
            self._tray.showMessage(APP_NAME, text, QSystemTrayIcon.Information, 2000)

    def _restore_from_tray(self) -> None:
        self._ensure_main_window()
        self._win.showNormal()
        self._win.activateWindow()
        self._win.raise_()

    def _quit_application(self) -> None:
        """完全退出：关工具 + 停 core。"""
        self._kill_core_on_exit = True
        if self._thread is not None:
            self.disconnect_and_wait()
        if self._tray is not None:
            self._tray.hide()
        from tools.tool_common import shutdown_all_tools
        shutdown_all_tools()
        if self._core is not None:
            try:
                self._core.shutdown_core()
            except Exception:
                pass
            self._core = None
        QApplication.instance().quit()

    def connect(self, live_id: str, route: str = "2"):
        from pages.home_page import HomePage
        import config as _cfg
        home = self._win.get_page(HomePage) if self._win is not None else None
        if home:
            home.preempt_other_listeners(route)

        force_system = bool(_cfg.get("use_system_browser", False))
        self._arm_core_session(route, live_id, force_system=force_system)
        logger.info("请求连接，线路=%s（听帧/解析在 core）", route)

        # 1/2：core 拉起 pw_worker，UI 不再跑 Playwright
        if str(route) in ("1", "2"):
            if self._thread and self._thread.isRunning():
                if home:
                    home.begin_listener_switch(route)
                self._pending_connect = None
                self._stop_listener()
            if home:
                home.end_listener_switch()
            return

        self._pending_connect = (live_id, route)
        if self._thread and not self._thread.isRunning():
            self._thread = None
        if self._thread and self._thread.isRunning():
            if home:
                home.begin_listener_switch(route)
            if not self._stopping:
                self._stopping = True
                self._stop_listener()
            return
        if home:
            home.end_listener_switch()
        self._start_listener()

    def _start_listener(self) -> None:
        pending = self._pending_connect
        if not pending:
            return
        live_id, route = pending
        self._pending_connect = None

        # 3=mitm 助手；4=伴侣 patch 挂起（听帧在 core）
        import config as _cfg
        force_system = bool(_cfg.get("use_system_browser", False))
        self._thread = ListenerThread(live_id, route=route, force_system=force_system)
        self._thread.message_received.connect(self.message_received)
        self._thread.status_changed.connect(self.status_changed)
        self._thread.finished.connect(self._on_listener_finished)
        self._thread.start()

    def _on_listener_finished(self) -> None:
        thread = self.sender()
        if not isinstance(thread, QThread):
            return
        pending = self._pending_connect
        if self._thread is thread:
            self._thread = None
        self._stopping = False
        if thread.isRunning():
            thread.wait(3000)
        thread.deleteLater()
        from pages.home_page import HomePage
        home = self._win.get_page(HomePage) if self._win is not None else None
        if home:
            home.end_listener_switch()
            if not pending:
                home.clear_listener_state()
        if pending:
            self._start_listener()

    def _stop_listener(self) -> None:
        thread = self._thread
        if not thread or not thread.isRunning():
            return
        self._stopping = True
        thread.stop()

    def stop_listener(self, *, switching: bool = False) -> None:
        if not switching:
            self._pending_connect = None
        if self._core is not None and self._core_route is not None and not switching:
            try:
                self._core.send({"op": "disconnect"})
            except Exception:
                pass
        if self._stopping:
            return
        logger.info("请求断开 listener%s", "（切换线路）" if switching else "")
        self._stop_listener()

    def disconnect(self, *, switching: bool = False) -> None:
        self.stop_listener(switching=switching)

    def disconnect_and_wait(self, timeout_ms: int = 5000) -> None:
        self._pending_connect = None
        try:
            from listener.core_feed import bind
            bind(None)
        except Exception:
            pass
        if self._core is not None and self._core_route is not None:
            try:
                self._core.send({"op": "disconnect"})
            except Exception:
                pass
        self._core_route = None
        thread = self._thread
        if not thread:
            return
        if thread.isRunning():
            self._stopping = True
            self._thread = None
            thread.stop()
            thread.wait(timeout_ms)
        self._thread = None
        self._stopping = False

    def run(self) -> int:
        if self._win is not None:
            self._win.show()
        result = self._qt.exec()
        self.disconnect_and_wait()
        if self._core is not None:
            try:
                if self._kill_core_on_exit:
                    self._core.shutdown_core()
                else:
                    self._core.detach()
            except Exception:
                pass
            self._core = None
        return result


def _warmup_background() -> None:
    from util.playwright_bootstrap import log_browser_mode
    from util.browser_trim import trim_playwright_browsers

    log_browser_mode()
    try:
        trim_playwright_browsers(app_root() / "browsers")
    except Exception:
        pass


def _defer_save_location() -> None:
    try:
        from listener.listener4 import save_location
        save_location()
    except Exception:
        pass


def run_ui(core_client=None) -> int:
    """进入 Qt 事件循环。"""
    ensure_startup_log()
    prepare_app_alpha_format()
    qInstallMessageHandler(_qt_msg_handler)
    logger.info("%s 启动", APP_NAME)

    from util.playwright_bootstrap import configure_playwright_browsers
    configure_playwright_browsers(app_root())

    app = App(sys.argv)
    if core_client is not None:
        app.attach_core_client(core_client)
    QTimer.singleShot(0, _warmup_background)
    QTimer.singleShot(0, _defer_save_location)
    return app.run()


def main() -> int:
    root = app_root()
    os.chdir(root)
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))

    from bridge.core_client import CoreClient, probe_core

    if not probe_core():
        print("未检测到 liveaio-core，请先运行 LiveAIO.exe", file=sys.stderr)
        return 1

    client = CoreClient(app_root=root)
    if not client.start(timeout=10.0):
        print("无法连接 liveaio-core", file=sys.stderr)
        client.detach()
        return 1

    try:
        return run_ui(core_client=client)
    finally:
        try:
            client.detach()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
