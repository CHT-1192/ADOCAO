#pragma once

#include <string>
#include <nlohmann/json.hpp>

// Clean ADOFAI JSON (fix missing commas, trailing commas, double commas)
std::string cleanJson(const std::string& raw);

// ADOFAI uses both booleans and "Enabled"/"Disabled" strings for bool fields
bool parseBool(const nlohmann::json& obj, const char* key, bool def = false);
