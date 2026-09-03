# main — Go 调试入口

本目录只放**可执行程序的调试入口**，不是整个 Go 工程的根。

| 文件 | 作用 |
|------|------|
| `main.go` | 调试启动：`go run .` / `go build -o liveaio-debug.exe .` |
| `*.exe` | 本地 `go build` 产物（已 gitignore，勿提交） |
| `requirements.txt` | 旧 Python 依赖清单（项目已迁 C++/Go，仅作归档） |

## 为什么 `go.mod` 还在仓库根目录？

Go 模块根必须包含 `core/`、`listener/`、`util/` 等包的路径。  
`go.mod` / `go.sum` 放在根目录是语言要求，不能挪到 `main/`。

生产构建走 `build/build.ps1` → `build/build_work/custom/LiveAIO.exe`（C++ 壳 + `LiveAIOCore.dll`），不依赖本目录下的 exe。
