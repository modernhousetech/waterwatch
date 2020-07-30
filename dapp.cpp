// Copyright 2020 Brenton Olander

#include "esphome.h"
#include "dapp.h"


#define FW_VERSION_BASE "0.05.22"

#ifdef APP_DEBUG_MODE
#define FW_VERSION_DEBUG FW_VERSION_BASE"-D"
#else
#define FW_VERSION_DEBUG FW_VERSION_BASE
#endif

#if LOG_LEVEL > LOGLEVEL_HIGHEST_RELEASE_LEVEL
#define FW_VERSION FW_VERSION_DEBUG"-L:D"
#else
#define FW_VERSION FW_VERSION_DEBUG
#endif 

//#ifdef gpio::GPIOSwitch::GPIOSwitchRestoreMode
extern gpio::GPIOSwitch *valve_open;
extern gpio::GPIOSwitch *valve_close;
//#endif


dApp dapp;

#ifdef APP_LOG
AppLog app_log;
#endif

// Mirrors dapp.upm_base for the benefit of WaterUsage
float* g_upm_base;
pulse_counter::pulse_counter_t* g_pulses_base;

///////////////////////////////////////////////////////////////////////////////////////////
dApp::dApp() {

    // Global cheat for WaterUsage objects
    g_upm_base = &upm_base_;
    g_pulses_base = &pulses_base_;
}


// Called at on_boot level 600 where sensors are setup but wifi (and mqtt) are not
void _entry_point dApp::on_boot(const char* app, int wf_report_wf_off_interval_secs, int wf_report_wf_on_interval_secs) {

    APP_LOG_ENTER("on_boot()");

    SetStatusLED(1.0, 1.0, 1.0);

    app_ = app;

    wf_->on_start_init(wf_report_wf_off_interval_secs, wf_report_wf_on_interval_secs);

    calc_max_plus_values();

    makeMqttTopics(mqtt_client->get_topic_prefix());

    SetStatusLEDBasedOnValveStatus();

    on_boot_called = true;

    APP_LOG_LOG("} on_boot(app=%s, wf_off_interval_secs=%i, wf_on_interval_secs=%i", app,
      wf_report_wf_off_interval_secs, wf_report_wf_on_interval_secs); 

    APP_LOG_EXIT("on_boot");
  }


  void dApp::makeMqttTopics(const std::string& prefix) {

    mqtt_topic_prefix_ = prefix;

    mqttTopicClosedUsageState_ = prefix + mqttTopicClosedUsageState_;
    mqttTopicStat_ = prefix + mqttTopicStat_;
    mqttTopicProp_ = prefix + mqttTopicProp_;
    mqttSensorWfOverlimitStatus_ = prefix + mqttSensorWfOverlimitStatus_;
    mqttSensorWfCurrentUsageState_ = prefix + mqttSensorWfCurrentUsageState_;
    mqttSensorWfHourlyUsageStatus_ = prefix + mqttSensorWfHourlyUsageStatus_;
    mqttSensorWfDailyUsageStatus_ = prefix + mqttSensorWfDailyUsageStatus_;
    mqttSensorWfSessionUsageState_ = prefix + mqttSensorWfSessionUsageState_;
    mqttSensorWfNamedUsageState_ = prefix + mqttSensorWfNamedUsageState_;

  }

