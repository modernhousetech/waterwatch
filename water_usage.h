// Copyright 2020 Brenton Olander
#pragma once

#include "esphome.h"
#include "esphome/components/time/real_time_clock.h"
#include "app_defs.h"
using namespace esphome;


// #define DEBUG_SESSION

// This is the water flow when presumably there is no water flow.
extern float* g_upm_base;

////////////////////
// Water usage timed unit definition
//      This defines data that describes water usage over a
//      specific time period.

class WaterUsageTimed {
    public:
    time_t          start_time;
    int             seconds;
    float           usage;
    unsigned int    flags;
    std::string     name;

    public:

    enum {
        start_on_first_usage =      1,
        next_flag_value =           start_on_first_usage << 1
    };

    WaterUsageTimed(unsigned int _flags=0):
        start_time(0),
        seconds(0),
        usage(0.0),
        flags(_flags)
    {
    }

    void init() {
        start_time = 0;
        usage = 0.0;
        seconds = 0;
        name.clear();
       if (!is(start_on_first_usage)) {
           start();
       }
    }

    bool is(unsigned int flag) const {
        return flags & flag;
    }

    void set(unsigned int flag) {
        flags |= flag;
    }

    void unset(unsigned int flag) {
        flags &= ~flag;
    }

    void setName(const std::string& _name) {
        name = _name;
    }

    const std::string& getName() const {
        return name;
    }

    void start(unsigned int _flags=0) {
        start_time = sntp_time->timestamp_now();
        set(_flags);
    }

    bool isStarted() const {
        return start_time != 0;
    }

    // bool isOpen() const {
    //     return start_time == 0 && seconds == 0;
    // }

    time_t timeSoFar(time_t endtime=0) {
        return (endtime ? endtime : sntp_time->timestamp_now()) - start_time;
    }

    void close(time_t endtime=0) {
        seconds = (endtime ? endtime : sntp_time->timestamp_now()) - start_time;
    }
    void addUsage(float usage) {

        if (!isStarted()) {
            start();
        }
        this->usage += usage;
    }

    float getUsage() const { return usage; }

    void convert_uom(std::function<float(float &)>f) {
        usage = f(usage);
    }

    JsonObject& toJson(JsonObject* pjo=nullptr) const {
        //JsonBuffer jb;
        if (pjo == nullptr) {
            // JsonObject& jo = global_json_buffer.createObject();
            pjo = &global_json_buffer.createObject();
        }

        if (!name.empty()) {
            (*pjo)["name"] = name;
        }
        // Sometimes I see infinitesimal usage amounts (eg, 7e-41), so I ignore 
        // Note: infinitesimal amounts also handled in dapp.cpp so maybe not needed here.
        (*pjo)["usage"] = usage > 0.00000001 ? usage : 0.0;
        (*pjo)["start_time"] = time::ESPTime::from_epoch_local(start_time).strftime("%Y-%m-%d %H:%M");
        (*pjo)["start_timestamp"] = start_time;
        (*pjo)["tz"] = sntp_time->get_timezone();
     
        // timed usage is closed when seconds is not zero
        int _seconds = seconds ? seconds : sntp_time->timestamp_now() - start_time;

        (*pjo)["duration_seconds"] = _seconds;
        //jo["duration"] = strftime(buf, sizeof buf, "%T", _seconds);

        return *pjo;
    }

};


///////////////
// A class to manage a collection of water usage timed units.
//
// The list is a simple array of WaterUsageTimed objects.
// There is a current unit in the list at indexCurrent.
// When a the current unit is over it is "closed" and
// the indexCurrent moves to the next slot in the array.
// If the maximum slot is used the current index wraps
// to the start and overwrites what was there.
//
// Before wrapping the current unit will be the last used
// slot and before it the closed units. So indexCurrent - 1
// is the most recent closed period, indexCurrent - 2 the
// period before that, etc.

// After wrapping the collection becomes slightly more
// complicated. An iterator is provide to simplify this
// for the user of the class.

template<class T=WaterUsageTimed>
class WaterUsageList {
    public:
    T*                  wut;
    T                   lastClosed;
    int                 countClosed;
    int                 closedMax;
    int                 indexCurrent;

    WaterUsageList(int _closedMax=1):
        wut(nullptr),
        countClosed(0),
        closedMax(0),
        indexCurrent(-1)
     {
        wut = nullptr;
        set_max_closed(_closedMax);
    }

    ~WaterUsageList() {
        delete[] wut;
    }
    
