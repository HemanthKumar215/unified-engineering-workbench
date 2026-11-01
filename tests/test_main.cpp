// Self-contained test runner (no external framework): positive, negative and numerical tests.
// Usage: wb_tests <path-to-tests/data>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "backprop.h"
#include "batch.h"
#include "fea1d.h"
#include "hardware.h"
#include "nodal_csv.h"
#include "stl_parser.h"

namespace {

int g_failures = 0;
int g_checks = 0;
std::string g_dataDir = "tests/data";

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_failures;                                                             \
            std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                             \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                         \
    do {                                                                              \
        ++g_checks;                                                                   \
        const double va = (a), vb = (b);                                              \
        if (!(std::abs(va - vb) <= (tol))) {                                          \
            ++g_failures;                                                             \
            std::printf("    FAIL %s:%d  %s=%.12g vs %s=%.12g (tol %g)\n", __FILE__, __LINE__, #a, va, #b, vb, \
                        static_cast<double>(tol));                                    \
        }                                                                             \
    } while (0)

std::string readFile(const std::string& name) {
    std::ifstream f(g_dataDir + "/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

// ---------------- FEA ----------------

void testFeaNodalExactness() {
    // For constant k, A, q, linear elements reproduce the exact solution at the nodes.
    wb::RodProblem p;
    for (int n : {10, 20, 40, 80}) {
        const auto s = wb::solveRod(p, n);
        CHECK(static_cast<int>(s.T_C.size()) == n + 1);
        CHECK_NEAR(s.T_C.front(), 100.0, 0.0);
        CHECK_NEAR(s.T_C.back(), 20.0, 0.0);
        CHECK(wb::computeErrors(p, s).maxNodalError_C < 1e-9);
    }
}

void testFeaMidpointErrorFormula() {
    // Midpoint interpolation error of the quadratic exact solution is q h^2 / (8 k).
    wb::RodProblem p;
    for (int n : {10, 20, 40, 80}) {
        const double h = p.length_m / n;
        const auto e = wb::computeErrors(p, wb::solveRod(p, n));
        CHECK_NEAR(e.maxMidpointError_C, p.q_W_m3 * h * h / (8.0 * p.k_W_mK), 1e-9);
    }
}

void testFeaSecondOrderConvergence() {
    wb::RodProblem p;
    const auto e10 = wb::computeErrors(p, wb::solveRod(p, 10));
    const auto e20 = wb::computeErrors(p, wb::solveRod(p, 20));
    const auto e80 = wb::computeErrors(p, wb::solveRod(p, 80));
    CHECK_NEAR(wb::observedOrder(e10, e20), 2.0, 1e-6);
    CHECK_NEAR(wb::observedOrder(e20, e80), 2.0, 1e-6);
}

void testFeaZeroSourceIsLinear() {
    wb::RodProblem p;
    p.q_W_m3 = 0;
    const auto s = wb::solveRod(p, 7);
    for (std::size_t i = 0; i < s.x_m.size(); ++i) CHECK_NEAR(s.T_C[i], 100.0 - 80.0 * s.x_m[i], 1e-10);
}

void testFeaSingleElement() {
    wb::RodProblem p;
    const auto s = wb::solveRod(p, 1);
    CHECK(s.T_C.size() == 2);
    CHECK_NEAR(wb::interpolate(s, 0.5), 60.0, 1e-12);
}

void testFeaEnergyBalance() {
    // Heat leaving both ends must equal heat generated: q A L.
    wb::RodProblem p;
    const int n = 40;
    const auto s = wb::solveRod(p, n);
    const double h = p.length_m / n, kA = p.k_W_mK * p.area_m2, qAh2 = p.q_W_m3 * p.area_m2 * h / 2;
    // Reaction forces from the first and last element equations.
    const double R0 = kA / h * (s.T_C[0] - s.T_C[1]) - qAh2;
    const double RL = kA / h * (s.T_C[n] - s.T_C[n - 1]) - qAh2;
    CHECK_NEAR(R0 + RL, -p.q_W_m3 * p.area_m2 * p.length_m, 1e-10);
}

void testFeaValidation() {
    wb::RodProblem p;
    CHECK(!wb::validate(p, 0).empty());
    p.k_W_mK = -1;
    CHECK(!wb::validate(p, 10).empty());
    p = wb::RodProblem{};
    p.q_W_m3 = NAN;
    CHECK(!wb::validate(p, 10).empty());
    p = wb::RodProblem{};
    p.T0_C = -300;
    CHECK(!wb::validate(p, 10).empty());
    bool threw = false;
    try {
        wb::solveRod(p, 10);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// ---------------- STL ----------------

void testStlValid() {
    const auto r = wb::parseAsciiStl(readFile("valid_tetra.stl"));
    CHECK(r.ok);
    CHECK(r.mesh.triangleCount() == 4);
    CHECK(r.mesh.solidName == "tetra");
    CHECK_NEAR(r.mesh.bboxMax[0], 1.0, 0);
}

void testStlExampleData() {
    std::ifstream f(g_dataDir + "/../../data/heatsink_bar.stl", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const auto r = wb::parseAsciiStl(ss.str());
    CHECK(r.ok);
    CHECK(r.mesh.triangleCount() > 12);
}

void testStlNegative() {
    struct Bad {
        const char* file;
        const char* expect;
    };
    const Bad cases[] = {
        {"empty.stl", "empty"},
        {"whitespace.stl", "whitespace"},
        {"nan_vertex.stl", "not a finite number"},
        {"truncated.stl", "endsolid"},
        {"two_vertices.stl", "3 vertices"},
        {"no_triangles.stl", "no triangles"},
        {"not_stl.stl", "Expected 'solid'"},
    };
    for (const auto& c : cases) {
        const auto r = wb::parseAsciiStl(readFile(c.file));
        CHECK(!r.ok);
        if (r.error.find(c.expect) == std::string::npos) {
            ++g_failures;
            std::printf("    FAIL %s: error '%s' lacks '%s'\n", c.file, r.error.c_str(), c.expect);
        }
    }
    CHECK(!wb::parseAsciiStl(std::string("solid x\0\0\0", 10)).ok);  // binary-looking data
}

// ---------------- CSV ----------------

void testCsvValid() {
    const auto r = wb::parseNodalCsv(readFile("valid_nodes.csv"));
    CHECK(r.ok);
    CHECK(r.field.size() == 4);
    CHECK_NEAR(r.field.tMin, 20.0, 0);
    CHECK_NEAR(r.field.tMax, 145.0, 0);
}

void testCsvExampleData() {
    std::ifstream f(g_dataDir + "/../../data/synthetic_rod_nodal_temperatures.csv", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const auto r = wb::parseNodalCsv(ss.str());
    CHECK(r.ok);
    CHECK(r.field.size() > 100);
}

void testCsvNegative() {
    struct Bad {
        const char* file;
        const char* expect;
    };
    const Bad cases[] = {
        {"empty.csv", "empty"},
        {"header_only.csv", "no data rows"},
        {"bad_header.csv", "Header column"},
        {"nan_temp.csv", "not finite"},
        {"inf_coord.csv", "not finite"},
        {"missing_col.csv", "Expected 5 values"},
        {"text_value.csv", "not a number"},
        {"duplicate_id.csv", "Duplicate node_id"},
        {"below_abs_zero.csv", "absolute zero"},
    };
    for (const auto& c : cases) {
        const auto r = wb::parseNodalCsv(readFile(c.file));
        CHECK(!r.ok);
        if (r.error.find(c.expect) == std::string::npos) {
            ++g_failures;
            std::printf("    FAIL %s: error '%s' lacks '%s'\n", c.file, r.error.c_str(), c.expect);
        }
    }
}

// ---------------- Batch / parallel ----------------

void testBatchReproducible() {
    const auto a = wb::generateBatch(42, 16), b = wb::generateBatch(42, 16), c = wb::generateBatch(43, 16);
    for (int i = 0; i < 16; ++i) {
        CHECK(a[i].k_W_mK == b[i].k_W_mK && a[i].q_W_m3 == b[i].q_W_m3);
        CHECK(a[i].k_W_mK > 0 && a[i].q_W_m3 > 0);
    }
    CHECK(a[0].k_W_mK != c[0].k_W_mK);
}

void testParallelMatchesSerial() {
    const auto cases = wb::generateBatch(7, 37);
    const auto ref = wb::solveSerial(cases, 500);
    for (int w : {1, 2, 3, 4}) {
        wb::WorkerPool pool(w);
        const auto par = wb::solveParallel(pool, cases, 500);
        CHECK(wb::maxAbsDifference(ref, par) <= 1e-8);
        // Deterministic input order: case i must equal an independent solve of case i.
        CHECK(par[11] == wb::solveRod(cases[11], 500).T_C);
    }
}

void testPoolReuseAndExceptions() {
    wb::WorkerPool pool(3);
    std::atomic<int> sum{0};
    for (int round = 0; round < 5; ++round) pool.parallelFor(100, [&](std::size_t i) { sum += static_cast<int>(i); });
    CHECK(sum == 5 * 4950);
    bool threw = false;
    try {
        pool.parallelFor(10, [](std::size_t i) {
            if (i == 3) throw std::runtime_error("boom");
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    pool.parallelFor(4, [&](std::size_t) { ++sum; });  // pool still usable afterwards
    CHECK(sum == 5 * 4950 + 4);
}

void testCancellation() {
    const auto cases = wb::generateBatch(1, 50);
    std::atomic<bool> cancel{true};
    bool threw = false;
    try {
        wb::WorkerPool pool(2);
        wb::solveParallel(pool, cases, 100, &cancel);
    } catch (const wb::Cancelled&) {
        threw = true;
    }
    CHECK(threw);
}

void testBenchmarkSmall() {
    wb::BatchConfig cfg;
    cfg.cases = 8;
    cfg.elements = 200;
    cfg.repetitions = 2;
    cfg.maxWorkers = 2;
    const auto rep = wb::runBenchmark(cfg);
    CHECK(rep.agreementOk);
    CHECK(!rep.pool.empty() && rep.pool.front().workers == 1);
    CHECK(rep.pool.front().seconds.size() == 2);
    CHECK_NEAR(rep.pool.front().speedup, 1.0, 1e-12);
    wb::BatchConfig bad = cfg;
    bad.cases = 0;
    CHECK(!wb::validate(bad).empty());
}

// ---------------- Hardware mocks ----------------

void testDeviceLifecycle() {
    wb::DeviceManager m;
    CHECK(m.discover().size() == 3);
    for (const auto& id : m.ids()) {
        CHECK(m.info(id).simulated);
        wb::Telemetry t;
        CHECK(!m.telemetry(id, t).ok);  // not connected yet
        CHECK(m.connect(id).ok);
        CHECK(m.state(id) == wb::LinkState::Connected);
        CHECK(m.telemetry(id, t).ok);
        CHECK(t.sequence == 1 && !t.fields.empty());
        CHECK(m.disconnect(id).ok);
        CHECK(m.state(id) == wb::LinkState::Disconnected);
    }
    CHECK(!m.connect("nope").ok);
}

void testDeviceTimeoutRecovery() {
    wb::DeviceManager m;
    m.discover();
    m.setFault("fpga0", wb::Fault::TimeoutOnce);
    CHECK(!m.connect("fpga0", 1, 10).ok);
    CHECK(m.state("fpga0") == wb::LinkState::Error);
    CHECK(m.connect("fpga0", 1, 10).ok);  // one-shot fault consumed -> recovers
    m.setFault("mcu0", wb::Fault::TimeoutOnce);
    CHECK(m.connect("mcu0", 3, 10).ok);   // retry policy recovers within one call
    m.setFault("jtag0", wb::Fault::TimeoutAlways);
    CHECK(!m.connect("jtag0", 2, 10).ok);
    m.setFault("jtag0", wb::Fault::None);
    CHECK(m.connect("jtag0", 1, 10).ok);
}

// ---------------- Backprop ----------------

void testBackpropValues() {
    const wb::Params p;
    const auto f = wb::forward(p, 1, 2);
    CHECK_NEAR(f.h, std::tanh(0.5), 1e-15);
    CHECK_NEAR(f.J, 0.5 * (std::tanh(0.5) - 2) * (std::tanh(0.5) - 2), 1e-15);
    const auto g = wb::backward(p, 1, 2);
    CHECK_NEAR(g.dc, std::tanh(0.5) - 2, 1e-15);
    CHECK(g.dw < 0 && g.db < 0 && g.dv < 0 && g.dc < 0);  // all parameters should increase
    CHECK_NEAR(g.dw, g.db, 1e-15);                         // x = 1
}

void testBackpropGradientCheck() {
    const wb::Params p;
    const auto a = wb::backward(p, 1, 2), n = wb::numericalGradients(p, 1, 2, 1e-6);
    CHECK_NEAR(a.dw, n.dw, 1e-5);
    CHECK_NEAR(a.db, n.db, 1e-5);
    CHECK_NEAR(a.dv, n.dv, 1e-5);
    CHECK_NEAR(a.dc, n.dc, 1e-5);
}

void testBackpropTrainingDecreases() {
    const auto loss = wb::train(wb::Params{}, 1, 2, 0.1, 100);
    CHECK(loss.size() == 101);
    CHECK(loss.back() < loss.front() * 1e-3);
    for (std::size_t i = 1; i < loss.size(); ++i) CHECK(loss[i] <= loss[i - 1] + 1e-15);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) g_dataDir = argv[1];
    const std::vector<TestCase> tests = {
        {"fea: nodal exactness N=10..80", testFeaNodalExactness},
        {"fea: midpoint error = q h^2 / 8k", testFeaMidpointErrorFormula},
        {"fea: observed order 2", testFeaSecondOrderConvergence},
        {"fea: q=0 gives linear profile", testFeaZeroSourceIsLinear},
        {"fea: single element", testFeaSingleElement},
        {"fea: energy balance", testFeaEnergyBalance},
        {"fea: input validation", testFeaValidation},
        {"stl: valid file", testStlValid},
        {"stl: bundled example", testStlExampleData},
        {"stl: malformed files rejected", testStlNegative},
        {"csv: valid file", testCsvValid},
        {"csv: bundled example", testCsvExampleData},
        {"csv: malformed files rejected", testCsvNegative},
        {"batch: reproducible generator", testBatchReproducible},
        {"batch: parallel == serial, ordered", testParallelMatchesSerial},
        {"batch: pool reuse + exceptions", testPoolReuseAndExceptions},
        {"batch: cancellation", testCancellation},
        {"batch: small benchmark", testBenchmarkSmall},
        {"hw: lifecycle of 3 device types", testDeviceLifecycle},
        {"hw: timeout + recovery", testDeviceTimeoutRecovery},
        {"bp: forward/backward values", testBackpropValues},
        {"bp: central-difference check", testBackpropGradientCheck},
        {"bp: loss decreases", testBackpropTrainingDecreases},
    };
    int failedTests = 0;
    for (const auto& t : tests) {
        const int before = g_failures;
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::printf("    EXCEPTION %s\n", e.what());
        }
        const bool pass = g_failures == before;
        failedTests += pass ? 0 : 1;
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", t.name);
    }
    std::printf("\n%zu tests, %d failed, %d checks\n", tests.size(), failedTests, g_checks);
    return failedTests == 0 ? 0 : 1;
}
