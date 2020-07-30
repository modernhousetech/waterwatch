#pragma once

#include "esphome.h"
using namespace esphome;

//#define APP_LOG

#ifdef APP_LOG
#define APP_LOG_ENTER(...) app_log.api_enter(__VA_ARGS__)
#define APP_LOG_EXIT(api_name) app_log.api_exit(api_name)
#define APP_LOG_LOG(...) app_log.log(AppLogLine::Type::Line, __VA_ARGS__)
#define APP_LOG_EMIT_ON(on) app_log.set_emit_on(on)

// extern void outd(const char* line);


  // Log
  // process_properties {
  //    timezone specified....
  //    max_upm was n now x
  //    valve_open() {}
  // } process_properties
  //
  // process_properties {
  //    timezone specified....
  //    max_upm was n now x
  //        some_function {
  //            did this
  //            did that
  //    } some_function
  //    max waterflow...
  // } process_properties

  struct AppLogLine {
    std::string text;
    int level = 0;
    enum Type {
      Line,
      Api_enter,
      Api_exit,
    } type = AppLogLine::Type::Line;

    AppLogLine(
      const std::string& line_, 
      int level_=0, 
      AppLogLine::Type type_=AppLogLine::Type::Line):
        text(line_),
        level(level_),
        type(type_) {
        
        if (level > 3) {
          level = 3;
        }
    }

    void out() {
      out(text, level, type);
   }

    static void out(const std::string& _text, int _level, Type _type ) {
      static char spaces[] = "                   ";
      char indent[20];

      int level_spaces = _level > 4 * 4 ? 4 : _level * 4;
      strncpy(indent, spaces, level_spaces);
      indent[level_spaces] = '\0';
      std::string line = indent;
      if (_type == Line) {
        line += _text;
      } else if (_type == Api_enter) {
        line += _text + " {";
      } else {
        line += "} " + _text;
      }

      ESP_LOGD("main", line.c_str());

    }
  };

  struct AppLog: public std::vector<AppLogLine> {
    // The option to not emit is too embargo debug lines on
    // device start up, during which time the logger is not
    // active, and any debug statements will be lost. The logger 
    // seems to use mqtt, when that is the active comm unit, and
    // until that is setup nothing will be logged.
    bool emit_on = false;
    int level = 0;

    void api_enter(const char* api_name, ...) {
      va_list arg;
      va_start(arg, api_name);
      log(AppLogLine::Type::Api_enter, api_name, arg);
      ++level;
    }

    void api_exit(const char* api_name) {
      --level;
      log(AppLogLine::Type::Api_exit, api_name);
    }
    void log(
          AppLogLine::Type type,
          const char *format, ...
      ) {

      va_list arg;
      va_start(arg, format);

      log(type, format, arg);
    }

    void log(
          AppLogLine::Type type,
          const char *format, va_list arg
      ) {

      char buf[128];
      int ret = vsnprintf(buf, sizeof(buf) - 1, format, arg);
      if (ret < 0) {
        strcpy(buf, "Encoding error at AppLog.log"); 
      }


      if (emit_on) {
        AppLogLine::out(buf, level, type);
      } else {

        // Sanity size check
        if (size() > 128) {
          erase(begin(), begin() + 32);
          push_back(AppLogLine("Saved log lines > 128 -- purged oldest 32", 
            0, AppLogLine::Type::Line));
        }
        push_back(AppLogLine(buf, level, type));
      }
    }

    void set_emit_on(bool on) { 
      if (on != emit_on) {
        ESP_LOGD("main", "set_emit_on");
        emit_on = on; 
        if (emit_on) {
          // Output everything we've been saving
          for (auto it = begin(); it != end(); ++it) {
            it->out();
          }
          clear();
        }
      }
    }
  };


#else
#define APP_LOG_ENTER(api_name) 
#define APP_LOG_ENTER_ARG(api_name, arg) 
#define APP_LOG_EXIT(api_name) 
#define APP_LOG_LOG(text) 
//#define APP_LOG_LOG_WITH_DATA(a, b) 
#define APP_LOG_EMIT_ON(on) 

#endif

