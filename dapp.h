// Copyright 2020 Brenton Olander

#include "esphome.h"
#include "app_defs.h"
#include "helper.h"
#include "water_flow_sensor.h"
#include "translation_unit.h"

using namespace esphome;
//using namespace time;
extern mqtt::MQTTClientComponent *mqtt_client;
using namespace mqtt;
using namespace json;
//extern homeassistant::HomeassistantTime *sntp_time;
extern sntp::SNTPComponent *sntp_time;
//using namespace output;
#ifdef light
extern light::LightState *_status_led;
#endif
//#ifdef gpio
// extern gpio::GPIOSwitch *valve_open;
// extern gpio::GPIOSwitch *valve_close;
//#endif

extern display::Font *fontOpenSans;
extern ssd1306_i2c::I2CSSD1306 *ssd1306_i2c_i2cssd1306;

// water usage include here because it needs the defs above
#include "water_usage.h"



class dApp {
  std::string app_;

  bool on_boot_called = false;
  bool haveRetainedProperties_ = false;

  // wwh stuff
  // When we startup we really don't know the status of the valve and we
  // want to be careful about forcing a state.

  // The problems are that the valve can't tell us its state, we want to
  // know its state, and we don't want to force a state because it might
  // not be safe. We want to know the state that it was last in, if that
  // is possible.
  // The strategy we use is we wait for an mqtt stat call with retained 
  // values. If we get a "valve_open" property than we set it to that.
  // If we don't get that property (unlikely) or we don't get a stat 
  // call at all (likely when new or mqtt server is down) then we use 
  // the value set below.
  bool valve_is_open_ = true;
  // The whole point is to get the valve state in a known state safely on
  // startup. We want it to be the last state. We depend on an mqtt
  // retained message (/stat) to achieve this. It would be much better to 
  // save this state in the flash, butI don't know how to this.

  // A special case the valve operation on startup
  bool do_not_send_retained_on_next_valve_operation_ = false;

  // NOTE: when user changes the unit of measure (e.g., "gal" to "L") we 
  // have to translate usage and upm values. When we started the project
  // this was trivial, but now it is much more work. We could just dump
  // these values, but translation is path we or on. Below is the master
  // list of the data that needs translating:

  //    specific_allowances_
  //    currentWaterUsage
  //    WaterUsage* lists:
  //      hourlyWaterUsage_;
  //      dailyWaterUsage_;
  //      sessionWaterUsage_;
  //      namedWaterUsage_;
  //    Anything with "upm" in its name:
  //      max_upm_
  //      max_upm_plus_
  //      upm_base_
  //    A few things with "usage" in its name:
  //      max_usage_ 
  //      max_usage_plus_ 

  //void convert_uom(TranslationUnit* xlate_from, TranslationUnit* xlate_to) {
  void convert_uom(const char* from, float old_calibrate_factor=0) {
  
    TranslationManager::ConvertType convert_code = xlate_mgr_.get_convert_code(from);
    
    specific_allowances_.convert_uom( [=](float &val) {
      return xlate_mgr_.convert(convert_code, val, old_calibrate_factor);
    });
    currentWaterUsage.convert_uom( [=](float &val) {
      return xlate_mgr_.convert(convert_code, val, old_calibrate_factor);
    });
    specific_allowances_.convert_uom( [=](float &val) {
      return xlate_mgr_.convert(convert_code, val, old_calibrate_factor);
    });
    hourlyWaterUsage_.convert_uom( [=](float &val) {
      return xlate_mgr_.convert(convert_code, val, old_calibrate_factor);
    });
    dailyWaterUsage_.convert_uom( [=](float &val) {
      return xlate_mgr_.convert(convert_code, val, old_calibrate_factor);
    });
    sessionWaterUsage_.convert_uom( [=](float &val) {
      return xlate_mgr_.convert(convert_code, val, old_calibrate_factor);
    });
    namedWaterUsage_.convert_uom( [=](float &val) {
      return xlate_mgr_.convert(convert_code, val, old_calibrate_factor);
    });

    max_upm_ = xlate_mgr_.convert(convert_code, max_upm_, old_calibrate_factor);
    upm_base_ = xlate_mgr_.convert(convert_code, upm_base_, old_calibrate_factor);
    if (max_usage_ != -1) {
      max_usage_ = xlate_mgr_.convert(convert_code, max_usage_, old_calibrate_factor);
    }

    // calc max_upm_plus_ and max_usage_plus_
    calc_max_plus_values();

    // Update water flow sensor
    //wf_->set_translation_unit(xlate_);

  }

  // -1: not tested yet, 0: tested not valid, 1: tested is valid
  int time_is_valid_ = -1;
  // User can calibrate the device pulses to user units
  float calibrate_factor_ = 1.0;
  bool over_limit_ = false;

