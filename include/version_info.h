#ifndef VERSION_INFO_H
#define VERSION_INFO_H

#if defined(__has_include)
#if __has_include("version_auto.h")
#include "version_auto.h"
#endif
#else
#include "version_auto.h"
#endif

#ifndef APP_GIT_VERSION
#define APP_GIT_VERSION "unknown"
#endif

#define APP_VERSION_STRING APP_GIT_VERSION

#endif // VERSION_INFO_H
