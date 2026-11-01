#include "fea1d.h"

#include <cmath>
#include <stdexcept>

namespace wb {

namespace {
constexpr int kMaxElements = 10'000'000;

bool positiveFinite(double v) { return std::isfinite(v) && v > 0.0; }
}  // namespace

std::string validate(const RodProblem& p, int elements) {
    if (!positiveFinite(p.length_m)) return "Length L must be a positive, finite number of metres.";
    if (!positiveFinite(p.area_m2)) return "Area A must be a positive, finite number of square metres.";
    if (!positiveFinite(p.k_W_mK)) return "Conductivity k must be positive and finite (W/(m K)).";
    if (!std::isfinite(p.q_W_m3)) return "Heat generation q must be a finite number (W/m^3).";
    if (!std::isfinite(p.T0_C) || !std::isfinite(p.TL_C)) return "Boundary temperatures must be finite (degC).";
    if (p.T0_C < -273.15 || p.TL_C < -273.15) return "Boundary temperatures cannot be below absolute zero (-273.15 degC).";
    if (elements < 1) return "Number of elements N must be at least 1.";
    if (elements > kMaxElements) return "Number of elements N must not exceed 10,000,000.";
    return {};
}

FeaSolution solveRod(const RodProblem& p, int elements) {
    const std::string err = validate(p, elements);
    if (!err.empty()) throw std::invalid_argument(err);

    const int n = elements;
    const int nodes = n + 1;
    const double h = p.length_m / n;
    const double ke = p.k_W_mK * p.area_m2 / h;   // element conductance [W/K]
    const double fe = p.q_W_m3 * p.area_m2 * h / 2.0;  // nodal share of generated heat [W]

    // Global tridiagonal system: lower (a), diagonal (d), upper (c), right-hand side (f).
    std::vector<double> a(nodes, 0.0), d(nodes, 0.0), c(nodes, 0.0), f(nodes, 0.0);
    for (int e = 0; e < n; ++e) {
        const int i = e, j = e + 1;
        d[i] += ke;
        d[j] += ke;
        c[i] -= ke;  // K(i, j)
        a[j] -= ke;  // K(j, i)
        f[i] += fe;
        f[j] += fe;
    }

    // Dirichlet BCs by elimination: move the known column to the RHS, then replace
    // the constrained row with the identity. This keeps the reduced system symmetric.
    FeaSolution s;
    s.elements = n;
    s.x_m.resize(nodes);
    s.T_C.assign(nodes, 0.0);
    for (int i = 0; i < nodes; ++i) s.x_m[i] = p.length_m * static_cast<double>(i) / n;

    if (nodes == 2) {  // single element: both DOFs are prescribed
        s.T_C[0] = p.T0_C;
        s.T_C[1] = p.TL_C;
        return s;
    }
    f[1] -= a[1] * p.T0_C;
    a[1] = 0.0;
    f[nodes - 2] -= c[nodes - 2] * p.TL_C;
    c[nodes - 2] = 0.0;

    // Thomas algorithm on the interior unknowns 1..nodes-2.
    const int first = 1, last = nodes - 2;
    std::vector<double> cp(nodes, 0.0), fp(nodes, 0.0);
    cp[first] = c[first] / d[first];
    fp[first] = f[first] / d[first];
    for (int i = first + 1; i <= last; ++i) {
        const double m = d[i] - a[i] * cp[i - 1];
        cp[i] = c[i] / m;
        fp[i] = (f[i] - a[i] * fp[i - 1]) / m;
    }
    s.T_C[last] = fp[last];
    for (int i = last - 1; i >= first; --i) s.T_C[i] = fp[i] - cp[i] * s.T_C[i + 1];

    s.T_C[0] = p.T0_C;
    s.T_C[nodes - 1] = p.TL_C;
    return s;
}

double exactTemperature(const RodProblem& p, double x) {
    const double L = p.length_m;
    return p.T0_C + (p.TL_C - p.T0_C) * x / L + p.q_W_m3 / (2.0 * p.k_W_mK) * x * (L - x);
}

double interpolate(const FeaSolution& s, double x) {
    const int n = s.elements;
    const double L = s.x_m.back();
    if (x <= 0.0) return s.T_C.front();
    if (x >= L) return s.T_C.back();
    const double h = L / n;
    int e = static_cast<int>(x / h);
    if (e >= n) e = n - 1;
    const double xi = (x - s.x_m[e]) / h;  // local coordinate in [0, 1]
    return (1.0 - xi) * s.T_C[e] + xi * s.T_C[e + 1];
}

ErrorReport computeErrors(const RodProblem& p, const FeaSolution& s) {
    ErrorReport r;
    r.elements = s.elements;
    r.h_m = p.length_m / s.elements;
    for (std::size_t i = 0; i < s.x_m.size(); ++i)
        r.maxNodalError_C = std::max(r.maxNodalError_C, std::abs(s.T_C[i] - exactTemperature(p, s.x_m[i])));
    for (int e = 0; e < s.elements; ++e) {
        const double xm = 0.5 * (s.x_m[e] + s.x_m[e + 1]);
        // Midpoint value comes from the FE shape functions, not from the exact formula.
        const double feMid = 0.5 * (s.T_C[e] + s.T_C[e + 1]);
        r.maxMidpointError_C = std::max(r.maxMidpointError_C, std::abs(feMid - exactTemperature(p, xm)));
    }
    return r;
}

double observedOrder(const ErrorReport& coarse, const ErrorReport& fine) {
    return std::log(coarse.maxMidpointError_C / fine.maxMidpointError_C) / std::log(coarse.h_m / fine.h_m);
}

}  // namespace wb
