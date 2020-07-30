#include "esphome.h"

bool getBool(const JsonObject& jo, const char *name, bool dfault) { 

  bool rv = dfault;
  if (jo.containsKey(name) && jo[name].is<bool>()) {
      rv = jo[name];
  }

  return rv;
}
const char* getString(const JsonObject& jo, const char *name, const char* dfault) { 

  const char* rv = dfault;
  if (jo.containsKey(name) && jo[name].is<char*>()) {
      rv = jo[name];
  }

  return rv;
}

int getInt(const JsonObject& jo, const char *name, int dfault) { 

  int rv = dfault;
  if (jo.containsKey(name) && jo[name].is<int>()) {
      rv = jo[name];
  }

  return rv;
}

float getFloat(const JsonObject& jo, const char *name, float dfault) { 

  float rv = dfault;
  if (jo.containsKey(name) && jo[name].is<float>()) {
      rv = jo[name];
  }

  return rv;
}


const JsonObject& getObject(const JsonObject& jo, const char *name) { 

  if (jo.containsKey(name) && jo[name].is<JsonObject>()) {
      return jo[name];
  } else {
    return JsonObject::invalid();
  }
}

const JsonArray& getArray(const JsonObject& jo, const char *name) { 

  if (jo.containsKey(name) && jo[name].is<JsonArray>()) {
      return jo[name];
  } else {
    return JsonArray::invalid();
  }
}

