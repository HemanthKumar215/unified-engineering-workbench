#include "api.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "backprop.h"
#include "fea1d.h"
#include "json_writer.h"
#include "nodal_csv.h"
#include "stl_parser.h"
#include "sysinfo.h"

namespace wb {

namespace {

// ---------------- request helpers ----------------

struct ParamError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

double numParam(const HttpRequest& r, const std::string& key, double fallback) {
    const auto it = r.query.find(key);
    if (it == r.query.end() || it->second.empty()) return fallback;
    char* end = nullptr;
    const double v = std::strtod(it->second.c_str(), &end);
    if (*end != '\0' || !std::isfinite(v)) throw ParamError("Parameter '" + key + "' must be a finite number (got '" + it->second + "').");
    return v;
}

int intParam(const HttpRequest& r, const std::string& key, int fallback) {
    const double v = numParam(r, key, fallback);
    if (v != std::floor(v) || std::abs(v) > 2e9) throw ParamError("Parameter '" + key + "' must be a whole number.");
    return static_cast<int>(v);
}

std::string strParam(const HttpRequest& r, const std::string& key) {
    const auto it = r.query.find(key);
    return it == r.query.end() ? std::string() : it->second;
}

RodProblem rodFrom(const HttpRequest& r) {
    RodProblem p;
    p.length_m = numParam(r, "L", p.length_m);
    p.area_m2 = numParam(r, "A", p.area_m2);
    p.k_W_mK = numParam(r, "k", p.k_W_mK);
    p.q_W_m3 = numParam(r, "q", p.q_W_m3);
    p.T0_C = numParam(r, "T0", p.T0_C);
    p.TL_C = numParam(r, "TL", p.TL_C);
    return p;
}

std::vector<int> meshList(const HttpRequest& r) {
    std::string s = strParam(r, "meshes");
    if (s.empty()) s = "10,20,40,80";
    std::vector<int> out;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        char* end = nullptr;
        const long v = std::strtol(tok.c_str(), &end, 10);
        if (tok.empty() || *end != '\0' || v < 1 || v > 100000)
            throw ParamError("Each mesh size must be a whole number between 1 and 100,000 (got '" + tok + "').");
        out.push_back(static_cast<int>(v));
    }
    if (out.empty() || out.size() > 8) throw ParamError("Give between 1 and 8 mesh sizes.");
    return out;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

HttpResponse csvDownload(const std::string& body, const std::string& filename) {
    HttpResponse r;
    r.contentType = "text/csv; charset=utf-8";
    r.body = body;
    r.headers["Content-Disposition"] = "attachment; filename=\"" + filename + "\"";
    return r;
}

// ---------------- Geometry / results (Task B) ----------------

HttpResponse stlResponse(const StlResult& r, const std::string& source) {
    if (!r.ok) return HttpResponse::error(422, r.error);
    JsonWriter w;
    w.beginObject().field("ok", true).field("source", source).field("name", r.mesh.solidName)
        .field("triangles", r.mesh.triangleCount());
    w.key("bboxMin").numberArray({r.mesh.bboxMin[0], r.mesh.bboxMin[1], r.mesh.bboxMin[2]});
    w.key("bboxMax").numberArray({r.mesh.bboxMax[0], r.mesh.bboxMax[1], r.mesh.bboxMax[2]});
    w.key("warnings").beginArray();
    for (const auto& s : r.warnings) w.value(s);
    w.endArray();
    w.key("vertices").numberArray(r.mesh.vertices, 7);
    w.endObject();
    return HttpResponse::json(w.str());
}

HttpResponse nodalResponse(const NodalResult& r, const std::string& source) {
    if (!r.ok) return HttpResponse::error(422, r.error);
    const auto& f = r.field;
    JsonWriter w;
    w.beginObject().field("ok", true).field("source", source).field("nodes", f.size())
        .field("tMin", f.tMin).field("tMax", f.tMax).field("units", "coordinates m, temperature degC");
    w.key("bboxMin").numberArray({f.bboxMin[0], f.bboxMin[1], f.bboxMin[2]});
    w.key("bboxMax").numberArray({f.bboxMax[0], f.bboxMax[1], f.bboxMax[2]});
    w.key("warnings").beginArray();
    for (const auto& s : r.warnings) w.value(s);
    w.endArray();
    w.key("ids").beginArray();
    for (auto id : f.nodeId) w.value(id);
    w.endArray();
    w.key("xyz").numberArray(f.xyz, 9);
    w.key("temperature").numberArray(f.temperature, 9);
    w.endObject();
    return HttpResponse::json(w.str());
}

// ---------------- FEA (Task C) ----------------

void writeStudy(JsonWriter& w, const RodProblem& p, const std::vector<int>& meshes) {
    w.beginObject().field("ok", true);
    w.key("problem").beginObject().field("L_m", p.length_m).field("A_m2", p.area_m2).field("k_W_mK", p.k_W_mK)
        .field("q_W_m3", p.q_W_m3).field("T0_C", p.T0_C).field("TL_C", p.TL_C).endObject();
    std::vector<double> ex, ey;
    for (int i = 0; i <= 200; ++i) {
        const double x = p.length_m * i / 200.0;
        ex.push_back(x);
        ey.push_back(exactTemperature(p, x));
    }
    w.key("exact").beginObject().key("x").numberArray(ex).key("T").numberArray(ey).endObject();
    w.key("meshes").beginArray();
    ErrorReport prev{};
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        const auto s = solveRod(p, meshes[i]);
        const auto e = computeErrors(p, s);
        w.beginObject().field("elements", meshes[i]).field("h_m", e.h_m).field("maxNodalError_C", e.maxNodalError_C)
            .field("maxMidpointError_C", e.maxMidpointError_C);
        if (i > 0 && prev.maxMidpointError_C > 0 && e.maxMidpointError_C > 0 && prev.h_m != e.h_m)
            w.field("observedOrder", observedOrder(prev, e));
        else
            w.key("observedOrder").null();
        if (meshes[i] <= 5000) w.key("x").numberArray(s.x_m).key("T").numberArray(s.T_C);
        w.endObject();
        prev = e;
    }
    w.endArray().endObject();
}

std::string studyCsv(const RodProblem& p, const std::vector<int>& meshes) {
    std::ostringstream o;
    o << "# L_m=" << formatNumber(p.length_m) << " A_m2=" << formatNumber(p.area_m2) << " k_W_mK=" << formatNumber(p.k_W_mK)
      << " q_W_m3=" << formatNumber(p.q_W_m3) << " T0_C=" << formatNumber(p.T0_C) << " TL_C=" << formatNumber(p.TL_C) << "\n";
    o << "elements,node,x_m,T_fe_C,T_exact_C,abs_error_C\n";
    for (int n : meshes) {
        const auto s = solveRod(p, n);
        for (std::size_t j = 0; j < s.x_m.size(); ++j) {
            const double te = exactTemperature(p, s.x_m[j]);
            o << n << ',' << j << ',' << formatNumber(s.x_m[j]) << ',' << formatNumber(s.T_C[j]) << ',' << formatNumber(te)
              << ',' << formatNumber(std::abs(s.T_C[j] - te)) << "\n";
        }
    }
    o << "\nelements,h_m,max_nodal_error_C,max_midpoint_error_C\n";
    for (int n : meshes) {
        const auto e = computeErrors(p, solveRod(p, n));
        o << n << ',' << formatNumber(e.h_m) << ',' << formatNumber(e.maxNodalError_C) << ','
          << formatNumber(e.maxMidpointError_C) << "\n";
    }
    return o.str();
}

// ---------------- Benchmark (Task D) ----------------

void writeTiming(JsonWriter& w, const WorkerTiming& t) {
    std::vector<double> secs(t.seconds.begin(), t.seconds.end());
    w.beginObject().field("workers", t.workers).field("median_s", t.median).field("speedup", t.speedup)
        .field("efficiency", t.efficiency).field("maxAbsDiff_C", t.maxAbsDiffVsSerial);
    w.key("seconds").numberArray(secs);
    w.endObject();
}

void writeSystem(JsonWriter& w, const AppContext& ctx) {
    const auto si = querySystemInfo();
    w.beginObject().field("cpu", si.cpuName).field("logicalCpus", si.logicalCpus).field("physicalCores", si.physicalCores)
        .field("ramGiB", si.ramGiB).field("os", si.os).field("compiler", si.compiler).field("optFlags", si.optFlags)
        .field("launchToListen_s", ctx.listenReady_s).field("launchToUiReady_s", ctx.uiReady_s.load()).endObject();
}

HttpResponse benchStatus(AppContext& ctx) {
    const auto s = ctx.bench.snapshot();
    JsonWriter w;
    w.beginObject().field("ok", true).field("jobId", s.id).field("state", toString(s.state)).field("progress", s.progress)
        .field("phase", s.phase).field("error", s.error).field("elapsed_s", s.elapsed_s);
    w.key("system");
    writeSystem(w, ctx);
    if (s.lastReport) {
        const auto& r = *s.lastReport;
        w.key("report").beginObject().field("agreementOk", r.agreementOk).field("logicalCpus", r.logicalCpus);
        w.key("config").beginObject().field("seed", static_cast<long long>(r.config.seed)).field("cases", r.config.cases)
            .field("elements", r.config.elements).field("repetitions", r.config.repetitions)
            .field("maxWorkers", r.config.maxWorkers).field("warmupRuns", 1).endObject();
        w.key("serial");
        writeTiming(w, r.serial);
        w.key("pool").beginArray();
        for (const auto& t : r.pool) writeTiming(w, t);
        w.endArray().endObject();
    }
    w.endObject();
    return HttpResponse::json(w.str());
}

HttpResponse benchCsv(AppContext& ctx) {
    const auto s = ctx.bench.snapshot();
    if (!s.lastReport) return HttpResponse::error(409, "No completed benchmark yet. Run one first.");
    const auto& r = *s.lastReport;
    const auto si = querySystemInfo();
    std::ostringstream o;
    o << "# cpu=" << si.cpuName << " physical=" << si.physicalCores << " logical=" << si.logicalCpus
      << " ram_GiB=" << formatNumber(si.ramGiB, 4) << " compiler=" << si.compiler << " flags=" << si.optFlags << "\n";
    o << "# seed=" << r.config.seed << " cases=" << r.config.cases << " elements=" << r.config.elements
      << " warmup=1 reps=" << r.config.repetitions << " agreement_ok=" << (r.agreementOk ? "true" : "false") << "\n";
    o << "mode,workers,repetition,seconds,median_s,speedup,efficiency,max_abs_diff_C\n";
    auto rows = [&](const char* mode, const WorkerTiming& t) {
        for (std::size_t i = 0; i < t.seconds.size(); ++i)
            o << mode << ',' << t.workers << ',' << i + 1 << ',' << formatNumber(t.seconds[i], 9) << ','
              << formatNumber(t.median, 9) << ',' << formatNumber(t.speedup, 6) << ',' << formatNumber(t.efficiency, 6) << ','
              << formatNumber(t.maxAbsDiffVsSerial, 6) << "\n";
    };
    rows("serial", r.serial);
    for (const auto& t : r.pool) rows("pool", t);
    return csvDownload(o.str(), "benchmark.csv");
}

// ---------------- Hardware (Task E) ----------------

void writeDevice(JsonWriter& w, DeviceManager& m, const std::string& id) {
    const auto info = m.info(id);
    w.beginObject().field("id", info.id).field("kind", toString(info.kind)).field("model", info.model)
        .field("transport", info.transport).field("ioVoltage", info.ioVoltage).field("simulated", info.simulated)
        .field("state", toString(m.state(id))).field("fault", toString(m.fault(id)));
    w.key("capabilities").beginArray();
    for (const auto& c : info.capabilities) w.value(c);
    w.endArray().endObject();
}

HttpResponse hwState(AppContext& ctx, const OpResult* op = nullptr, const Telemetry* tel = nullptr) {
    JsonWriter w;
    w.beginObject().field("ok", op ? op->ok : true);
    if (op && !op->ok) w.field("error", op->error);
    w.key("devices").beginArray();
    for (const auto& id : ctx.devices.ids()) writeDevice(w, ctx.devices, id);
    w.endArray();
    if (tel) {
        w.key("telemetry").beginObject().field("deviceId", tel->deviceId).field("sequence", static_cast<long long>(tel->sequence))
            .field("hostTime_s", tel->hostTime_s).field("simulated", true);
        w.key("fields").beginObject();
        for (const auto& kv : tel->fields) w.field(kv.first, kv.second);
        w.endObject().endObject();
    }
    w.key("events").beginArray();
    for (const auto& e : ctx.devices.events())
        w.beginObject().field("t", e.hostTime_s).field("device", e.deviceId).field("level", e.level).field("message", e.message).endObject();
    w.endArray().endObject();
    return HttpResponse::json(w.str());
}

// ---------------- Backprop (Task F) ----------------

HttpResponse backprop(const HttpRequest& req) {
    Params p;
    p.w = numParam(req, "w", p.w);
    p.b = numParam(req, "b", p.b);
    p.v = numParam(req, "v", p.v);
    p.c = numParam(req, "c", p.c);
    const double x = numParam(req, "x", 1.0), y = numParam(req, "y", 2.0), lr = numParam(req, "lr", 0.1);
    const int steps = intParam(req, "steps", 100);
    if (lr <= 0 || lr > 10) throw ParamError("Learning rate must be in (0, 10].");
    if (steps < 1 || steps > 100000) throw ParamError("Steps must be between 1 and 100,000.");

    const auto f = forward(p, x, y);
    const auto g = backward(p, x, y);
    const auto n = numericalGradients(p, x, y, 1e-6);
    const auto p1 = sgdStep(p, g, lr);
    const auto loss = train(p1, x, y, lr, steps);

    JsonWriter w;
    w.beginObject().field("ok", true);
    w.key("forward").beginObject().field("z", f.z).field("h", f.h).field("yHat", f.yHat).field("J", f.J).endObject();
    auto grads = [&](const char* k, const Gradients& gg) {
        w.key(k).beginObject().field("w", gg.dw).field("b", gg.db).field("v", gg.dv).field("c", gg.dc).endObject();
    };
    grads("analytic", g);
    grads("numeric", n);
    const double maxErr = std::max({std::abs(g.dw - n.dw), std::abs(g.db - n.db), std::abs(g.dv - n.dv), std::abs(g.dc - n.dc)});
    w.field("maxAbsGradError", maxErr).field("gradCheckPass", maxErr <= 1e-5);
    w.key("updated").beginObject().field("w", p1.w).field("b", p1.b).field("v", p1.v).field("c", p1.c).endObject();
    w.key("loss").numberArray(loss);
    w.endObject();
    return HttpResponse::json(w.str());
}

// ---------------- Static files ----------------

const char* mimeFor(const std::string& path) {
    auto ends = [&](const char* ext) {
        const std::string e(ext);
        return path.size() >= e.size() && path.compare(path.size() - e.size(), e.size(), e) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js")) return "text/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".png")) return "image/png";
    if (ends(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

HttpResponse staticFile(const AppContext& ctx, std::string path) {
    if (path == "/") path = "/index.html";
    // Reject traversal and anything outside a simple whitelist of characters.
    if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos || path.find(':') != std::string::npos)
        return HttpResponse::error(400, "Invalid path.");
    std::string body;
    if (!readFile(ctx.appDir + path, body)) return HttpResponse::error(404, "Not found: " + path);
    HttpResponse r;
    r.contentType = mimeFor(path);
    r.body = std::move(body);
    return r;
}

HttpResponse route(AppContext& ctx, const HttpRequest& req) {
    const std::string& p = req.path;
    const bool get = req.method == "GET", post = req.method == "POST";
    ctx.lastHeartbeatMs = ctx.nowMs();

    if (p.rfind("/api/", 0) != 0) {
        if (!get) return HttpResponse::error(405, "Static files are GET only.");
        return staticFile(ctx, p);
    }

    // ---- lifecycle ----
    if (p == "/api/health") return HttpResponse::json("{\"ok\":true,\"app\":\"Unified Engineering Workbench\",\"version\":\"1.0.0\"}");
    if (p == "/api/heartbeat") {
        ctx.goodbyeAtMs = 0;
        return HttpResponse::json("{\"ok\":true}");
    }
    if (p == "/api/goodbye" && post) {
        ctx.goodbyeAtMs = ctx.nowMs();
        return HttpResponse::json("{\"ok\":true}");
    }
    if (p == "/api/ready" && post) {
        if (ctx.uiReady_s.load() == 0) ctx.uiReady_s = ctx.nowMs() / 1000.0;
        return HttpResponse::json("{\"ok\":true}");
    }
    if (p == "/api/shutdown" && post) {
        ctx.bench.cancel();
        ctx.shutdownRequested = true;
        return HttpResponse::json("{\"ok\":true}");
    }
    if (p == "/api/system") {
        JsonWriter w;
        w.beginObject().field("ok", true).key("system");
        writeSystem(w, ctx);
        w.endObject();
        return HttpResponse::json(w.str());
    }

    // ---- Task B ----
    if (p == "/api/geometry/stl" && post) return stlResponse(parseAsciiStl(req.body), strParam(req, "name"));
    if (p == "/api/results/nodal" && post) return nodalResponse(parseNodalCsv(req.body), strParam(req, "name"));
    if (p == "/api/examples/stl" || p == "/api/examples/nodal") {
        const bool stl = p == "/api/examples/stl";
        const std::string file = stl ? "heatsink_bar.stl" : "synthetic_rod_nodal_temperatures.csv";
        std::string text;
        if (!readFile(ctx.dataDir + "/" + file, text))
            return HttpResponse::error(404, "Example file data/" + file + " is missing next to Launch.exe. Re-extract the ZIP.");
        return stl ? stlResponse(parseAsciiStl(text), "data/" + file) : nodalResponse(parseNodalCsv(text), "data/" + file);
    }

    // ---- Task C ----
    if (p == "/api/fea/study" || p == "/api/fea/csv") {
        const RodProblem prob = rodFrom(req);
        const auto meshes = meshList(req);
        for (int n : meshes) {
            const std::string err = validate(prob, n);
            if (!err.empty()) return HttpResponse::error(400, err);
        }
        if (p == "/api/fea/csv") return csvDownload(studyCsv(prob, meshes), "fea_study.csv");
        JsonWriter w;
        writeStudy(w, prob, meshes);
        return HttpResponse::json(w.str());
    }

    // ---- Task D ----
    if (p == "/api/bench/start" && post) {
        BatchConfig cfg;
        cfg.seed = static_cast<std::uint64_t>(numParam(req, "seed", static_cast<double>(cfg.seed)));
        cfg.cases = intParam(req, "cases", cfg.cases);
        cfg.elements = intParam(req, "elements", cfg.elements);
        cfg.repetitions = intParam(req, "reps", cfg.repetitions);
        cfg.maxWorkers = intParam(req, "workers", cfg.maxWorkers);
        int id = 0;
        const std::string err = ctx.bench.start(cfg, id);
        if (!err.empty()) return HttpResponse::error(ctx.bench.running() ? 409 : 400, err);
        return benchStatus(ctx);
    }
    if (p == "/api/bench/cancel" && post) {
        ctx.bench.cancel();
        return benchStatus(ctx);
    }
    if (p == "/api/bench/status") return benchStatus(ctx);
    if (p == "/api/bench/csv") return benchCsv(ctx);

    // ---- Task E ----
    if (p == "/api/hw/discover" && post) {
        ctx.devices.discover();
        return hwState(ctx);
    }
    if (p == "/api/hw/state") return hwState(ctx);
    if (p.rfind("/api/hw/", 0) == 0 && post) {
        const std::string id = strParam(req, "id");
        if (!ctx.devices.has(id)) return HttpResponse::error(404, "Unknown device '" + id + "'. Click 'Discover devices' first.");
        OpResult op;
        Telemetry tel;
        if (p == "/api/hw/connect") op = ctx.devices.connect(id, intParam(req, "attempts", 1), intParam(req, "timeout", 300));
        else if (p == "/api/hw/disconnect") op = ctx.devices.disconnect(id);
        else if (p == "/api/hw/telemetry") {
            op = ctx.devices.telemetry(id, tel);
            if (op.ok) return hwState(ctx, &op, &tel);
        } else if (p == "/api/hw/fault") {
            const std::string mode = strParam(req, "mode");
            Fault f;
            if (mode == "none") f = Fault::None;
            else if (mode == "timeout_once") f = Fault::TimeoutOnce;
            else if (mode == "timeout_always") f = Fault::TimeoutAlways;
            else return HttpResponse::error(400, "mode must be none, timeout_once or timeout_always.");
            op = ctx.devices.setFault(id, f);
        } else
            return HttpResponse::error(404, "Unknown hardware endpoint.");
        return hwState(ctx, &op);
    }

    // ---- Task F ----
    if (p == "/api/backprop") return backprop(req);

    return HttpResponse::error(404, "Unknown API endpoint " + req.method + " " + p);
}

}  // namespace

HttpResponse handleRequest(AppContext& ctx, const HttpRequest& req) {
    try {
        return route(ctx, req);
    } catch (const ParamError& e) {
        return HttpResponse::error(400, e.what());
    } catch (const std::invalid_argument& e) {
        return HttpResponse::error(400, e.what());
    }
}

}  // namespace wb
