# LiveAIO Core — design locks

| Topic | Now | Later |
|-------|-----|-------|
| Process roles | **liveaio.exe** = 启动链 + 托盘；**liveaio-core.exe** = 纯服务；**ui_shell.py** = Qt | — |
| System tray | **liveaio** 进程托管（打开界面 / 退出 → shutdown core） | — |
| UI launch | 托盘 / `liveaio --ui` → `python ui_shell.py` | — |
| Listen ownership | **core**：route4=`shellipc`；1/2=`pw_worker`；3=mitm 助手仍 `frame.push` | 3 迁 Go MITM |
| Proto decode | **仅 Go** `parse.TryParseFrame` | — |
| Playwright | 仅 `listener/pw_worker.py`（无 Qt），由 core spawn | — |
| UI close | 无悬浮窗 → 退 Python；core+托盘仍在 | — |
| Explicit Quit | 托盘「退出」→ shutdown core | — |
| config writer | UI 在时 Python 写文件 + `tool.*.set` | — |
