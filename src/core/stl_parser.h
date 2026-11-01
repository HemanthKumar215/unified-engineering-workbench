// ASCII STL parser with strict validation.
// STL carries no unit information; the workbench treats coordinates as metres
// unless the user picks another unit in the UI (display only).
#pragma once
#include <array>
#include <string>
#include <vector>

namespace wb {

struct StlMesh {
    std::string solidName;
    // 9 floats per triangle: v0.xyz v1.xyz v2.xyz
    std::vector<double> vertices;
    std::vector<double> normals;  // 3 per triangle, as stored in the file
    std::array<double, 3> bboxMin{0, 0, 0};
    std::array<double, 3> bboxMax{0, 0, 0};
    std::size_t triangleCount() const { return vertices.size() / 9; }
};

struct StlResult {
    bool ok = false;
    std::string error;     // user-facing message incl. line number
    int errorLine = 0;
    std::vector<std::string> warnings;
    StlMesh mesh;
};

// Hard limits; documented in README ("Tested file-size limits").
constexpr std::size_t kMaxStlBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxStlTriangles = 500'000;

StlResult parseAsciiStl(const std::string& text);

}  // namespace wb