/* 
  Properties {

    [See https://en.wikipedia.org/wiki/List_of_tz_database_time_zones ]
    "timezone": "America/Phoenix",

    [Hourly usage totals can be saved in array in device.
    Use mqtt topic "<topic-prefix>/cmnd/get_closed_periods" to have
    waterwatch send array in mqtt json message topic
    "/sensor/wf/closed_usage/state".
    Use mqtt message topic "<topic-prefix>/cmnd/clear_closed" to
    clear this array and session array.]
    "closed_periods_max": 48,

    [Waterwatch supports 'gal' for gallons and gpm and 'L' for liters and lpm]
    "unit_of_measure": "gal",

    [Maximimum water flow. When above this level
    waterwatch will send out an alarm in mqtt topic
    "sensor/wf/over_limit/status" with payload "on" or "off"]
    "water_flow_max": 1.0,

    [At or below this level is considered "no water flow".
    Normally this should be 0, but a tolerance is permitted using
    this property in case zero is not possible perhaps because of a 
    leak that is not fixed immediately.]
    "water_flow_base": 0.0,

    [Adjust device water flow and usage using this value in case
    what waterwatch thinks is a gallon or a liter is not what you. 
    measure. Examples: 1.0 is unadjusted, 1.1 is a 10% adjustment upwards,
    0.9 is a 10% adjustment downwards. For instance, if you measure
    .5 gallons and waterwatch reports 1 gallon, you would use a 
    calibrate_factor of 0.5]
    "calibrate_factor": 1.1,


    [How long in seconds does an overlimit condition have to exist 
    before an alarm is raised? The integral "reporting period" in
    waterwatch is 15 seconds. "test_period_secs" must be a multiple
    of this reporting period. The default is the reporting period
    of 15 seconds. False alarms can also be avoided using the
    "initial_surge_secs" property.  ]
    "test_period_secs": 30,

    [If pipes are empty there can sometimes be an initial 
    surge of over limit water flow as the pipes are filled. 
    This propery allows a "grace" period when water first
    starts to flow to account for this. After water is flowing
    this property is ignored and only "test_period_secs" is
    observed.]
    "initial_surge_secs": 15,

    [Waterwatch keeps track of the water usage during a continuous period 
    of irrigating which we call a session. The next three properties
    control this feature]

    [How many sessions to save before overwriting oldest]
    "closed_sessions_max": 14

    [Sessions shorter than min_secs will be thrown out]
    "min_session_secs": 420,

    [When one zone valve closes and another opens there
    may be no water flow. How long to allow for this. (in
    the future these subs zones will alse be saved)]
    "end_session_secs": 180

    [How often we report water flow in seconds. "wf_off" mode is
    when there is no water flow, wf_on mode is when there is water flow.
    A value of 0 means to report only on change of flow. 
    Defaults in the code are: wf_off = 15, wf_on = 2. There are
    defaults in the common_substitutions.yaml as well that override the 
    code. At last check they are wf_off = 180, wf_on = 2
    (TODO: would it be better to use 0 to 
    mean "never report" and a negative number to mean "on change only"?)]
    "report_period_secs": {
      "wf_off": 0,  
      "wf_on": 0   
    }

    Other items needed:
      cmmd/signature/add
      cmmd/signature/remove

      cmmd/delete_signature
      [ {
        "name": "Shower",
        "uom": "gpm",
        "ver": 1
        "segments": [
          [<upm>, <upm_allowance>, <duration_ms>, <duration_allowance_ms>],
          ...
        ]} 
  }
*/
  void _entry_point dApp::process_properties(const JsonObject& jo, bool fromRetainedProperties/*=false*/) {
    
    //APP_LOG_EMIT_ON(true);

    APP_LOG_ENTER("process_properties()");

    APP_LOG_LOG("\n\nBegin: process_properties(arg count=%i)", jo.size()); 

    if ( jo.containsKey("timezone") && jo["timezone"].is<char*>()) {
      std::string was = sntp_time->get_timezone();
      sntp_time->set_timezone(jo["timezone"]);

      // time_t now = sntp_time->timestamp_now();
      // std::string str_time_now = ESPTime::from_epoch_local(now).strftime("%Y-%m-%d %H:%M");

      // APP_LOG_LOG("timezone: specified %s, was %s now %s, time is %s, num=%f",  (const char*)jo["timezone"], 
      //   was.c_str(), sntp_time->get_timezone().c_str(), str_time_now.c_str(), (float)now); 
      APP_LOG_LOG("timezone: specified %s, was %s now %s",  (const char*)jo["timezone"], 
        was.c_str(), sntp_time->get_timezone().c_str()); 
    }

    if ( jo.containsKey("closed_periods_max") && jo["closed_periods_max"].is<int>() /*&& jo.size() == 1*/) {
      int was = hourlyWaterUsage_.get_max_closed();
      APP_LOG_LOG("closed_periods_max: specified %i, was %i now %i",  (int)jo["closed_periods_max"], was, hourlyWaterUsage_.get_max_closed()); 
      hourlyWaterUsage_.set_max_closed(jo["closed_periods_max"]);
      dailyWaterUsage_.set_max_closed(jo["closed_periods_max"]);
    }


    if ( jo.containsKey("unit_of_measure") && jo["unit_of_measure"].is<char*>()) {
      const char* was = xlate_mgr_.current->uom_text(); 
      const char* uom = jo["unit_of_measure"];

     if (xlate_mgr_.set_current(uom)) {
       convert_uom(was);
     }
     
      APP_LOG_LOG("unit_of_measure: specified %s, was %s, now %s", uom, was, xlate_mgr_.current->uom_text()); 
    }

    if ( jo.containsKey("water_flow_max") && jo["water_flow_max"].is<float>()) {
      float was = max_upm_;
      max_upm_ = (float)jo["water_flow_max"];
      calc_max_plus_values();
      APP_LOG_LOG("max_upm: was %f, now %f", was, max_upm_); 
    }

    if ( jo.containsKey("water_flow_base") && jo["water_flow_base"].is<float>()) {
      float was = upm_base_;
      upm_base_ = (float)jo["water_flow_base"];
      pulses_base_ = xlate_mgr_.current->convert_pulses_to_uom(upm_base_);
      APP_LOG_LOG("upm_base: was %f, now %f", was, upm_base_); 
    }

    if ( jo.containsKey("calibrate_factor") && jo["calibrate_factor"].is<float>()) {
      float was = xlate_mgr_.get_calibrate_factor();
      float specified = jo["calibrate_factor"];
      // Zero would cause divide by zero error
      if (specified != 0 && calibrate_factor_ != specified) {
        // New calibrate factor -- need to update translation units and
        // translate stored values
        xlate_mgr_.set_calibrate_factor(specified);
        convert_uom(nullptr, was);
      }
      APP_LOG_LOG("calibrate_factor: specified %f, was %f, now %f", specified, was, calibrate_factor_); 
    }

    if ( jo.containsKey("test_period_secs") && jo["test_period_secs"].is<int>()) {
      int was = test_period_secs_;
      test_period_secs_ = jo["test_period_secs"];
      APP_LOG_LOG("test_period_secs: was %i now %i",  was, test_period_secs_); 
    }

    if ( jo.containsKey("initial_surge_secs") && jo["initial_surge_secs"].is<int>()) {
      int was = allow_initial_surge_seconds_;
      allow_initial_surge_seconds_ = jo["initial_surge_secs"];
      APP_LOG_LOG("initial_surge_secs: was %i now %i",  was, allow_initial_surge_seconds_); 
    }

    if ( jo.containsKey("closed_sessions_max") && jo["closed_sessions_max"].is<int>() /*&& jo.size() == 1*/) {
      int was = sessionWaterUsage_.get_max_closed();
      sessionWaterUsage_.set_max_closed(jo["closed_sessions_max"]);
      APP_LOG_LOG("closed_sessions_max: specified %i, was %i now %i",  
        (int)jo["closed_sessions_max"], was, sessionWaterUsage_.get_max_closed()); 
    }

    if ( jo.containsKey("min_session_secs") && jo["min_session_secs"].is<int>()) {
      int was = sessionWaterUsage_.get_min_session_secs();
      sessionWaterUsage_.set_min_session_secs(jo["min_session_secs"]);
      APP_LOG_LOG("min_session_secs: specified %i, was %i now %i",  
        (int)jo["min_session_secs"], was, sessionWaterUsage_.get_min_session_secs()); 
    }

    if ( jo.containsKey("end_session_secs") && jo["end_session_secs"].is<int>()) {
      int was = sessionWaterUsage_.get_end_session_secs();
      sessionWaterUsage_.set_end_session_secs(jo["end_session_secs"]);
      APP_LOG_LOG("end_session_secs: specified %i, was %i now %i",  
        (int)jo["end_session_secs"], was, sessionWaterUsage_.get_end_session_secs()); 
    }

    if (jo.containsKey("allowances") && jo["allowances"].is<JsonArray>()) {
      int was = specific_allowances_.size();
      specific_allowances_ = SpecificAllowances((const JsonArray&)jo["allowances"]);
      calc_max_plus_values();
      APP_LOG_LOG("allowances count: was %i now %i",  
        was, (int)specific_allowances_.size() ); 
    }

    if (jo.containsKey("valve_open") && jo["valve_open"].is<bool>()) {
      int was = valve_is_open_;
      valve_is_open_ = jo["valve_open"];
      // If we don't have haveRetainedProperties_ then the previous state of
      // valve_is_open_ can't be assumed to be valid
      if (was != valve_is_open_ || !haveRetainedProperties_) {
        // Normally changing the valve status will call send_retained_properties()
        // Since we will be sending it later in this function we special case
        // it. The set_valve_status will reset this special case boolean. I'm
        // not going to reset it here because I don't know when the set_valve_status
        // will be called (is it syncronous?)  Is a danger that
        // set_valve_status will not be called?
        do_not_send_retained_on_next_valve_operation_ = false;
        if (valve_is_open_) {
          open_valve();
        } else {
          close_valve();
        }
      }
      APP_LOG_LOG("valve_open:  was %i now %i", was, valve_is_open_); 
    }

    // New style of parsing json using helper get..() functions
    // The getObject variety is not much of an improvement
    const JsonObject& joReport_period_secs = getObject(jo, "report_period_secs");
    if (joReport_period_secs != JsonObject::invalid()) {
      float was_wf_off = wf_->get_report_period_wf_off_mode_secs();
      float was_wf_on = wf_->get_report_period_wf_on_mode_secs();

      set_report_period_secs(joReport_period_secs);

      APP_LOG_LOG("report_period_secs:  wf_off was %f now %f, wf_on was %f now %f", 
        was_wf_off, wf_->get_report_period_wf_off_mode_secs(),
        was_wf_on, wf_->get_report_period_wf_on_mode_secs()
      ); 
    }

    const JsonArray& jaSignatures = getArray(jo, "signatures");
    if (jaSignatures != JsonArray::invalid()) {

      wf_->set_signatures_from_json(jaSignatures);

      APP_LOG_LOG("signatures:"); 
    }

    if (fromRetainedProperties) {
      haveRetainedProperties_ = true;
    }

    // Immediatly send retained message
    // This serves two purposes:
    //  1. If this is from set options cmnd the call gets feedback that
    //    the option has been received.
    //  2. Since the mqtt message is "retained" it will be resent to this 
    //    device whenever it connects to mqtt, providing persistence of 
    //    these values.

    send_retained_properties();

    APP_LOG_EXIT("process_properties"); 
    // APP_LOG_EMIT_ON(true);

  }

