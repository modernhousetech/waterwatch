#pragma once

#include "esphome.h"
#include "helper.h"
#include <limits>

using namespace json;

// How will signatures work?
// Multiple signatures will run simultaneously.
// They have segments or stages.
// Can we have multiple segments going on at the same time?
// A signature should describe the water flow for a specific event.
//      An event would be things like a toilet flush or a shower.
//      A toilet flush would be something like continuous water 
//      flow + n gals/min for n number of seconds.
//      A shower would be vaguer, something like + n wf for
//      between n secs and nn secs. Do we need to account for 
//      wf stoppage, for when a user stops the shower when they
//      temporarily don't need water? 
// A Signature will end in a match or no match
// Can a match invalidate other running signatures?
//      For instance, n + wf could start many signatures.
//      Lets say: toilet flush, and shower.
//      When the wf stops at a certain timespan we
//      have match for toilet flush. Shower will 
//      naturely end with a no match.
//      What if we want to track a full toilet flush and a 
//      a short (#1) flush. When water flow starts they both 
//      run. When it goes long then the short signature will
//      end with a no match. The full flush signature could then
//      match or not. So far no conflicts, and no need to invalidate
//      other running signatures.
// Low water pressure could be a problem. 
//      What happens when someone fills a bath tub. This could be
//      at full house pressure. When someone then flushes a
//      toilet, both pressures will drop. How can a signature
//      account for this?
// A signature should have an associated action. 
// Two actions: report, close house valve and report  
// Is there a signature for a broken pipe? In a house with
// low pressure, how would you differentiate a full broken
// pipe with filling a bath tub?

class Signature {
    protected:
    class Segment {
        float upm_min_;
        float upm_max_;
        float upm_allowance_;
        float duration_min_secs_;
        float duration_max_secs_;

        int secs_;
        bool blocked_ = false;

        unsigned int    flags_ = 0;

        public:

        enum {
            next_flag_value =   1
        };

        public:
        Segment() {};
        Segment(float upm, float upm_allowance, 
            float duration_secs, float duration_allowance) {
            init(upm, upm_allowance, duration_secs, duration_allowance);
        }

        void init(float upm, float upm_allowance, 
            float duration_secs, float duration_allowance) {
         
            upm_min_ = upm;
            upm_allowance_ = upm_allowance;
            upm_max_ = upm_allowance_ != 0
                ? upm_min_ + upm_allowance_
                :  std::numeric_limits<float>::max();

            duration_min_secs_ = duration_secs;
            duration_max_secs_ = duration_min_secs_ + duration_allowance;

        }

        bool is_match(float upm, float secs) {
            bool ret = false;

            if (!blocked_ && upm >= upm_min_ && upm < upm_max_) {
                secs_ += secs;
                if (secs > duration_max_secs_) {
                    reset();
                    blocked_ = true;
                } 
            } else if (upm < upm_min_) {
                if (secs_ >= duration_min_secs_) {
                    ret = true;
                }
                reset();
                blocked_ = false;
            }

            return ret;
        }

        void reset() {
            secs_ = 0;
        }
    
        void convert_uom(/*std::function<float(float &)>f*/) {
            //usage = f(usage);
        }

        // flags test and set
        bool is(unsigned int flag) const {
            return flags_ & flag;
        }
        void set(unsigned int flag) {
            flags_ |= flag;
        }
        void unset(unsigned int flag) {
            flags_ &= ~flag;
        }

        // It deserves a json object but we use a json array because it 
        // serializes much more compactly.
        JsonArray& toJson() const {
            JsonArray& ja = global_json_buffer.createArray();
            ja.add(upm_min_);
            ja.add(upm_allowance_);
            ja.add(duration_min_secs_);
            ja.add(duration_max_secs_ - duration_min_secs_);

            return ja;
        }

        void fromJson(const JsonArray& ja) {
            init(ja[0], ja[1], ja[2], ja[3]);
        }

        
    }; // end Segment

    TranslationManager& xlate_mgr_;
    std::string name;
    std::string uom = "gal";
    float ver = 1.0;
    Segment*    segments_ = nullptr;
    int         segment_count_ = 0;
    int         segment_index = 0;

    unsigned int    flags_ = 0;

    public:

    enum {
        built_in =          1,
        next_flag_value =   built_in << 1
    };

    public:
    enum ReportLevel {
        nothing,  
        report,     // Default
        alert,      // Elevated report   
        alarm       // Turns off valve, Elavated report,
                    // ignores "do not disturb" status
    };

    ReportLevel report_level = ReportLevel::report;

    public:
    Signature(int segment_count, const char* _name, TranslationManager& xlate_mgr,
        int flags=0):
        xlate_mgr_(xlate_mgr),
        name(_name),
        flags_(flags) {
        segment_count_ = segment_count;
        segments_ = new Segment[segment_count_];
    }