    void set_max_closed(int _closedMax) {
        ESP_LOGD("main", "at set_max_closed");

        if (_closedMax < 0 || _closedMax > 24 * 7) {
            _closedMax = 0;
        }

        T* new_wut = new T[1 + (_closedMax)];
        ESP_LOGD("main", "at set_max_closed 3");
        if (wut) {
            // Had an old wut that we need to properly copy to new
            int i = 0;
            T* last = nullptr; 
            while (i < _closedMax && (last = getPreviousClosed(last))) {
               new_wut[i++] = *last;
            }
            new_wut[i] = wut[indexCurrent];
            indexCurrent = i;
            countClosed = i;
            delete[] wut;
        } else {
            indexCurrent = -1;
            countClosed = 0;
        }
        wut = new_wut;
        closedMax = _closedMax;
        ESP_LOGD("main", "at set_max_closed end");
    }

    int get_max_closed() const {
        return closedMax;
    }

    void next() {
        auto indexLast = indexCurrent;

        if (indexCurrent != -1) {
            lastClosed = getCurrent();
        }

        if (indexCurrent == -1 || indexCurrent >= closedMax) {
            indexCurrent = 0;
        } else {
            ++indexCurrent;
            ++countClosed;
        }

        if (indexLast != -1) {
            wut[indexLast].close();
        }

        wut[indexCurrent].init();

        ESP_LOGI("main", "WaterUsageList.next old index %i, new index %i", indexLast, indexCurrent);
    }

    // Iterator to return closed items
    //  cur == nullptr  - get most recent closed
    //  cur == 
    T* getPreviousClosed(T* last) {
        // getCurrentIndex() insures that indexCurrent is valid
        getCurrentIndex();

        // We return last - 1 (last is indexCurrent if no last given)
        int retIndex = (last ? last - &wut[0] : indexCurrent) - 1;
        
        // If retIndex is less than zero then we wrap to end if list
        if (retIndex < 0) {
            retIndex = countClosed;
        }

        // If we are no at the current index then there are no 
        // more closed entries        
        return retIndex == indexCurrent ? nullptr : &wut[retIndex];
    }

    const T& getLastClosed() const {
        return lastClosed;
    }    

    int getCurrentIndex() {
        if (indexCurrent == -1) {
            next();
        }

       return indexCurrent;
    }

    T& getCurrent() {
        return wut[getCurrentIndex()];
    }

    // Returns: true  - current period was closed
    //          false - otherwise  
    bool addUsage(float usage) {

        getCurrent().addUsage(usage);

        return false;
    }

    float getUsage() {
        return getCurrent().getUsage();
    }

    void convert_uom(std::function<float(float &)>f) {
        
        wut[getCurrentIndex()].convert_uom(f);
        T* last = nullptr;
        while ((last = getPreviousClosed(last))) {
            last->convert_uom(f);
        } 

    }

    void clearClosed() {
        if (getCurrentIndex() != 0) {
            wut[0] = wut[getCurrentIndex()];
            indexCurrent = 0;
        }

        countClosed = 0;
    }

    T* getCurrentStarted() {
        T* rv = nullptr;
        T& cur = getCurrent();
        if (cur.isStarted()) {
            rv = &cur;
        }
        return rv;
    }

    JsonObject& currentToJson() {

        JsonObject& jo = global_json_buffer.createObject();

        T& cur = getCurrent();
        if (cur.isStarted()) {
            jo["current"] = cur.toJson();
        }

        jo["closed_count"] = countClosed;

        return jo;
    }

    JsonObject& toJson() {

        JsonObject& jo = global_json_buffer.createObject();

        jo["current"] = wut[indexCurrent].toJson();

        // Closed periods go into array
        JsonArray& ja = global_json_buffer.createArray();

        T* last = nullptr;
        while ((last = getPreviousClosed(last))) {
            ja.add(last->toJson());
        } 

        jo["closed"] = ja;

        return jo;
    }
};

typedef WaterUsageList<> WaterUsagePeriodList;

////////////////////////////////////////////////////////
// WaterUsageSession: a class to detect and handle
// a single water usage session.
//
// States: 
//  Not started         - x minutes of no water flow
//                          [start_time == 0]
//  started             - water flowed with x minutes
//                          [start_time != 0 && seconds == 0]
//  Closed              - water flowed for at least xx minutes
//                          and then stopped flowing for n minutes
//                          [start_time != 0 && seconds != 0]
//
////////////////////////////////////////////////////////

class WaterUsageSession: public WaterUsageTimed {
    public:
    WaterUsageSession(): WaterUsageTimed(start_on_first_usage) {
    }
};


////////////////////////////////////////////////////////
// WaterUsageSessionList: a class to manage
// a collection of water usage sessions.
//
//
////////////////////////////////////////////////////////
class WaterUsageSessionList: public WaterUsageList<WaterUsageSession>  {

    time_t      time_last_addUsage_call;