JsonObject& dApp::toJson(JsonObject& jo, const char* prop_name/*=nullptr*/) {


    if (prop_name == nullptr) {
      jo["fw_version"] = FW_VERSION;
    }

    if (prop_name == nullptr || strcmp(prop_name, "timezone") == 0) {
      jo["timezone"] = sntp_time->get_timezone();
    }
    if (prop_name == nullptr || strcmp(prop_name, "closed_periods_max") == 0) {
      jo["closed_periods_max"] = hourlyWaterUsage_.get_max_closed();
    }
    if (prop_name == nullptr || strcmp(prop_name, "unit_of_measure") == 0) {
      jo["unit_of_measure"] = xlate_mgr_.current->uom_text();
    }
    if (prop_name == nullptr || strcmp(prop_name, "water_flow_max") == 0) {
      jo["water_flow_max"] = max_upm_;
    }
    if (prop_name == nullptr || strcmp(prop_name, "water_flow_base") == 0) {
      jo["water_flow_base"] = upm_base_;
    }
    if (prop_name == nullptr || strcmp(prop_name, "calibrate_factor") == 0) {
      jo["calibrate_factor"] = calibrate_factor_;
    }
    if (prop_name == nullptr || strcmp(prop_name, "test_period_secs") == 0) {
      jo["test_period_secs"] = test_period_secs_;
    }
    if (prop_name == nullptr || strcmp(prop_name, "initial_surge_secs") == 0) {
      jo["initial_surge_secs"] = allow_initial_surge_seconds_;
    }
    if (prop_name == nullptr || strcmp(prop_name, "closed_sessions_max") == 0) {
      jo["closed_sessions_max"] = sessionWaterUsage_.get_max_closed();
    }
    if (prop_name == nullptr || strcmp(prop_name, "end_session_secs") == 0) {
      jo["end_session_secs"] = sessionWaterUsage_.get_end_session_secs();
    }
    if (prop_name == nullptr || strcmp(prop_name, "min_session_secs") == 0) {
      jo["min_session_secs"] = sessionWaterUsage_.get_min_session_secs();
    }
    if (prop_name == nullptr || strcmp(prop_name, "min_session_secs") == 0) {
      jo["min_session_secs"] = sessionWaterUsage_.get_min_session_secs();
    }

    if (prop_name == nullptr || strcmp(prop_name, "valve_open") == 0) {
        jo["valve_open"] = valve_is_open_;
    }

    if (prop_name == nullptr || strcmp(prop_name, "report_period_secs") == 0) {

        JsonObject& joReportPeriodSecs = global_json_buffer.createObject();

        joReportPeriodSecs["wf_off"] = wf_->get_report_period_wf_off_mode_secs();
        joReportPeriodSecs["wf_on"] = wf_->get_report_period_wf_on_mode_secs();

        jo["report_period_secs"] = joReportPeriodSecs;
    }

    if (prop_name == nullptr || strcmp(prop_name, "signatures") == 0) {
        jo["signatures"] = wf_->get_signatures_as_json();
    }


    return jo;
}

