#pragma once

using namespace std;

	// std::array<char, 16> arr1;
  // auto lambda1 = [arr1](){};


class TranslationUnit {

  float& calibrate_factor_; 

public:
  TranslationUnit(float& calibrate_factor):
    calibrate_factor_(calibrate_factor) {

  }
  virtual ~TranslationUnit() {};

  void setCalibrateFactor(float& calibrate_factor) { calibrate_factor_ = calibrate_factor; }
  
  // Unit of measure text
  virtual const char* uom_text() = 0; 
  virtual const char* uom_pm_text() = 0; 
  virtual bool is_match(const char* str) { 
    return strcmp(str, uom_text()) == 0 || strcmp(str, uom_pm_text()) == 0 
        ? true 
        : false; };
  virtual float pulses_to_uom() = 0;
  virtual float convert_pulses_to_uom(float pulses) { return (pulses / pulses_to_uom()) * calibrate_factor_; }
  virtual float convert_uom_to_pulses(float uom) { return (uom / calibrate_factor_) * pulses_to_uom(); }
};

class GenericTranslationUnit: public TranslationUnit {
  virtual const char* uom_text() { return "gal"; } 
  virtual const char* uom_pm_text()  { return "gpm"; }  
  virtual float pulses_to_uom() { return 1380.0; }
};

class GallonTranslationUnit: public TranslationUnit {
  public:
    GallonTranslationUnit( float& calibrate_factor):
      TranslationUnit(calibrate_factor) { }
    virtual ~GallonTranslationUnit() {};

  virtual const char* uom_text() { return "gal"; } 
  virtual const char* uom_pm_text()  { return "gpm"; }  
  virtual float pulses_to_uom() { return 1380.0; }
};

// 1 US gallon = 3.78541 liters
// 1 liter = 0.264172 US gallons

class LiterTranslationUnit: public TranslationUnit {
  public:
    LiterTranslationUnit( float& calibrate_factor):
      TranslationUnit(calibrate_factor) { }
    virtual ~LiterTranslationUnit() {};
      
  virtual const char* uom_text() { return "L"; } 
  virtual const char* uom_pm_text()  { return "lpm"; }  
  virtual float pulses_to_uom() { return 364.56; }
};

class TranslationManager: public vector<TranslationUnit*> {
  GallonTranslationUnit gal_xlate_; 
  LiterTranslationUnit liter_xlate_;
  float calibrate_factor_ = 1.0;
public:
  TranslationUnit* current =  &gal_xlate_;
  TranslationManager():
  gal_xlate_(calibrate_factor_),
  liter_xlate_(calibrate_factor_) {
    push_back(&gal_xlate_);
    push_back(&liter_xlate_);

    set_calibrate_factor(1.0);
  }

  void set_calibrate_factor(float calibrate_factor) {
    calibrate_factor_ = calibrate_factor;
    for (TranslationUnit* unit: *this ) {
      unit->setCalibrateFactor(calibrate_factor_);
    }
  }

    float get_calibrate_factor() const {
      return calibrate_factor_;
    }


// Get translation unit by unit of measure text or unit of measure per minute text
// One of the string args below must be nullptr
  // TranslationUnit* get_translation_unit(const char* uom_text, const char* upm_text=nullptr) {
  //   // for (iterator it = begin(); it != end(); ++it) {
  //   //     //std::cout << *it << "\n";
  //   // }    

  //   for (TranslationUnit* unit: *this ) {
  //     if ((uom_text && uom_text == unit->uom_text()) || 
  //          (upm_text && upm_text == unit->uom_pm_text())) {
  //       return unit;
  //     };
  //   }

  //   return nullptr;
  // }
  bool set_current(const char* uom_upm_text) {

    bool changed = false;

    TranslationUnit* unit = get_translation_unit(uom_upm_text);
    if (unit && unit != current) {
      current = unit;
      changed = true;
    }

    return changed;
  }

  // Get translation unit by unit of measure text or unit of measure per minute text
  TranslationUnit* get_translation_unit(const char* uom_upm_text) const {
    TranslationUnit* ret = nullptr;
    for (TranslationUnit* unit: *this ) {
      if (unit->is_match(uom_upm_text)) {
        ret = unit;
        break;
      }
    };
    return ret;
  }

  enum ConvertType {
    GalToLiter,
    LiterToGal,
    CalibrateChange,
    invalid
  };

  ConvertType get_convert_code(const char* uom_upm_text) {

    ConvertType ret = ConvertType::invalid;
    if (uom_upm_text) {
      TranslationUnit* unit = get_translation_unit(uom_upm_text);
      if (unit) {
        ret = unit->is_match("gal") ? ConvertType::GalToLiter : ConvertType::LiterToGal;
      }
    } else {
      ret = ConvertType::CalibrateChange;
    }

    return ret;
  } 
      //return xlate_to->convert_pulses_to_uom(xlate_from->convert_uom_to_pulses(val));
  float convert(ConvertType type, float val, float old_calibrate=0) {
    switch (type) {
        case GalToLiter:
          return gal_xlate_.convert_pulses_to_uom(liter_xlate_.convert_uom_to_pulses(val));
        case LiterToGal:
          return liter_xlate_.convert_pulses_to_uom(gal_xlate_.convert_uom_to_pulses(val));
        case CalibrateChange:
          return (val / old_calibrate) * calibrate_factor_;
        default:
          return val;
    }
  }

  float galToLiter(float val) {
        return liter_xlate_.convert_pulses_to_uom(gal_xlate_.convert_uom_to_pulses(val));
  }


  // std::function<float (float)> retFun() {
  //   return [&](float val) { 
  //     gal_xlate_.convert_pulses_to_uom(liter_xlate_.convert_uom_to_pulses(val)); 
  //     };
  // }

  // std::function<int (int)> retFun() {
  //     return [](int x) { return x; };
  // }

  // auto getFunction(int algo) -> auto (*)(char const&, char const&) -> bool { 
  //   return true;
  //  }
  // function<float(float &)> func() {
  //     return gal_xlate_.convert_pulses_to_uom(liter_xlate_.convert_uom_to_pulses(0));
  // }
    // currentWaterUsage.convert_uom( [=](float &val) {
    //   return xlate_to->convert_pulses_to_uom(xlate_from->convert_uom_to_pulses(val));
    // });


};
