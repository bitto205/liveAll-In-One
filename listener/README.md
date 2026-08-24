# listener — 平面纯 Go

| 文件 | 内容 |
|------|------|
| `listener.go` | Manager / 路由注册 / 帧解析 `TryParseFrame` |
| `browser.go` | 浏览器公用、线路 1/2、进房检查、登录 |
| `route3.go` | 线路 3（proxy_shell；系统代理读写在 companion） |
| `route4.go` | 线路 4 PrepareR4 + Shell IPC |
| `companion.go` | 伴侣 patch、系统代理、hideCmd（Windows） |
| `livepb/` | protobuf |
| `proxy_shell*` | MITM 二进制与源码 |