void _entry_point dApp::send_retained_properties() {
    APP_LOG_ENTER("send_retained_properties()");

    // Send retained message
    mqtt_client->publish_json(mqttTopicStat_, [=](JsonObject &root) {
      toJson(root);
    }, 1, true);

    APP_LOG_EXIT("send_retained_properties");
}

void dApp::send_property(const char* prop_name) {
  APP_LOG_LOG("send_property");

  // Send non-retained message
  mqtt_client->publish_json(mqttTopicProp_, [=](JsonObject &root) {
    toJson(root, prop_name);
  }, 1, false);

}

void _entry_point dApp::process_stat(const JsonObject& x) {
  APP_LOG_ENTER("process_stat()");

  APP_LOG_LOG(haveRetainedProperties_ ? "ignored" : "processed");

  if (!haveRetainedProperties_) {
    process_properties(x, true);
  }

  APP_LOG_EXIT("process_stat");
}


void _entry_point dApp::reset() {
  APP_LOG_ENTER("reset()");
  APP_LOG_EXIT("reset");
}

  // Called by esphome every "report_interval"
  // The value is in upm (units per minute). This would be in either gallons per minute or
  // liters per minute. Esphome natively returns pulses per minute, which we convert to
  // units per minute in a esphome sensor filter prior to esphome calling this function.
  // Note that whatever report_interval we spcifiy for this sensor, the value is always a
  // "per minute" value. I assume this means that esphome counts the pulses during the
  // interval period then factors it for that value over a minute. So if "report_interval" ==
  // 5s, and the sensor gets 1000 pulses over the 5s period then esphome returns 1000 * 20, or
  // 200000. 
