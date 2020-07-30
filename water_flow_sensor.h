#pragma once

#include "esphome.h"
#include "pulse_counter_sensor.h"
using namespace esphome;
using namespace sensor;
using namespace pulse_counter;
#include "translation_unit.h"
#include "signature.h"

// This is the units per minute water flow value when presumably there is no water flow.
// If the plumbing system is working correctly this should be zero.
extern pulse_counter::pulse_counter_t* g_pulses_base;



class WaterflowSensor: public pulse_counter::PulseCounterSensor {

    // wf_off mode or wf_on mode. 
    // wf_off mode is when water flow <= 0 and wf_on mode is when water flow > 0

    bool in_wf_on_mode_ = false;

    int pin_;
    TranslationManager& xlate_mgr_;
    SignatureManager signature_mgr_;

    static constexpr float report_period_wf_off_mode_secs_default_ = 15.0f;
    static constexpr float report_period_wf_on_mode_secs_default_ = 2.0f;


    // We get updates every <update_interval_default> but report at
    // schedule below when we are in wf_off mode
    float report_period_wf_off_mode_secs_ = report_period_wf_off_mode_secs_default_; 
    float report_period_wf_on_mode_secs_ = report_period_wf_on_mode_secs_default_; 

    static constexpr float update_interval_secs_default_ = 1.0f;

    float update_interval_secs_ = update_interval_secs_default_; 

    struct ReportPeriod {

        pulse_counter_t pulses_total_ = 0.0f;
        int secs_ = 0;
        int last_report_period_secs_ = 0.0f;
        bool report_only_on_change = false;
        
        // Special to report on next add call
        // Currently this is used to get an initial
        // report as soon as possible on start-up so
        // user can see something when we start.
        bool report_on_next_add = false;

        static const int wf_is_changed_delta_ = 10;

        int report_period_secs_;

        pulse_counter_t last_pulses_ = -1;

        ReportPeriod(int report_period_secs):
            report_period_secs_(report_period_secs) {
            reset();
        }

        void set_report_period_secs(int report_period_secs) {
            report_period_secs_ = report_period_secs;
        }

        int get_report_period_secs() const {
            return report_period_secs_;
        }

        int get_last_report_period_secs() const {
            return last_report_period_secs_;
        }

        bool is_report_only_on_change() const { return report_period_secs_ == 0; }

        bool add(pulse_counter_t pulses, int secs) {
            pulses_total_ += pulses;
            secs_ += secs;
            bool change_of_pulses = abs(last_pulses_ - pulses) > wf_is_changed_delta_;
            last_pulses_ = pulses;

            // ESP_LOGD("main", "report_period.add(%i) pulses_total=%i, secs total=%i"
            //     ", last_pulses=%i, change=%i, report_on_change=%i"
            //     ", report_period_secs=%i", 
            //     pulses, pulses_total_, secs_ , last_pulses_,  change_of_pulses,
            //     is_report_only_on_change(), report_period_secs_);  

            bool report = (is_report_only_on_change() && change_of_pulses) 
                || secs_ >= report_period_secs_ || report_on_next_add;
            report_on_next_add = false;

            return report;
        }

        float get_value_as_pulses_per_minute() {

            return (60.0f * pulses_total_) / float(secs_);
        }

        void reset() {
            pulses_total_ = 0;
            last_report_period_secs_ = secs_;
            secs_ = 0;
            last_pulses_ = -1;
        }
    } report_period_;

    public:

    WaterflowSensor(int pin, TranslationManager& xlate_mgr):
        pin_(pin),
        xlate_mgr_(xlate_mgr),
        signature_mgr_(xlate_mgr_),
        report_period_(report_period_wf_off_mode_secs_) {

        set_name("wf");
        set_unit_of_measurement("pulses/min");
        set_icon("mdi:pulse");
        set_accuracy_decimals(2);
        set_force_update(false);
    }

