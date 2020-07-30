#pragma once

bool getBool(const JsonObject& jo, const char *name, bool dfault);
const char* getString(const JsonObject& jo, const char *name, const char* dfault);
int getInt(const JsonObject& jo, const char *name, int dfault);
float getFloat(const JsonObject& jo, const char *name, float dfault);
const JsonObject& getObject(const JsonObject& jo, const char *name);
//const JsonArray& getArray(const JsonArray& jo, const char *name);
const JsonArray& getArray(const JsonObject& jo, const char *name);