void _entry_point dApp::process_wf_on_value(float upm) {

    #ifdef APP_LOG
    if (mqtt_client && mqtt_client->is_connected()) {
      APP_LOG_EMIT_ON(true);
    }
    #endif

    APP_LOG_ENTER("process_wf_on_value(upm=%f)", upm);

    if (!on_boot_called) {
      APP_LOG_LOG("!on_boot_called");
      APP_LOG_EXIT("process_wf_on_value");
      return;
    }

    APP_LOG_LOG("initialized=%i, time_is_valid=%i, gotStat=%i, valve_close=%i, valve_open=%i", 
      on_boot_called, time_is_valid_, haveRetainedProperties_
      , valve_close->state, valve_open->state);

    // Make sure we have a valid time from time server
    // time_is_valid_: -1: not tested yet, 0: tested not valid, 1: tested is valid
    if (time_is_valid_ != 1) {
      // Not yet tested or tested not valid 
      APP_LOG_LOG("Testing if time is valid");
      auto time = sntp_time->now();
      if ( !time.is_valid()) {
        APP_LOG_LOG("NOT time.is_valid()");
        time_is_valid_ = 0;
      } else {
        APP_LOG_LOG("time.is_valid()");
        time_is_valid_ = 1;
      }
    }

    // Until time is valid we don't process anything
    // TODO: notify user if time not valid
    if (time_is_valid_ == 1) {
      
      // Sometimes I see infinitesimal usage amounts (eg, 7e-41), so I ignore and
      if (upm < 0.00000001 ) upm = 0.0;


      int report_period_secs = wf_->get_last_report_period_secs();
    
      refresh_display(upm);

      // We have flow and we have a surge grace period
      if (upm > upm_base_ && grace_for_surge_ > 0) {
        // Yes, we don't care of we are over limit or not.
        // we are in a surge grace period
        // Decrement grace surge period
        grace_for_surge_ -= report_period_secs;
        
      } else if (max_upm_plus_ >= 0.0 && upm > max_upm_plus_) {

          // We have flow and are over limit

        secs_over_limit_ += report_period_secs;

        if (secs_over_limit_ >= test_period_secs_) {
          over_limit_ = true;
          mqtt_client->publish(mqttSensorWfOverlimitStatus_, "on", 2, 2);

          // If we are a wwh device then close master valve immediately
          if (app_ == "wwh") {
            close_valve();
          }

          APP_LOG_LOG("over limit: max_upm=%f, max_upm_plus=%f,  wf=%f", 
            max_upm_, max_upm_plus_, upm);
        }
      } else {

        // We are not over limit
      
        secs_over_limit_ = 0;

        // were we over limit before?
        if (over_limit_) {
          over_limit_ = false;
          mqtt_client->publish(mqttSensorWfOverlimitStatus_, "off", 3, 2);
          APP_LOG_LOG("back under limit: max_upm=%f, max_upm_plus=%f, upm=%f", 
            max_upm_, max_upm_plus_, upm);
        }

          if (upm <= upm_base_) {
            grace_for_surge_ = allow_initial_surge_seconds_;
        }
      }

      // upm is for a minute, but we are updating every "report_period_secs", so we need
      // to factor usage 
      float usage = upm * report_period_secs / 60.0; 
      hourlyWaterUsage_.addUsage(usage);
      dailyWaterUsage_.addUsage(usage);
      currentWaterUsage.addUsage(usage);
      
      // We only add to session usage if we do not have a named usage in process
      if (namedWaterUsage_.count() == 0 && sessionWaterUsage_.addUsage(usage)) {
        mqtt_client->publish_json(mqttSensorWfSessionUsageState_, [=](JsonObject &root) { 
          sessionWaterUsage_.getLastClosed().toJson(&root);
          });

      }

      //if (app_ == "wwh") {
        namedWaterUsage_.addUsage(usage);
      //}

      secs_since_last_publish_ += report_period_secs;
      if (secs_since_last_publish_ >=  publish_usage_secs_) {
        // Publish usage 
        mqtt_client->publish_json(mqttSensorWfCurrentUsageState_, [=](JsonObject &root) { 
          currentWaterUsage.toJson(&root);
          currentWaterUsage.init();
          });

        secs_since_last_publish_ = 0;
      }
    } // if (time_is_valid_ == 1)

    APP_LOG_EXIT("process_wf_on_value");

}

