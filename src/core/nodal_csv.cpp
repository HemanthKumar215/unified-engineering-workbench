#include "nodal_csv.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_set>

namespace wb {

namespace {

const char* const kColumns[5] = {"node_id", "x_m", "y_m", "z_m", "temperature_C"};

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> splitComma(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    std::istringstream ss(line);
    while (std::getline(ss, cell, ',')) out.push_back(trim(cell));
    if (!line.empty() && line.back() == ',') out.push_back("");
    return out;
}

NodalResult fail(int line, const std::string& msg) {
    NodalResult r;
    r.errorLine = line;
    r.error = line > 0 ? "Line " + std::to_string(line) + ": " + msg : msg;
    return r;
}

}  // namespace

NodalResult parseNodalCsv(const std::string& text) {
    if (text.empty()) return fail(0, "The file is empty (0 bytes). Expected a header line 'node_id,x_m,y_m,z_m,temperature_C'.");
    if (text.size() > kMaxCsvBytes) return fail(0, "The file is larger than the 64 MB limit for nodal CSV.");

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    bool haveHeader = false;
    NodalResult r;
    std::unordered_set<long long> seen;

    while (std::getline(in, line)) {
        ++lineNo;
        if (lineNo == 1 && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // UTF-8 BOM
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;  // blank lines and comments (provenance notes) are allowed
        const auto cells = splitComma(t);

        if (!haveHeader) {
            if (cells.size() != 5) return fail(lineNo, "Header must have exactly 5 columns: node_id,x_m,y_m,z_m,temperature_C.");
            for (int i = 0; i < 5; ++i)
                if (cells[i] != kColumns[i])
                    return fail(lineNo, std::string("Header column ") + std::to_string(i + 1) + " should be '" + kColumns[i] + "' but is '" + cells[i] + "'.");
            haveHeader = true;
            continue;
        }
        if (cells.size() != 5)
            return fail(lineNo, "Expected 5 values but found " + std::to_string(cells.size()) + ".");

        char* end = nullptr;
        errno = 0;
        const long long id = std::strtoll(cells[0].c_str(), &end, 10);
        if (cells[0].empty() || *end != '\0' || errno == ERANGE)
            return fail(lineNo, "node_id '" + cells[0] + "' is not an integer.");
        if (id < 0) return fail(lineNo, "node_id must be non-negative.");
        if (!seen.insert(id).second) return fail(lineNo, "Duplicate node_id " + cells[0] + ".");

        double v[4];
        for (int i = 0; i < 4; ++i) {
            const std::string& c = cells[i + 1];
            v[i] = std::strtod(c.c_str(), &end);
            if (c.empty() || *end != '\0')
                return fail(lineNo, std::string("Column '") + kColumns[i + 1] + "' value '" + c + "' is not a number.");
            if (!std::isfinite(v[i]))
                return fail(lineNo, std::string("Column '") + kColumns[i + 1] + "' is not finite (NaN/Inf are rejected).");
        }
        if (v[3] < -273.15) return fail(lineNo, "temperature_C is below absolute zero (-273.15 degC).");
        if (r.field.size() >= kMaxCsvNodes) return fail(lineNo, "More than 1,000,000 nodes; this exceeds the tested limit.");

        r.field.nodeId.push_back(id);
        r.field.xyz.insert(r.field.xyz.end(), v, v + 3);
        r.field.temperature.push_back(v[3]);
    }

    if (!haveHeader) return fail(0, "The file has no header line. Expected 'node_id,x_m,y_m,z_m,temperature_C'.");
    if (r.field.size() == 0) return fail(0, "The header is valid but there are no data rows, so there are no nodes to display.");

    auto& f = r.field;
    const auto mm = std::minmax_element(f.temperature.begin(), f.temperature.end());
    f.tMin = *mm.first;
    f.tMax = *mm.second;
    for (int i = 0; i < 3; ++i) f.bboxMin[i] = f.bboxMax[i] = f.xyz[i];
    for (std::size_t n = 0; n < f.size(); ++n)
        for (int i = 0; i < 3; ++i) {
            f.bboxMin[i] = std::min(f.bboxMin[i], f.xyz[3 * n + i]);
            f.bboxMax[i] = std::max(f.bboxMax[i], f.xyz[3 * n + i]);
        }
    if (f.tMin == f.tMax) r.warnings.push_back("All nodes have the same temperature; the colour legend has zero range.");
    r.ok = true;
    return r;
}

}  // namespace wb
