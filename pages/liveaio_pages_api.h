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

/* Raise the existing main window. Safe to call from any thread; returns 0 when
   no Pages application is running, so the caller knows it must start one. */
LIVEAIO_PAGES_API int LiveAIO_PagesShow(void);

#ifdef __cplusplus
}
#endif
