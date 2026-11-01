// Minimal streaming JSON writer. The backend only ever *produces* JSON;
// requests carry parameters in the query string and raw file text in the body.
#pragma once
#include <string>
#include <vector>

namespace wb {

class JsonWriter {
public:
    JsonWriter& beginObject();
    JsonWriter& endObject();
    JsonWriter& beginArray();
    JsonWriter& endArray();
    JsonWriter& key(const std::string& k);
    JsonWriter& value(const std::string& v);
    JsonWriter& value(const char* v);
    JsonWriter& value(double v);
    JsonWriter& value(long long v);
    JsonWriter& value(int v) { return value(static_cast<long long>(v)); }
    JsonWriter& value(std::size_t v) { return value(static_cast<long long>(v)); }
    JsonWriter& value(bool v);
    JsonWriter& null();
    // Writes a flat array of doubles; much faster than per-element calls for big point clouds.
    JsonWriter& numberArray(const std::vector<double>& v, int precision = 9);
    template <typename T>
    JsonWriter& field(const std::string& k, const T& v) { key(k); return value(v); }

    const std::string& str() const { return out_; }

    static std::string escape(const std::string& s);

private:
    void separator();
    std::string out_;
    std::vector<bool> firstInScope_;
    bool afterKey_ = false;
};

// Formats a double with enough digits to round-trip; non-finite values become null.
std::string formatNumber(double v, int precision = 17);

}  // namespace wb