    void on_start_init( int report_period_wf_off_mode_secs,
                        int report_period_wf_on_mode_secs ) {

        ESP_LOGD("main", "on_start_init(%i, %i)",  
          report_period_wf_off_mode_secs, report_period_wf_on_mode_secs);
        set_pin(new GPIOPin(pin_, INPUT, false));

        set_report_period_wf_off_mode_secs(report_period_wf_off_mode_secs);
        set_report_period_wf_on_mode_secs(report_period_wf_on_mode_secs);
        report_period_.set_report_period_secs(report_period_wf_off_mode_secs_);
        // We want to report (publish) initially as soon as possible so
        // user can see something.
        report_period_.report_on_next_add = true;

        set_update_interval_secs(1);
        
        set_in_wf_on_mode(false);

        set_rising_edge_mode(pulse_counter::PULSE_COUNTER_INCREMENT);
        set_falling_edge_mode(pulse_counter::PULSE_COUNTER_DISABLE);
        set_filter_us(13);
    }

    // Modes:
    //      Normal
    //      Fast
    //      on change
    //      never on zero (=< base flow)
    void update() override {
        pulse_counter_t pulses = this->storage_.read_raw_value();

        if (report_period_.add(pulses, update_interval_secs_)) {
            float value = xlate_mgr_.current->convert_pulses_to_uom(report_period_.get_value_as_pulses_per_minute());
            // reset() below will save the secs of this period which
            // the app will retrieve using get_last_period_secs(). This is a
            // bit of  kludge which we use because we can't, as far as I know,
            // send more information when we publish_state(). TODO: we could easily use
            // an alternative to publish_state() to get this data to the app.
            report_period_.reset();
            this->publish_state(value);
        }

        //float value = (60000.0f * raw) / float(this->get_update_interval());  // per minute
        //value = xlate_->convert_pulses_to_uom(value);

        //if (!only_publish_on_change || )
        if (pulses > *g_pulses_base && !in_wf_on_mode()) {
            set_in_wf_on_mode(true);
        } else if (pulses <= *g_pulses_base && in_wf_on_mode()) {
            set_in_wf_on_mode(false);
        }

        // ESP_LOGD("main", "pulse counter: %i pulses,wf_on_mode(%i), rps(%i)", 
        //   pulses, in_wf_on_mode(), report_period_.get_report_period_secs());
        
    }

    void convert_uom(const char* from, float old_calibrate_factor=0) {
        signature_mgr_.convert_uom(from, old_calibrate_factor);
    }

    int get_report_period_secs() const {
        return report_period_.get_report_period_secs();
    }

    int get_last_report_period_secs() const {
        return report_period_.get_last_report_period_secs();
    }

    void set_report_period_wf_off_mode_secs(float secs) {
        report_period_wf_off_mode_secs_ = secs; 
        if (!in_wf_on_mode()) {
            report_period_.set_report_period_secs(report_period_wf_off_mode_secs_);
        }
    }

    float get_report_period_wf_off_mode_secs() const {
        return report_period_wf_off_mode_secs_; 
    }

    void set_report_period_wf_on_mode_secs(float secs) {
        report_period_wf_on_mode_secs_ = secs; 
        if (in_wf_on_mode()) {
            report_period_.set_report_period_secs(report_period_wf_on_mode_secs_);
        }
    }

    float get_report_period_wf_on_mode_secs() const {
        return report_period_wf_on_mode_secs_; 
    }


    bool in_wf_on_mode() const { return in_wf_on_mode_; }

    void set_in_wf_on_mode(bool in_wf_on_mode) {
        if (in_wf_on_mode != in_wf_on_mode_) {
            in_wf_on_mode_ = in_wf_on_mode;
            report_period_.set_report_period_secs(!in_wf_on_mode
                ? report_period_wf_off_mode_secs_
                : report_period_wf_on_mode_secs_);
        ESP_LOGD("main", "set_in_wf_on_mode(%i), secs(%i)", in_wf_on_mode, report_period_.get_report_period_secs());
        }
    }

    JsonArray& get_signatures_as_json() {
        return signature_mgr_.toJson();
    }

    bool set_signatures_from_json(const JsonArray& ja) {
        return signature_mgr_.fromJson(ja);
    }

    protected:
    void set_update_interval_secs(float secs) {
        update_interval_secs_ = secs;
        set_update_interval(update_interval_secs_ * 1000);
    }

};
