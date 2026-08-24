#pragma once

#ifdef _WIN32
#  ifdef LIVEAIO_TOOLS_BUILD
#    define LIVEAIO_TOOLS_API __declspec(dllexport)
#  else
#    define LIVEAIO_TOOLS_API __declspec(dllimport)
#  endif
#else
#  define LIVEAIO_TOOLS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Tools 只作为 Pages 进程内的插件存在：没有第二个 QApplication，也没有宿主窗口。
 * 打开一个工具窗口。tool_id: "memo" | "danmu" | "overtime"。返回 0 表示成功。 */
LIVEAIO_TOOLS_API int LiveAIO_ToolsOpen(const char* tool_id);

/* 主界面切换主题时同步已打开的工具窗（theme_name 为 util/widgets.cpp 的主题名）。 */
LIVEAIO_TOOLS_API void LiveAIO_ToolsApplyTheme(const char* theme_name);

#ifdef __cplusplus
}
#endif
