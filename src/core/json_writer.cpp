#include "json_writer.h"

#include <cmath>
#include <cstdio>

namespace wb {

std::string formatNumber(double v, int precision) {
    if (!std::isfinite(v)) return "null";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*g", precision, v);
    return buf;
}

std::string JsonWriter::escape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    r += buf;
                } else {
                    r += static_cast<char>(c);
                }
        }
    }
    return r;
}

void JsonWriter::separator() {
    if (afterKey_) {
        afterKey_ = false;
        return;
    }
    if (!firstInScope_.empty()) {
        if (!firstInScope_.back()) out_ += ',';
        firstInScope_.back() = false;
    }
}

JsonWriter& JsonWriter::beginObject() {
    separator();
    out_ += '{';
    firstInScope_.push_back(true);
    return *this;
}

JsonWriter& JsonWriter::endObject() {
    out_ += '}';
    firstInScope_.pop_back();
    return *this;
}

JsonWriter& JsonWriter::beginArray() {
    separator();
    out_ += '[';
    firstInScope_.push_back(true);
    return *this;
}

JsonWriter& JsonWriter::endArray() {
    out_ += ']';
    firstInScope_.pop_back();
    return *this;
}

JsonWriter& JsonWriter::key(const std::string& k) {
    separator();
    out_ += '"' + escape(k) + "\":";
    afterKey_ = true;
    return *this;
}

JsonWriter& JsonWriter::value(const std::string& v) {
    separator();
    out_ += '"' + escape(v) + '"';
    return *this;
}

JsonWriter& JsonWriter::value(const char* v) { return value(std::string(v)); }

JsonWriter& JsonWriter::value(double v) {
    separator();
    out_ += formatNumber(v);
    return *this;
}

JsonWriter& JsonWriter::value(long long v) {
    separator();
    out_ += std::to_string(v);
    return *this;
}

JsonWriter& JsonWriter::value(bool v) {
    separator();
    out_ += v ? "true" : "false";
    return *this;
}

JsonWriter& JsonWriter::null() {
    separator();
    out_ += "null";
    return *this;
}

JsonWriter& JsonWriter::numberArray(const std::vector<double>& v, int precision) {
    separator();
    out_ += '[';
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out_ += ',';
        out_ += formatNumber(v[i], precision);
    }
    out_ += ']';
    return *this;
}

}  // namespace wb
