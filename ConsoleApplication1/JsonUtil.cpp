#include "JsonUtil.h"

#include <cctype>
#include <iomanip>
#include <sstream>

std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (unsigned char ch : text) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                std::ostringstream oss;
                oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
                out += oss.str();
            }
            else {
                out += static_cast<char>(ch);
            }
        }
    }
    return out;
}

std::optional<std::string> JsonGetString(const std::string& json, const std::string& key, size_t from) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, from);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos = json.find('"', pos);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    ++pos;
    std::string value;
    while (pos < json.size()) {
        const char c = json[pos++];
        if (c == '"') {
            return value;
        }
        if (c == '\\' && pos < json.size()) {
            const char esc = json[pos++];
            switch (esc) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u':
                if (pos + 4 <= json.size()) {
                    value.push_back('?');
                    pos += 4;
                }
                break;
            default: value.push_back(esc); break;
            }
        }
        else {
            value.push_back(c);
        }
    }
    return std::nullopt;
}

std::optional<int64_t> JsonGetInt(const std::string& json, const std::string& key, size_t from) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, from);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    bool negative = false;
    if (pos < json.size() && json[pos] == '-') {
        negative = true;
        ++pos;
    }
    int64_t value = 0;
    bool found = false;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        found = true;
        value = value * 10 + (json[pos] - '0');
        ++pos;
    }
    if (!found) {
        return std::nullopt;
    }
    return negative ? -value : value;
}

std::optional<double> JsonGetDouble(const std::string& json, const std::string& key, size_t from) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, from);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    size_t end = pos;
    while (end < json.size() &&
        (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '.' || json[end] == '-')) {
        ++end;
    }
    if (end == pos) {
        return std::nullopt;
    }
    try {
        return std::stod(json.substr(pos, end - pos));
    }
    catch (...) {
        return std::nullopt;
    }
}

std::vector<std::string> JsonSplitTopObjects(const std::string& json) {
    std::vector<std::string> objects;
    const size_t start = json.find('[');
    if (start == std::string::npos) {
        return objects;
    }
    size_t i = start + 1;
    int depth = 0;
    size_t objStart = std::string::npos;
    while (i < json.size()) {
        const char c = json[i];
        if (c == '{') {
            if (depth == 0) {
                objStart = i;
            }
            ++depth;
        }
        else if (c == '}') {
            --depth;
            if (depth == 0 && objStart != std::string::npos) {
                objects.push_back(json.substr(objStart, i - objStart + 1));
                objStart = std::string::npos;
            }
        }
        ++i;
    }
    return objects;
}