void _entry_point dApp::on_new_hour() {

    APP_LOG_ENTER("on_new_hour()");

    hourlyWaterUsage_.next();

    mqtt_client->publish_json(mqttSensorWfHourlyUsageStatus_, [=](JsonObject &root) { 
      hourlyWaterUsage_.getLastClosed().toJson(&root);
      });

    while (namedWaterUsage_.purgeFirstExpired());

    calc_max_plus_values();

    APP_LOG_EXIT("on_new_hour");
}

void _entry_point dApp::on_new_day() {

  APP_LOG_ENTER("on_new_day()");

    dailyWaterUsage_.next();

    mqtt_client->publish_json(mqttSensorWfDailyUsageStatus_, [=](JsonObject &root) { 
      dailyWaterUsage_.getLastClosed().toJson(&root);
      });

  APP_LOG_EXIT("on_new_day");
}


void _entry_point dApp::add_allowance(const JsonObject& jo) {
    // {  name: "washing machine",
    //    upm: 1.1,
    //    usage: -1,
    //    expire_secs: 5400
    //  }
    APP_LOG_ENTER("add_allowance");

    std::string name(getString(jo, "name", ""));
    float upm(getFloat(jo, "upm", 0));
    float usage(getFloat(jo, "usage", 0));
    // default exprire time 2 hour
    int expire_secs(getInt(jo, "expire_secs", 7200));
    bool create_named_session = getBool(jo, "create_named_session", false);
  
    if (!name.empty()) {

      time_t expire_time = sntp_time->now().timestamp + (expire_secs * 1000);
      specific_allowances_.add_item(name, upm, usage, expire_time, create_named_session);

      if (create_named_session) {
        namedWaterUsage_.add_usage_unit(name, expire_time);
        APP_LOG_LOG("add named usage { name: %s }", name.c_str());
      }

      calc_max_plus_values();
      
      APP_LOG_LOG("add allowance{ name: %s, upm: %f, usage: %f, expire_secs: %i, create_named_session: %i }",  
        name.c_str(), upm, usage, expire_secs, create_named_session);

    } else {
      APP_LOG_LOG("add allowance called but NO NAME specified");
    }

    APP_LOG_EXIT("add_allowance");
}

