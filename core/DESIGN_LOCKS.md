# LiveAIO Core — design locks

| Topic | Now | Later |
|-------|-----|-------|
| Process | **LiveAIO.exe**（C++）LoadLibrary **LiveAIOCore.dll** + Pages/Tools dll | — |
| System tray | Core 同进程（打开界面 / 退出） | — |
| UI | C++ **LiveAIOPages.dll** / **LiveAIOTools.dll**；无 Python；无 LiveAIOUI.exe | pages 功能补全 |
| Listen | Core 同进程纯 Go；**connect 时才**拉线路（1/2 chromedp，3/4 proxy_shell） | — |
| Proto decode | **仅 Go** `listener.TryParseFrame` | — |
| UI close | 关窗 → 断 IPC；Core 仍在 | — |
| Explicit Quit | 托盘退出 / `shutdown` / `quit.shutdown_all` | — |
| config writer | **Go core**；UI 仅 `config.get` / `config.set`（成功回执 `config.ok`） | — |
| Protocol docs | [`CONTRACT.md`](CONTRACT.md) + [`CPP_UI_CONTRACT.md`](CPP_UI_CONTRACT.md) | — |
