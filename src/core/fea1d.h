// Steady-state 1D thermal finite-element solver for a uniform rod.
//
// Governing equation:  -d/dx( k A dT/dx ) = q A   on 0 < x < L
// Boundary conditions:  T(0) = T0,  T(L) = TL   (both prescribed / Dirichlet)
// Units: x [m], A [m^2], k [W/(m K)], q [W/m^3], T [degC].
// Lateral surfaces are insulated and properties constant, so the problem is truly 1D.
#pragma once
#include <string>
#include <vector>

namespace wb {

struct RodProblem {
    double length_m = 1.0;       // L
    double area_m2 = 1e-4;       // A
    double k_W_mK = 10.0;        // thermal conductivity
    double q_W_m3 = 10000.0;     // uniform volumetric heat generation
    double T0_C = 100.0;         // T(0)
    double TL_C = 20.0;          // T(L)
};

struct FeaSolution {
    int elements = 0;
    std::vector<double> x_m;     // node coordinates, size elements+1
    std::vector<double> T_C;     // nodal temperatures
};

// Scratch buffers for the assembled tridiagonal system. Reusing one per thread avoids
// allocating six vectors per solve, which dominated multi-threaded batch runs (see docs/report.md).
struct FeaWorkspace {
    std::vector<double> a, d, c, f, cp, fp;
};

// Returns an empty string if the problem is physically valid, else a user-facing reason.
std::string validate(const RodProblem& p, int elements);

// Assembles element stiffness kA/h [[1,-1],[-1,1]] and consistent load qAh/2 [1,1],
// imposes both Dirichlet values by eliminating the known DOFs (keeps symmetry),
// and solves the resulting tridiagonal system with the Thomas algorithm.
// Throws std::invalid_argument if validate() fails.
FeaSolution solveRod(const RodProblem& p, int elements);

// Same solve, writing only nodal temperatures into T (resized to elements+1) using `ws`.
void solveRodTemperatures(const RodProblem& p, int elements, FeaWorkspace& ws, std::vector<double>& T);

// Analytical solution for constant k, A, q:  T = T0 + (TL-T0) x/L + q/(2k) x (L-x).
double exactTemperature(const RodProblem& p, double x_m);

// Linear FE interpolation of the nodal solution at position x (0 <= x <= L).
double interpolate(const FeaSolution& s, double x_m);

struct ErrorReport {
    int elements = 0;
    double h_m = 0;
    double maxNodalError_C = 0;     // max |T_fe - T_exact| at nodes
    double maxMidpointError_C = 0;  // max |T_fe - T_exact| at element midpoints
};

ErrorReport computeErrors(const RodProblem& p, const FeaSolution& s);

// Observed order of convergence between two meshes: log(e1/e2) / log(h1/h2).
double observedOrder(const ErrorReport& coarse, const ErrorReport& fine);

}  // namespace wb