void _entry_point dApp::delete_allowance(const JsonObject& jo) {
  // {  name: "washing machine" }

  APP_LOG_ENTER("delete_allowance()");

  std::string name(getString(jo, "name", ""));
  bool cancel = getBool(jo, "cancel", false);
  bool create_named_session = false;
  bool rc = false;

  if (!name.empty()) {
    SpecificAllowance* sa = specific_allowances_.get(name);
    if (sa) {
      create_named_session = sa->create_named_session;
      specific_allowances_.delete_item(name);

      if (create_named_session) {
        delete_named_usage(name, cancel);
      }

      rc = true;
    }

    // So how do know if we auto created a matching named session?

    calc_max_plus_values();
  }

  if (rc) {
    APP_LOG_LOG("delete allowance{ name: %s }", name.c_str());
  } else {
    APP_LOG_LOG("delete allowance called but name %s not found!", name.c_str());
  }

  APP_LOG_EXIT("delete_allowance");
}


void _entry_point dApp::add_named_usage(const JsonObject& jo) {
    APP_LOG_ENTER("add_named_usage()");

    std::string name(getString(jo, "name", ""));
    // default exprire time 2 hour
    int expire_secs(getInt(jo, "expire_secs", 7200));
    
    if (!name.empty()) {
      // No sessions during named
      sessionWaterUsage_.clearCurrent();
      namedWaterUsage_.add_usage_unit(name, 
        sntp_time->now().timestamp + (expire_secs * 1000));
      
      APP_LOG_LOG("add named usage { name: %s }", name.c_str());
      } else {
        APP_LOG_LOG("add_named_usage called but NO NAME specified");
    }

    APP_LOG_EXIT("add_named_usage");
}

void _entry_point dApp::delete_named_usage(const JsonObject& jo) {
  APP_LOG_ENTER("delete_named_usage()");

  delete_named_usage(getString(jo, "name", ""), getBool(jo, "cancel", false));

  APP_LOG_EXIT("delete_named_usage");
}

void dApp::delete_named_usage(const std::string name, bool cancel) {

  if (!name.empty()) {
    if (namedWaterUsage_.delete_usage_unit(name) && !cancel) {
      mqtt_client->publish_json(mqttSensorWfNamedUsageState_, [=](JsonObject &root) { 
        namedWaterUsage_.getLastClosed().toJson(&root);
        });
    }
    
    //APP_LOG_LOG("delete named usage{ name: %s, cancel: %i }", name.c_str(), cancel);
    APP_LOG_LOG("delete named usage{ name: %s, cancel: %i }", name.c_str(), cancel);
    } else {
      APP_LOG_LOG("delete named usage called but NO NAME specified");
  }
}

void _entry_point dApp::set_report_period_secs(const JsonObject& jo) {
    APP_LOG_ENTER("set_report_period_secs()");

    int wf_off(getFloat(jo, "wf_off", -1));
    int wf_on(getFloat(jo, "wf_on", -1));

    if (wf_off >= 0) {
      wf_->set_report_period_wf_off_mode_secs(wf_off);
    } 
    
    if (wf_on >= 0) {
      wf_->set_report_period_wf_on_mode_secs(wf_on);
    }

    APP_LOG_LOG("set_report_period_secs { wf_off: %i, wf_on: %i }", wf_off, wf_on);

    send_retained_properties();

    APP_LOG_EXIT("set_report_period_secs");

}



