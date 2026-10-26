#pragma once

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <memory>
#include <iomanip>

namespace google_tts {

enum class JsonType {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class JsonValue {
public:
    JsonType type = JsonType::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<JsonValue> arr_val;
    std::map<std::string, JsonValue> obj_val;

    JsonValue() : type(JsonType::Null) {}
    JsonValue(bool b) : type(JsonType::Boolean), bool_val(b) {}
    JsonValue(double n) : type(JsonType::Number), num_val(n) {}
    JsonValue(int n) : type(JsonType::Number), num_val(static_cast<double>(n)) {}
    JsonValue(int64_t n) : type(JsonType::Number), num_val(static_cast<double>(n)) {}
    JsonValue(const std::string& s) : type(JsonType::String), str_val(s) {}
    JsonValue(const char* s) : type(JsonType::String), str_val(s ? s : "") {}
    JsonValue(JsonType t) : type(t) {}

    bool is_null() const { return type == JsonType::Null; }
    bool is_bool() const { return type == JsonType::Boolean; }
    bool is_number() const { return type == JsonType::Number; }
    bool is_string() const { return type == JsonType::String; }
    bool is_array() const { return type == JsonType::Array; }
    bool is_object() const { return type == JsonType::Object; }

    bool as_bool(bool def = false) const { return is_bool() ? bool_val : def; }
    double as_double(double def = 0.0) const { return is_number() ? num_val : def; }
    int64_t as_int64(int64_t def = 0) const { return is_number() ? static_cast<int64_t>(num_val) : def; }
    int as_int(int def = 0) const { return is_number() ? static_cast<int>(num_val) : def; }
    const std::string& as_string() const { return str_val; }
    std::string as_string_or(const std::string& def) const { return is_string() ? str_val : def; }
    const std::vector<JsonValue>& as_array() const { return arr_val; }
    const std::map<std::string, JsonValue>& as_object() const { return obj_val; }

    bool contains(const std::string& key) const {
        if (!is_object()) return false;
        return obj_val.find(key) != obj_val.end();
    }

    const JsonValue& get(const std::string& key) const {
        static const JsonValue null_val;
        if (!is_object()) return null_val;
        auto it = obj_val.find(key);
        return (it != obj_val.end()) ? it->second : null_val;
    }

    const JsonValue& operator[](const std::string& key) const {
        return get(key);
    }

    static std::string escape_string(const std::string& s) {
        std::ostringstream o;
        for (char c : s) {
            switch (c) {
                case '"': o << "\\\""; break;
                case '\\': o << "\\\\"; break;
                case '\b': o << "\\b"; break;
                case '\f': o << "\\f"; break;
                case '\n': o << "\\n"; break;
                case '\r': o << "\\r"; break;
                case '\t': o << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) <= 0x1f) {
                        o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c));
                    } else {
                        o << c;
                    }
            }
        }
        return o.str();
    }

    static JsonValue parse(const std::string& text) {
        size_t pos = 0;
        skip_ws(text, pos);
        return parse_value(text, pos);
    }

private:
    static void skip_ws(const std::string& s, size_t& pos) {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) {
            pos++;
        }
    }

    static JsonValue parse_value(const std::string& s, size_t& pos) {
        skip_ws(s, pos);
        if (pos >= s.size()) return JsonValue();
        char c = s[pos];
        if (c == '{') return parse_object(s, pos);
        if (c == '[') return parse_array(s, pos);
        if (c == '"') return parse_string(s, pos);
        if (c == 't' || c == 'f') return parse_bool(s, pos);
        if (c == 'n') return parse_null(s, pos);
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number(s, pos);
        return JsonValue();
    }

    static JsonValue parse_object(const std::string& s, size_t& pos) {
        JsonValue val(JsonType::Object);
        pos++; // skip '{'
        while (pos < s.size()) {
            skip_ws(s, pos);
            if (pos < s.size() && s[pos] == '}') {
                pos++;
                return val;
            }
            if (s[pos] != '"') break;
            std::string key = parse_string(s, pos).as_string();
            skip_ws(s, pos);
            if (pos >= s.size() || s[pos] != ':') break;
            pos++; // skip ':'
            val.obj_val[key] = parse_value(s, pos);
            skip_ws(s, pos);
            if (pos < s.size() && s[pos] == ',') {
                pos++;
            } else if (pos < s.size() && s[pos] == '}') {
                pos++;
                return val;
            } else {
                break;
            }
        }
        return val;
    }

    static JsonValue parse_array(const std::string& s, size_t& pos) {
        JsonValue val(JsonType::Array);
        pos++; // skip '['
        while (pos < s.size()) {
            skip_ws(s, pos);
            if (pos < s.size() && s[pos] == ']') {
                pos++;
                return val;
            }
            val.arr_val.push_back(parse_value(s, pos));
            skip_ws(s, pos);
            if (pos < s.size() && s[pos] == ',') {
                pos++;
            } else if (pos < s.size() && s[pos] == ']') {
                pos++;
                return val;
            } else {
                break;
            }
        }
        return val;
    }

    static JsonValue parse_string(const std::string& s, size_t& pos) {
        pos++; // skip '"'
        std::string res;
        while (pos < s.size()) {
            char c = s[pos++];
            if (c == '"') {
                return JsonValue(res);
            }
            if (c == '\\' && pos < s.size()) {
                char esc = s[pos++];
                switch (esc) {
                    case '"': res += '"'; break;
                    case '\\': res += '\\'; break;
                    case '/': res += '/'; break;
                    case 'b': res += '\b'; break;
                    case 'f': res += '\f'; break;
                    case 'n': res += '\n'; break;
                    case 'r': res += '\r'; break;
                    case 't': res += '\t'; break;
                    case 'u': {
                        if (pos + 4 <= s.size()) {
                            std::string hex_str = s.substr(pos, 4);
                            pos += 4;
                            unsigned int codepoint = 0;
                            try {
                                codepoint = std::stoul(hex_str, nullptr, 16);
                            } catch (...) {}
                            // Convert unicode codepoint to UTF-8
                            if (codepoint <= 0x7f) {
                                res += static_cast<char>(codepoint);
                            } else if (codepoint <= 0x7ff) {
                                res += static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f));
                                res += static_cast<char>(0x80 | (codepoint & 0x3f));
                            } else {
                                res += static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f));
                                res += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
                                res += static_cast<char>(0x80 | (codepoint & 0x3f));
                            }
                        }
                        break;
                    }
                    default: res += esc; break;
                }
            } else {
                res += c;
            }
        }
        return JsonValue(res);
    }

    static JsonValue parse_number(const std::string& s, size_t& pos) {
        size_t start = pos;
        if (pos < s.size() && s[pos] == '-') pos++;
        while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E' || s[pos] == '+' || s[pos] == '-')) {
            pos++;
        }
        try {
            double v = std::stod(s.substr(start, pos - start));
            return JsonValue(v);
        } catch (...) {
            return JsonValue(0.0);
        }
    }

    static JsonValue parse_bool(const std::string& s, size_t& pos) {
        if (s.compare(pos, 4, "true") == 0) {
            pos += 4;
            return JsonValue(true);
        }
        if (s.compare(pos, 5, "false") == 0) {
            pos += 5;
            return JsonValue(false);
        }
        return JsonValue();
    }

    static JsonValue parse_null(const std::string& s, size_t& pos) {
        if (s.compare(pos, 4, "null") == 0) {
            pos += 4;
        }
        return JsonValue();
    }
};

} // namespace google_tts