  // When do we alarm?
  // There are a number of factors. All of these are sent to us from our 
  // controller (the smart home subsystem).
  //  - max water flow value 
  //  - an initial surge allowance in secords 
  //      This is to accomadate the initial filling of possibly empty pipes.
  //  - max usage (optional). 
  //      This will acumulate until a new max 
  //      usage is received. When the usage is exceed we alarm. This is useful 
  //      for instance, during a "sleeptime" period. You can expect a certain
  //      amount of wf_off usage during this period, for say briefly running the 
  //      faucet, flushing the toilet a few times, but not allow a large usage 
  //      of water.
  //  - specific allowances 
  //      specific allowance set temporary flow and usage allowances. They are 
  //      sent and revoked by the controller. They also have a timeout value
  //      for automatic revocation in case the controller, for whatever reason,
  //      does not send a revocation. Specific allowances are for thing like a
  //      washer running or an irrigation valve is open. If the contoller can 
  //      detect, for instance, that an irrigation valve is open, it will send 
  //      us a specific allowance that says, in effect: expect this amount of
  //      additional water flow and this amount of additional usage. I will
  //      revoke this at some point, but if I don't, please automatically
  //      revoke in this amount of time. The can be multiple specific allowances
  //      at any time

  struct SpecificAllowance {
      std::string name;
      float       upm = 0;
      float       usage = 0;
      time_t      expire_time = 0; 
      bool        create_named_session = false;

      SpecificAllowance(
        const std::string&  _name,
        float               _upm,
        float               _usage,
        time_t              _expire_time,
        bool                _create_named_session):
          name(_name),
          upm(_upm),
          usage(_usage),
          expire_time(_expire_time),
          create_named_session(_create_named_session) {
          }

      SpecificAllowance(const JsonObject& jo):
        name(getString(jo, "name", "")),
        upm(getFloat(jo, "upm", 0)),
        usage(getFloat(jo, "usage", 0)),
        expire_time(getInt(jo, "expire_time", 0)),
        create_named_session(getBool(jo, "cns", false))
      { }

      void convert_uom(std::function<float(float &)>f) {
          upm = f(upm);
          usage = f(usage);
      }
  
      JsonObject& toJson() const {
          JsonObject& jo = global_json_buffer.createObject();

          jo["name"] = name;
          jo["upm"] = upm;
          jo["usage"] = usage;
          jo["expire_time"] = expire_time;
          jo["cns"] = create_named_session;

          return jo;
      }

  
  };

  struct SpecificAllowances: public std::vector<SpecificAllowance> {

    SpecificAllowances() {}

    SpecificAllowances(const JsonArray& ja) {
      for (int i = 0; i < ja.size(); ++i) {
        if (ja[i].is<JsonObject>()) {
          push_back(SpecificAllowance((const JsonObject&)ja[i]));
        }
      }
    }

    void convert_uom(std::function<float(float &)>f) {
        for (auto it = begin(); it != end(); ++it) {
            it->convert_uom(f);
        }
    }

    void add_item(const std::string& name, float upm, float usage, 
      time_t expire_time, bool create_named_session) {
      this->delete_item(name);
      push_back(SpecificAllowance(name, upm, usage, expire_time, create_named_session));
    }

    void delete_item(const std::string& name) {
      auto now = sntp_time->now().timestamp;
      auto it = begin();
      while ( it != end()) {
        if (it->name == name || it->expire_time >= now) {
          // delete
          it = erase(it);
        } else {
          ++it;
        }
      }
    }

    SpecificAllowance* get(const std::string& name) {
      for (auto it = begin(); it != end(); ++it) {
        if (it->name == name) {
          return &*it;
        }
      }
      return nullptr;
    }

    // Note that we delete allowances without notifying caller
    // This should be OK because these are allowance that have not been
    // properly explicitly deleted. Auto created named sessions will be
    // timed deleted elsewhere.
    void get_totals(float& upm_allowance, float& usage_allowance) {
      auto now = sntp_time->now().timestamp;
      auto it = begin();
      while ( it != end()) {
        if (it->expire_time >= now) {
          // delete
          it = erase(it);
        } else {
          upm_allowance += it->upm;
          usage_allowance += it->usage;
          ++it;
        }
      }
    }

    JsonArray& toJson() const {
      JsonArray& ja = global_json_buffer.createArray();
      for (auto it = begin(); it != end(); ++it) {
        ja.add(it->toJson());
      }
      return ja;
    }


  };


  SpecificAllowances specific_allowances_;

  void calc_max_plus_values() {
      float upm_allowance = 0;
      float usage_allowance = 0;
      specific_allowances_.get_totals(upm_allowance, usage_allowance);

      max_upm_plus_ = max_upm_ < 0 ? max_upm_ : max_upm_ + upm_allowance;

      if (max_usage_ > 0) {
        max_usage_plus_ =  max_usage_ + usage_allowance;
      }

  }

  // Negative for no upper limit
  float max_upm_ = -1.0;
  // Max allowed water flow + extant specific allowances;
  float max_upm_plus_ = 0.0;
  float max_usage_ = -1;
  float max_usage_plus_ = -1;


