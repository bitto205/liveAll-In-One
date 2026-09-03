#pragma once

#ifdef _WIN32
#  ifdef LIVEAIO_PAGES_BUILD
#    define LIVEAIO_PAGES_API __declspec(dllexport)
#  else
#    define LIVEAIO_PAGES_API __declspec(dllimport)
#  endif
#else
#  define LIVEAIO_PAGES_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Pages module entry — loaded by LiveAIOCore.dll via LoadLibrary/GetProcAddress. */
LIVEAIO_PAGES_API int LiveAIO_PagesRun(int argc, char** argv);

/* 必须在 PagesRun 前调用；用于托盘冷启动 Qt 但不显示主窗口。 */
LIVEAIO_PAGES_API void LiveAIO_PagesSetStartHidden(int hidden);

/* Raise the existing main window. Safe to call from any thread; returns 0 when
   no Pages application is running, so the caller knows it must start one. */
LIVEAIO_PAGES_API int LiveAIO_PagesShow(void);

/* 任意线程调用；不抬主界面，直接控制 Tools 内指定悬浮窗。 */
LIVEAIO_PAGES_API int LiveAIO_PagesOverlayCommand(
    const char* tool_id, const char* action);

/* 状态位：1=已打开，2=边框显示，4=锁定。 */
LIVEAIO_PAGES_API int LiveAIO_PagesOverlayState(const char* tool_id);

#ifdef __cplusplus
}
#endif
