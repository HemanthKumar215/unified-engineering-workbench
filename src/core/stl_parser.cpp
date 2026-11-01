#include "stl_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace wb {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Strict number parse: the whole token must be consumed and the value finite.
bool parseFinite(const std::string& tok, double& out) {
    if (tok.empty()) return false;
    char* end = nullptr;
    out = std::strtod(tok.c_str(), &end);
    return end == tok.c_str() + tok.size() && std::isfinite(out);
}

class LineReader {
public:
    explicit LineReader(const std::string& text) : in_(text) {}
    // Returns the next non-blank line split into tokens; false at EOF.
    bool next(std::vector<std::string>& tokens) {
        std::string line;
        while (std::getline(in_, line)) {
            ++lineNo_;
            std::istringstream ls(line);
            tokens.clear();
            std::string t;
            while (ls >> t) tokens.push_back(t);
            if (!tokens.empty()) return true;
        }
        return false;
    }
    int line() const { return lineNo_; }

private:
    std::istringstream in_;
    int lineNo_ = 0;
};

StlResult fail(int line, const std::string& msg) {
    StlResult r;
    r.ok = false;
    r.errorLine = line;
    r.error = line > 0 ? "Line " + std::to_string(line) + ": " + msg : msg;
    return r;
}

}  // namespace

StlResult parseAsciiStl(const std::string& text) {
    if (text.empty())
        return fail(0, "The file is empty (0 bytes). Select an ASCII STL file that starts with 'solid <name>'.");
    if (text.size() > kMaxStlBytes)
        return fail(0, "The file is larger than the 64 MB limit for ASCII STL.");
    // Binary STL files have an 80-byte header that may even start with "solid"; NUL bytes give them away.
    if (text.find('\0') != std::string::npos)
        return fail(0, "This looks like a binary STL. Only ASCII STL is supported; re-export as ASCII from your CAD tool.");

    LineReader rd(text);
    std::vector<std::string> tk;
    if (!rd.next(tk))
        return fail(0, "The file contains only whitespace. An ASCII STL must start with 'solid <name>'.");
    if (lower(tk[0]) != "solid") return fail(rd.line(), "Expected 'solid' at the start of an ASCII STL file.");

    StlResult r;
    for (std::size_t i = 1; i < tk.size(); ++i) r.mesh.solidName += (i > 1 ? " " : "") + tk[i];

    bool ended = false;
    while (rd.next(tk)) {
        const std::string kw = lower(tk[0]);
        if (kw == "endsolid") {
            ended = true;
            break;
        }
        if (kw != "facet") return fail(rd.line(), "Expected 'facet normal nx ny nz' or 'endsolid', found '" + tk[0] + "'.");
        if (tk.size() != 5 || lower(tk[1]) != "normal")
            return fail(rd.line(), "Facet line must be 'facet normal nx ny nz'.");
        double n[3];
        for (int i = 0; i < 3; ++i)
            if (!parseFinite(tk[2 + i], n[i]))
                return fail(rd.line(), "Facet normal component '" + tk[2 + i] + "' is not a finite number.");

        if (!rd.next(tk) || lower(tk[0]) != "outer" || tk.size() != 2 || lower(tk[1]) != "loop")
            return fail(rd.line(), "Expected 'outer loop' after the facet normal.");

        double v[9];
        for (int k = 0; k < 3; ++k) {
            if (!rd.next(tk) || lower(tk[0]) != "vertex")
                return fail(rd.line(), "Expected 'vertex x y z' (a facet needs exactly 3 vertices).");
            if (tk.size() != 4) return fail(rd.line(), "A vertex line needs exactly three coordinates.");
            for (int i = 0; i < 3; ++i)
                if (!parseFinite(tk[1 + i], v[3 * k + i]))
                    return fail(rd.line(), "Vertex coordinate '" + tk[1 + i] + "' is not a finite number.");
        }
        if (!rd.next(tk) || lower(tk[0]) != "endloop")
            return fail(rd.line(), "Expected 'endloop' after three vertices (polygons with more than 3 vertices are not valid STL).");
        if (!rd.next(tk) || lower(tk[0]) != "endfacet") return fail(rd.line(), "Expected 'endfacet'.");

        if (r.mesh.triangleCount() >= kMaxStlTriangles)
            return fail(rd.line(), "More than 500,000 triangles; this exceeds the tested limit.");
        r.mesh.vertices.insert(r.mesh.vertices.end(), v, v + 9);
        r.mesh.normals.insert(r.mesh.normals.end(), n, n + 3);
    }
    if (!ended) return fail(rd.line(), "File ended without 'endsolid' - it may be truncated.");
    if (r.mesh.vertices.empty())
        return fail(0, "The STL is well formed but contains no triangles, so there is nothing to display.");

    // Bounding box and a degenerate-triangle check (warning only).
    auto& mn = r.mesh.bboxMin;
    auto& mx = r.mesh.bboxMax;
    mn = {r.mesh.vertices[0], r.mesh.vertices[1], r.mesh.vertices[2]};
    mx = mn;
    std::size_t degenerate = 0;
    for (std::size_t t = 0; t < r.mesh.triangleCount(); ++t) {
        const double* p = &r.mesh.vertices[9 * t];
        for (int k = 0; k < 3; ++k)
            for (int i = 0; i < 3; ++i) {
                mn[i] = std::min(mn[i], p[3 * k + i]);
                mx[i] = std::max(mx[i], p[3 * k + i]);
            }
        const double ux = p[3] - p[0], uy = p[4] - p[1], uz = p[5] - p[2];
        const double vx = p[6] - p[0], vy = p[7] - p[1], vz = p[8] - p[2];
        const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
        if (std::sqrt(cx * cx + cy * cy + cz * cz) == 0.0) ++degenerate;
    }
    if (degenerate)
        r.warnings.push_back(std::to_string(degenerate) + " degenerate (zero-area) triangle(s) found; they are displayed but have no visible area.");
    r.ok = true;
    return r;
}

}  // namespace wb
