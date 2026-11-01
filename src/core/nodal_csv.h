// Parser for nodal temperature results exported as CSV with the exact header
//   node_id,x_m,y_m,z_m,temperature_C
// (the schema of an ANSYS Mechanical nodal-temperature export after column renaming).
#pragma once
#include <string>
#include <vector>

namespace wb {

struct NodalField {
    std::vector<long long> nodeId;
    std::vector<double> xyz;          // 3 per node, metres
    std::vector<double> temperature;  // degC
    double tMin = 0, tMax = 0;
    double bboxMin[3]{0, 0, 0}, bboxMax[3]{0, 0, 0};
    std::size_t size() const { return nodeId.size(); }
};

struct NodalResult {
    bool ok = false;
    std::string error;
    int errorLine = 0;
    std::vector<std::string> warnings;
    NodalField field;
};

constexpr std::size_t kMaxCsvBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxCsvNodes = 1'000'000;

NodalResult parseNodalCsv(const std::string& text);

}  // namespace wb