  // The water flow when no water should be flowing. You would think
  // this would always be 0, but I'm making an allowance in case the
  // user has a unfixable leak of some kind. 
  float upm_base_ = 0.0;
  pulse_counter::pulse_counter_t pulses_base_ = 0.0;


  // Test period seconds. We have to overlimit during a test period. A single
  // spike is not sufficient to alarm.
  // Should be a multiple of wf_report_interval_secs_. If not it will end up being rounded up
  // to a multiple of wf_report_interval_secs_ anyway.
  static const int test_period_secs_default_ = 15;

  int test_period_secs_ = test_period_secs_default_;

  // Allow an initial surge when flow starts.
  // The pipes in an irrigation system are often empty when a
  // valve is first turned on. This allow time for the water
  // flow surge until the pipes are filled.
  int allow_initial_surge_seconds_ = 30;
  int grace_for_surge_ = allow_initial_surge_seconds_;

  // Water is/is not flowing
  //bool g_wf_off = true;

  int secs_over_limit_ = 0;

  // How often do we publish usage?
  int publish_usage_secs_ = 60;
  int secs_since_last_publish_ = 0;

  // We prefix out mqtt messages with this prefix. The value originates in the 
  // yaml layer and is passed to us in on_boot. 
  std::string mqtt_topic_prefix_; 

  // MQTT topics
  std::string mqttTopicClosedUsageState_ = "/sensor/wf/closed_usage/state";
  std::string mqttTopicStat_ = "/stat";
  std::string mqttTopicProp_ = "/prop";
  std::string mqttSensorWfOverlimitStatus_ = "/sensor/wf/over_limit/status";
  std::string mqttSensorWfCurrentUsageState_ = "/sensor/wf/usage/current/state";
  std::string mqttSensorWfHourlyUsageStatus_ = "/sensor/wf/usage/hourly/state";
  std::string mqttSensorWfDailyUsageStatus_ = "/sensor/wf/usage/daily/state";
  std::string mqttSensorWfSessionUsageState_ = "/sensor/wf/usage/session/state";
  std::string mqttSensorWfNamedUsageState_ = "/sensor/wf/usage/named/state";

  TranslationManager xlate_mgr_;

  // GallonTranslationUnit gal_xlate_; 
  // LiterTranslationUnit liter_xlate_;
  // // Setup initially for gallons
  // TranslationUnit* xlate_ = &gal_xlate_;

  // How often do we publish usage values to waterflow per minute publishing
  // We currently publish water flow every 15sec

  WaterUsageTimed           currentWaterUsage;
  WaterUsagePeriodList      hourlyWaterUsage_;
  WaterUsagePeriodList      dailyWaterUsage_;
  WaterUsageSessionList     sessionWaterUsage_;
  WaterUsageNamedList       namedWaterUsage_;

  WaterflowSensor* wf_;

public:
  dApp();

  WaterflowSensor* _entry_point create_wf_sensor(int pin) {
    APP_LOG_ENTER("create_wf_sensor()");

    wf_ = new WaterflowSensor(pin, xlate_mgr_);

    APP_LOG_EXIT("create_wf_sensor");

    return wf_;

}

  void _entry_point on_boot(const char* app, int wf_report_wf_off_interval_secs, int wf_report_fast_interval_secs);

  // Experimenting with new (for me) c++ 11 getter/setter syntax
  // usage is: tu.calibrate_factor()
  //            tu.calibrate_factor() = 12.3
  auto calibrate_factor()       -> float&       { return calibrate_factor_; }
  auto calibrate_factor() const -> const float& { return calibrate_factor_; }
  void makeMqttTopics(const std::string& prefix);
  JsonObject& toJson(JsonObject& jo, const char* prop_name=nullptr);
  void _entry_point send_retained_properties();
  void send_property(const char* prop_name);
  void _entry_point process_stat(const JsonObject& x);
  void _entry_point reset();
  void _entry_point process_wf_on_value(float upm);
  void _entry_point process_properties(const JsonObject& jo, bool fromRetainedProperties=false);
  void _entry_point add_allowance(const JsonObject& jo);
  void _entry_point delete_allowance(const JsonObject& jo);
  void _entry_point on_new_hour();
  void _entry_point on_new_day();
  void SetStatusLED(float r, float g, float b) const;
  void SetStatusLEDBasedOnValveStatus() const;
  void _entry_point get_closed();
  void _entry_point clear_closed();
  //float _entry_point process_pulse_counter(float pulses) ;
  // WWH functions
  void _entry_point set_valve_status(bool open);
  void open_valve();
  void close_valve();
  void _entry_point toggle_valve();
  void _entry_point add_named_usage(const JsonObject& jo) ;
  void _entry_point delete_named_usage(const JsonObject& jo);
  void delete_named_usage(const std::string name, bool cancel);
  void _entry_point set_report_period_secs(const JsonObject& jo);
  void refresh_display(float pulses);

};

extern dApp dapp;

