#define DEBUG_SESSION

// If any of above are defined then define APP_DEBUG_MODE below, 
// otherwise comment out
#define APP_DEBUG_MODE

// Use:
#define LOGLEVEL_NONE 0
#define LOGLEVEL_ERROR 10
#define LOGLEVEL_WARN 20
#define LOGLEVEL_INFO 30
#define LOGLEVEL_DEBUG 40
#define LOGLEVEL_VERBOSE 50
#define LOGLEVELVERY_VERBOSE 60

#define LOGLEVEL_HIGHEST_RELEASE_LEVEL LOGLEVEL_DEBUG

#define LOG_LEVEL LOGLEVEL_DEBUG

#if LOG_LEVEL >= LOGLEVEL_DEBUG
#define APP_LOG
#endif

#include "app_logger.h"

#ifdef APP_LOG
extern AppLog app_log;
#endif

// Documents APIs that are entry points into the app from
// outaide the app (from mqtt, timers, sensors callback, etc.)
#define _entry_point