void _entry_point dApp::get_closed() {
    APP_LOG_ENTER("get_closed()");

    mqtt_client->publish_json(mqttTopicClosedUsageState_, [=](JsonObject &root) { 
      root["hourly"] = hourlyWaterUsage_.toJson();
      root["daily"] = dailyWaterUsage_.toJson();
      root["sessions"] = sessionWaterUsage_.toJson();
      if (app_ == "wwh") {
        root["named"] = namedWaterUsage_.toJson();
      }
      });

    APP_LOG_EXIT("get_closed");
}

  void _entry_point dApp::clear_closed() {
    APP_LOG_ENTER("clear_closed()");

    hourlyWaterUsage_.clearClosed();
      dailyWaterUsage_.clearClosed();
      sessionWaterUsage_.clearClosed();
      namedWaterUsage_.clearClosed();

      mqtt_client->publish_json(mqttTopicClosedUsageState_, [=](JsonObject &root) { 
        root["hourly"] = hourlyWaterUsage_.toJson();
        root["daily"] = dailyWaterUsage_.toJson();
        root["sessions"] = sessionWaterUsage_.toJson();
        //if (app_ == "wwh") {
          root["named"] = namedWaterUsage_.toJson();
        //}
        });

    APP_LOG_EXIT("clear_closed");

  }

  // We receive sensor native pulses per period and return units per period (gals or liters)
  // In esphome that period is always a minute, but the math is the same no matter the
  // period length. 
  // float _entry_point dApp::process_pulse_counter(float pulses) {
  //     return xlate_->convert_pulses_to_uom(pulses); 
  // }

  // WWH functions
void _entry_point dApp::set_valve_status(bool open) {
  APP_LOG_ENTER("set_valve_status()"/*, to_string(open).c_str()*/);

  valve_is_open_ = open;
  // We can get calls to this prior to on_boot()
  if (on_boot_called) {
    SetStatusLEDBasedOnValveStatus();
    if (do_not_send_retained_on_next_valve_operation_) {
      do_not_send_retained_on_next_valve_operation_ = false;
    } else {
      send_retained_properties();
    }
  }
  APP_LOG_EXIT("set_valve_status");
}

void dApp::open_valve() {
  APP_LOG_ENTER("open_valve()");
  // #ifdef valve_open
  valve_open->turn_on();
  // #endif
  APP_LOG_EXIT("open_valve");
}

void dApp::close_valve() {
  APP_LOG_ENTER("close_valve()");
  // #ifdef valve_open
  valve_close->turn_on();
  // #endif
  APP_LOG_EXIT("close_valve");
}

void _entry_point dApp::toggle_valve() {
  APP_LOG_ENTER("toggle_valve()");

  // #ifdef valve_close
  if (valve_is_open_) {
    valve_close->turn_on();
  } else {
    valve_open->turn_on();
  }
  // #endif
  APP_LOG_EXIT("toggle_valve");
}

void dApp::refresh_display(float pulses) {
  APP_LOG_LOG("refresh_display()");

  ssd1306_i2c_i2cssd1306->set_writer([=](display::DisplayBuffer & it) -> void {
        it.printf(0, 8, fontOpenSans, "WF: %.2f", pulses);
        if (over_limit_) {
          it.print(0, 30, fontOpenSans, "OVER LIMIT!");
        } else if (max_upm_plus_ < 0) {
          it.printf(0, 40, fontOpenSans, "No Max!");
        } else {
          it.printf(0, 40, fontOpenSans, "Max: %.2f", max_upm_plus_);
        }
    });
}

void dApp::SetStatusLEDBasedOnValveStatus() const {     
    if (valve_is_open_) {
      SetStatusLED(0.0, 1.0, 0.0);
    } else {
      SetStatusLED(1.0, 0.0, 0.0);
    }
}

  void dApp::SetStatusLED(float r, float g, float b) const {            
    #ifdef _status_led
    // auto call = id(_status_led).turn_on();
    auto call = _status_led->turn_on();
    call.set_brightness(0.33);
    call.set_rgb(r, g, b);
    call.perform();
    #endif
  }