    // > 3 minutes of no flow means flow is "dormant"
    // <= 3 minutes of no flow is a pause between zone valve
    //  open and close

    const int def_end_session_secs = 3 * 60;
    int end_session_secs;  

    // irrigation sessions less than this are thrown away
    const int def_min_session_secs = 4 * 60;
    int min_session_secs;
    
    // Seconds of water flow of "zero"
    int wf0_secs;


    public:
    WaterUsageSessionList(): 
        WaterUsageList(), 
        end_session_secs(def_end_session_secs),
        min_session_secs(def_min_session_secs) {

        // When we start we assume line was dormant
        wf0_secs = end_session_secs;
        time_last_addUsage_call = 0;
    }

    void clearCurrent() {
        getCurrent().init();
    }

    // Returns: true  - current period was closed
    //          false - otherwise  
    bool addUsage(float usage) {

        // The rule are simple
        // wf0
        //      current started AND wf0t >= dormancy-time
        //          current-secs > min-time
        //              close current
        //          else
        //              init current
        // else
        //      current not started?
        //          start current
        //      
        //       add usage to current

        bool rc = false;

        int secs_since_last_call;
        time_t time_this_addUsage_call = sntp_time->timestamp_now();

        if (time_last_addUsage_call) {
            secs_since_last_call = time_this_addUsage_call - time_last_addUsage_call;
        } else {
            secs_since_last_call = 0;
        } 

        time_last_addUsage_call = time_this_addUsage_call;

        WaterUsageSession& cur = getCurrent();

        if (usage <= *g_upm_base) {

            // No water flow
            ESP_LOGD("main", "Session: no waterflow for %i secs, usage=%f, base=%f", 
              wf0_secs, usage, *g_upm_base); 

            // Increment seconds of no water flow
            wf0_secs += secs_since_last_call;

            // We have a session AND we've now been wf0 to conclude that we are
            // done.
            #ifndef DEBUG_SESSION
            if (cur.isStarted() && wf0_secs >= end_session_secs) {
            #else
            // No wait in debug
            if (cur.isStarted() ) {
            #endif
                ESP_LOGD("main", "Session: w aterflow sttopped, end_session_secs=%i", end_session_secs); 
               // Now determine if the session was long enough to constitite
                // a real irrigation
                #ifndef DEBUG_SESSION
                if (cur.timeSoFar(time_this_addUsage_call - wf0_secs) < min_session_secs) {
                    // Throw back the little ones 
                    cur.init();
                    ESP_LOGD("main", "Session: Throw back the little ones"); 
                } else {
                #endif
                    // And pan-fry the big ones
                    cur.close(time_this_addUsage_call - wf0_secs);
                    next();
                     ESP_LOGD("main", "Session: pan-fry the big ones"); 
                   rc = true;
                #ifndef DEBUG_SESSION
                }
                #endif
            }
        } else {

            // We have water flow
            //if (!cur.isStarted()) {
                //ESP_LOGD("main", "Session: We have waterflow"); 
                APP_LOG_LOG("Session: We have waterflow"); 
            //}
           
            cur.addUsage(usage);

            wf0_secs = 0;
        }

        return rc;
    }

    int get_end_session_secs() const {
        return end_session_secs;
    }

    void set_end_session_secs(int _end_session_secs) {
        end_session_secs = _end_session_secs;
    }

    int get_min_session_secs() const {
        return min_session_secs;
    }
    void set_min_session_secs(int _min_session_secs) {
        min_session_secs = _min_session_secs;
    }



};

////////////////////////////////////////////////////////
// WaterUsageNamed: 
//
//
////////////////////////////////////////////////////////

class WaterUsageNamed: public WaterUsageTimed {
    public:

    enum {
        active =                   next_flag_value,
        closed =                   active << 1,
        canceled =                 closed << 1,
        next_flag_value =          canceled << 1
    };

    time_t  expire_time = 0;

    WaterUsageNamed(): WaterUsageTimed() {
    }
};


// WaterUsageNamed uses a completely different strategy for active and "closed" units.
// The other lists alwsays have one (WaterUsagePeriodList and WaterUsageSessionList) 
// or zero (WaterUsageSessionList only), while WaterUsageNamedList can have more than one
// current at a time.

// WaterUsageNamedList probably shouldn't inherit WaterUsageList at all (TODO!)

////////////////////////////////////////////////////////
// WaterUsageNamedList: a class to manage
// a collection of water usage sessions.
//
//
////////////////////////////////////////////////////////

class WaterUsageNamedList: public WaterUsageList<WaterUsageNamed>  {

    int countActive_ = 0;

    public:
    WaterUsageNamedList(): 
        WaterUsageList() {

    }

    int count() { return countActive_; }

