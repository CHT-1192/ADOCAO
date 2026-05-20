#include "JsonCleaner.h"
#include <cstring>

// In-string replacement: replaces False/True/None → false/true/null (not inside strings)
static void fixPythonLiterals(std::string& s) {
    const char* pairs[][2] = {
        {"False", "false"}, {"True", "true"}, {"None", "null"},
        {"FALSE", "false"}, {"TRUE", "true"}, {"NONE", "null"},
    };
    bool inStr = false, esc = false;
    for (size_t i = 0; i + 4 < s.size(); i++) {
        char c = s[i];
        if (inStr) { if (esc) esc = false; else if (c == '\\') esc = true; else if (c == '"') inStr = false; continue; }
        if (c == '"') { inStr = true; continue; }
        for (auto& p : pairs) {
            size_t len = std::strlen(p[0]);
            if (i + len > s.size()) continue;
            if (std::memcmp(&s[i], p[0], len) == 0) {
                // make sure it's a whole word (not part of a larger identifier)
                char before = (i > 0) ? s[i-1] : ' ';
                char after  = (i + len < s.size()) ? s[i+len] : ' ';
                if (!((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') ||
                      (before >= '0' && before <= '9') || before == '_' ||
                      (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') ||
                      (after >= '0' && after <= '9') || after == '_')) {
                    s.replace(i, len, p[1]);
                }
            }
        }
    }
}

std::string cleanJson(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());

    bool inString = false;
    bool escaped = false;
    char lastOut = 0;

    for (size_t i = 0; i < raw.size(); i++) {
        char c = raw[i];

        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            if (c == '\r') continue;
            lastOut = c;
            out.push_back(c);
            continue;
        }

        if (c == '"') {
            inString = true;
            // Insert missing comma before key: ...value"key" or ...}"key" or ...]key"
            if (lastOut == '}' || lastOut == ']' || lastOut == '"' ||
                (lastOut >= '0' && lastOut <= '9') ||
                (lastOut >= 'a' && lastOut <= 'z') ||
                (lastOut >= 'A' && lastOut <= 'Z')) {
                out.push_back(',');
            }
            lastOut = '"';
            out.push_back(c);
            continue;
        }

        if (c == '\r') continue;

        // Insert missing comma before { or [ when preceded by value
        if ((c == '{' || c == '[') &&
            (lastOut == '}' || lastOut == ']' || lastOut == '"' ||
             (lastOut >= '0' && lastOut <= '9') ||
             (lastOut >= 'a' && lastOut <= 'z') ||
             (lastOut >= 'A' && lastOut <= 'Z'))) {
            out.push_back(',');
        }

        // Skip trailing commas before } or ] or another comma
        if (c == ',') {
            size_t j = i + 1;
            while (j < raw.size() && (raw[j] == ' ' || raw[j] == '\t' || raw[j] == '\n' || raw[j] == '\r'))
                j++;
            if (j < raw.size()) {
                char nc = raw[j];
                if (nc == '}' || nc == ']' || nc == ',') continue;
            }
        }

        if (c != ' ' && c != '\t' && c != '\n') lastOut = c;
        out.push_back(c);
    }

    // Post-pass: remove leading commas after { or [ and double commas
    std::string out2;
    out2.reserve(out.size());
    bool afterBrace = false;
    bool justSkippedComma = false;
    for (char c : out) {
        if (c == '{' || c == '[') {
            afterBrace = true;
            justSkippedComma = false;
            out2.push_back(c);
        } else if (c == ',' && afterBrace) {
            continue;
        } else if (c == ',' && justSkippedComma) {
            continue;
        } else if (c == ',') {
            justSkippedComma = true;
            out2.push_back(c);
        } else if (c != ' ' && c != '\t' && c != '\n') {
            afterBrace = false;
            justSkippedComma = false;
            out2.push_back(c);
        } else {
            out2.push_back(c);
        }
    }

    fixPythonLiterals(out2);
    return out2;
}

bool parseBool(const nlohmann::json& obj, const char* key, bool def) {
    if (!obj.contains(key)) return def;
    auto& v = obj[key];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        return s == "Enabled" || s == "true";
    }
    return def;
}