    // Construct from json 
    Signature(const JsonObject& jo, TranslationManager& xlate_mgr):
        xlate_mgr_(xlate_mgr),
        name(getString(jo, "name", "unnamed")),
        uom(getString(jo, "uom", "gal")),
        ver(getFloat(jo, "ver", ver)) { 

        const char* reportLevel = getString(jo, "level", "");
        if (strcmp(reportLevel, "nothing") == 0) {
            report_level = ReportLevel::nothing;
        } else if (strcmp(reportLevel, "alert") == 0) {
            report_level = ReportLevel::alert;
        } else if (strcmp(reportLevel, "alarm") == 0) {
            report_level = ReportLevel::alarm;
        } else {
            report_level = ReportLevel::report;
        }

        JsonArray& ja = jo["segments"];
        segment_count_ = ja.size();
        int i = 0;
        segments_ = new Segment[segment_count_];
        for (JsonVariant value : ja) {
            segments_[i++].fromJson(value);
        }
        // The segments are now loaded, but the water flow values
        // are in gal or L. We need them in native pulses
        Segment* segment = segments_;
        for (i = 0; i < segment_count_; ++i, ++segment) {
            segment->convert_uom();
        }
    }

    ~Signature() {
        delete[] segments_;
    }

    bool is_match(float lpm, float secs) {
        bool ret = false;

        if (segments_[segment_index].is_match(lpm, secs)) {
            if (++segment_index >= segment_count_) {
                ret = true;
                segment_index = 0;
            }
        }
        return ret;
    }

    // flags test and set
    bool is(unsigned int flag) const {
        return flags_ & flag;
    }
    void set(unsigned int flag) {
        flags_ |= flag;
    }
    void unset(unsigned int flag) {
        flags_ &= ~flag;
    }

    JsonObject& toJson() const {
        JsonObject& jo = global_json_buffer.createObject();
        jo["name"] = name;
        jo["uom"] = uom;
        jo["ver"] = ver;
        JsonArray& ja = global_json_buffer.createArray();
        int i = 0;
        for (Segment* segment = segments_; i < segment_count_; ++i, ++segment) {
            ja.add(segment->toJson());
        }

        return jo;
    }
};

// class SignatureToiletShortFlush: public Signature {
//     SignatureToiletShortFlush(TranslationManager& xlate_mgr):
//         Signature(1, "Toilet short flush", xlate_mgr) {
//         //segments.push_back
//         segments_[0] = Segment(1, 2, 3, 4);
//     }
// };

// Built-in signatures

// Over limit
// What we want
// When no one is home we should only expect 
//      irrigation
//      What else?
//          Automatic pet water feeders?
//          Perhaps (other than irrigation), any sustained (how 
//          long?) water flow. Short water flow is OK. 
//
//      How do we 
class SignatureOverLimit: public Signature {
    public:
    SignatureOverLimit(TranslationManager& xlate_mgr, int flags):
        Signature(1, "Over limit", xlate_mgr, flags) {
        report_level = ReportLevel::alarm;
        segments_[0] = Segment(1, 2, 3, 4);
    }

};


class SignatureManager: public std::vector<Signature*> {
    TranslationManager& xlate_mgr_;
    public:
    SignatureManager(TranslationManager& xlate_mgr):
        xlate_mgr_(xlate_mgr) {

        // Add built-ins
        push_back(new SignatureOverLimit(xlate_mgr_, Signature::built_in));

    }
    ~SignatureManager() {
        for (Signature* signature: *this ) {
            delete signature;
        }; 
    }

    std::vector<std::reference_wrapper<Signature>> is_match(float upm, float secs) {

        std::vector<std::reference_wrapper<Signature>> ret;

        for (Signature* signature: *this ) {
            if (signature->is_match(upm, secs)) {
                ret.push_back(*signature);
            }
        }; 

        return ret;
    }

    void convert_uom(const char* from, float old_calibrate_factor=0) {
        if (old_calibrate_factor) {
            for (Signature* signature: *this ) {
            };
        }
    }

    JsonArray& toJson() const {
        JsonArray& ja = global_json_buffer.createArray();
        for (Signature* signature: *this ) {
            ja.add(signature->toJson());
        };
        return ja;
    }

    bool fromJson(const JsonArray& ja) {
        for (const JsonObject& jo : ja) {
            push_back(new Signature(jo, xlate_mgr_));
        }

        return true;
}

};


// class SignatureToiletShortFlush: public Signature {
//     SignatureToiletShortFlush(TranslationManager& xlate_mgr):
//         Signature(1, "Toilet short flush", xlate_mgr) {
//         //segments.push_back
//         push_back(new Segment(1, 2, 3, 4));
//     }

// };