    bool add_usage_unit(const std::string& name, time_t expire_time) {

        ESP_LOGD("main", "start_usage_unit {");

        bool rc = false;

        WaterUsageNamed* pwun = findActive(name);
        if (!pwun) {
            pwun = getFirstAvailable();
            if (pwun) { ESP_LOGD("main", "getFirstAvailable"); }
        }

        if (!pwun) {
            pwun = getOldest(WaterUsageNamed::closed);
            if (pwun) { ESP_LOGD("main", "getOldest(closed"); }
        }

        if (!pwun) {
            pwun = getOldest(WaterUsageNamed::active);
             if (pwun) { ESP_LOGD("main", "getOldest(active"); }
       }

        if (pwun) {
            ESP_LOGD("main", "init name start");
            pwun->init();
            pwun->name = name;
            pwun->expire_time = expire_time;
            pwun->start(WaterUsageNamed::active);

            ++countActive_;
            rc = true;
        }

        ESP_LOGD("main", "} start_usage_unit");

        return rc;
    }
    
    bool delete_usage_unit(const std::string& name, bool cancel=false) {
        WaterUsageNamed* pwun = findActive(name);
        if (pwun) {
            close_usage_unit(pwun, cancel);
            return true;
        }
        return false;
    }

    void close_usage_unit(WaterUsageNamed* pwun, bool cancel=false) {
        pwun->close();
        pwun->unset(WaterUsageNamed::active);
        pwun->set(cancel ? WaterUsageNamed::canceled : WaterUsageNamed::closed);
        ++countClosed;
        --countActive_;
        lastClosed = *pwun;
    }

    bool purgeFirstExpired() {
        time_t now = sntp_time->now().timestamp;
        int i;
        for (WaterUsageNamed* pwun = &wut[i = 0]; i < closedMax + 1; ++i, ++pwun) {
            if (pwun->is(WaterUsageNamed::active) && pwun->expire_time > now) {
                close_usage_unit(pwun);
                return true;
            }
        }
        return false;
    }
    
    void addUsage(float usage) {

        int i;
        for (WaterUsageNamed* pwun = &wut[i = 0]; i < closedMax + 1; ++i, ++pwun) {
            if (pwun->is(WaterUsageNamed::active)) {
                pwun->addUsage(usage);
            }
        }
    }

    WaterUsageNamed* findActive(const std::string& name) {

        int i;
        for (WaterUsageNamed* pwun = &wut[i = 0]; i < closedMax + 1; ++i, ++pwun) {
            if (pwun->name == name && pwun->is(WaterUsageNamed::active)) {
                return pwun;
            }
        }

        return nullptr;
    }

    WaterUsageNamed* getFirstAvailable() {

        int i;
        for (WaterUsageNamed* pwun = &wut[i = 0]; i < closedMax + 1; ++i, ++pwun) {
            if (pwun->is(WaterUsageNamed::canceled) || (!pwun->is(WaterUsageNamed::active) && !pwun->isStarted())) {
                return pwun;
            }
        }

        return nullptr;
    }

    WaterUsageNamed* getOldest(unsigned int flags) {

        WaterUsageNamed* pwunOldest = nullptr;

        int i;
        for (WaterUsageNamed* pwun = &wut[i = 0]; i < closedMax + 1; ++i, ++pwun) {
            if (!pwun->is(flags) && 
            (pwunOldest == nullptr || pwun->start_time < pwunOldest->start_time )) {
                pwunOldest = pwun;
            }
        }

        return nullptr;
    }

    // Overrides
    void clearClosed() {
        int i;
        for (WaterUsageNamed* pwun = &wut[i = 0]; i < closedMax + 1; ++i, ++pwun) {
            pwun->unset(WaterUsageNamed::closed);
        }

        countClosed = 0;
    }

    void convert_uom(std::function<float(float &)>f) {
        int i;
        for (WaterUsageNamed* pwun = &wut[i = 0]; i < closedMax + 1; ++i, ++pwun) {
            pwun->convert_uom(f);
        }
    }

    JsonObject& toJson() {

        JsonObject& jo = global_json_buffer.createObject();

        JsonArray& jaActive = global_json_buffer.createArray();
        JsonArray& jaClosed = global_json_buffer.createArray();

        int i;
        for (WaterUsageNamed* pwun = &wut[i = 0]; i < closedMax + 1; ++i, ++pwun) {
            if (pwun->is(WaterUsageNamed::active)) {
                jaActive.add(pwun->toJson());
            } else if (pwun->is(WaterUsageNamed::closed)) {
                jaClosed.add(pwun->toJson());
            } 
        }

        jo["active"] = jaActive;
        jo["closed"] = jaClosed;

        return jo;
    }


};