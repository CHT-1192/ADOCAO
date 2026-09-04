#pragma once

#include <string>

// Clean ADOFAI JSON (fix missing commas, trailing commas, double commas)
std::string cleanJson(const std::string& raw);
