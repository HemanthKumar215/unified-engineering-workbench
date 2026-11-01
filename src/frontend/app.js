// UI wiring: views, validation, status, and calls into the C++ backend through Api.
"use strict";

const $ = sel => document.querySelector(sel);
const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const num = (v, d = 4) => (v === null || v === undefined || Number.isNaN(v)) ? "–" : (+v).toPrecision(d);
const MAX_UPLOAD_BYTES = 64 * 1024 * 1024;

// ---------------- status bar ----------------
function status(text, kind = "ok") {
  $("#statusText").textContent = text;
  $("#statusDot").className = "dot " + kind;
}
function message(el, text, kind) {
  el.innerHTML = `<div class="msg ${kind}">${esc(text)}</div>` + el.innerHTML;
}

// Runs an async action with the triggering button disabled and errors routed to the UI.
async function busy(btn, label, fn) {
  if (btn) btn.disabled = true;
  status(label + "…", "busy");
  try {
    const r = await fn();
    status("Ready.", "ok");
    return r;
  } catch (e) {
    status(e.message, "err");
    throw e;
  } finally {
    if (btn) btn.disabled = false;
  }
}

// ---------------- tabs ----------------
document.querySelectorAll(".tabs button").forEach(b => b.addEventListener("click", () => {
  document.querySelectorAll(".tabs button").forEach(x => x.classList.toggle("active", x === b));
  document.querySelectorAll(".view").forEach(v => v.classList.toggle("active", v.id === "view-" + b.dataset.view));
  window.dispatchEvent(new Event("resize"));
  if (b.dataset.view === "thermal" && lastStudy) drawStudy(lastStudy);
  if (b.dataset.view === "performance" && lastReport) drawBench(lastReport);
  if (b.dataset.view === "backprop" && lastBp) drawBp(lastBp);
  if (b.dataset.view === "geometry") viewer.render();
}));

// ---------------- form helpers ----------------
// Reads numeric inputs, marks invalid ones, and returns null if any are invalid.
function readNumbers(form, rules) {
  const out = {};
  let ok = true;
  for (const [name, rule] of Object.entries(rules)) {
    const input = form.elements[name];
    const v = Number(input.value.trim());
    let err = input.value.trim() === "" || !Number.isFinite(v) ? "must be a number" : null;
    if (!err && rule.int && !Number.isInteger(v)) err = "must be a whole number";
    if (!err && rule.min !== undefined && v < rule.min) err = `must be ≥ ${rule.min}`;
    if (!err && rule.gt !== undefined && v <= rule.gt) err = `must be > ${rule.gt}`;
    if (!err && rule.max !== undefined && v > rule.max) err = `must be ≤ ${rule.max}`;
    input.classList.toggle("invalid", !!err);
    input.title = err ? `${name} ${err}` : "";
    if (err) { ok = false; status(`${input.closest("label").firstChild.textContent.trim()} ${err}.`, "err"); }
    out[name] = v;
  }
  return ok ? out : null;
}

// =====================================================================
// Geometry (Task B)
// =====================================================================
const viewer = new Viewer3D.Viewer($("#viewer"), onPick);
let stlData = null, cloud = null;

function onPick(i, x, y) {
  const tip = $("#nodeTip");
  if (i === null || !cloud) { tip.classList.add("hidden"); return; }
  tip.textContent = `node_id ${cloud.ids[i]}\nx = ${num(cloud.xyz[3 * i], 6)} m\ny = ${num(cloud.xyz[3 * i + 1], 6)} m\n` +
                    `z = ${num(cloud.xyz[3 * i + 2], 6)} m\nT = ${num(cloud.temp[i], 6)} °C`;
  tip.style.left = (x + 14) + "px";
  tip.style.top = (y + 14) + "px";
  tip.classList.remove("hidden");
}

function geoInfo() {
  const el = $("#geoInfo");
  let h = "";
  if (stlData) {
    const u = +$("#stlUnit").value;
    const ext = [0, 1, 2].map(k => ((stlData.bboxMax[k] - stlData.bboxMin[k]) * u).toPrecision(4)).join(" × ");
    h += `<b>STL</b> ${esc(stlData.source)}<br>solid "${esc(stlData.name)}", ${stlData.triangles} triangles<br>extent ${ext} m<br>`;
    for (const w of stlData.warnings) h += `<div class="msg warn">${esc(w)}</div>`;
  }
  if (cloud) {
    h += `<b>Nodal CSV</b> ${esc(cloud.source)}<br>${cloud.ids.length} nodes, T ${num(cloud.tMin, 5)} … ${num(cloud.tMax, 5)} °C<br>`;
    if (/synthetic/i.test(cloud.source)) h += `<div class="msg warn">Synthetic data — not an actual ANSYS export.</div>`;
    for (const w of cloud.warnings) h += `<div class="msg warn">${esc(w)}</div>`;
  }
  el.innerHTML = h;
}

function legend() {
  const el = $("#legend");
  if (!cloud) { el.classList.add("hidden"); return; }
  const stops = [];
  for (let i = 0; i <= 10; i++) stops.push(`rgb(${Viewer3D.colormap(1 - i / 10).join(",")}) ${i * 10}%`);
  const ticks = [];
  for (let i = 0; i <= 4; i++) ticks.push(`<span>${num(cloud.tMax - (cloud.tMax - cloud.tMin) * i / 4, 4)}</span>`);
  el.innerHTML = `<b>Temperature [°C]</b><div class="scale"><div class="bar" style="background:linear-gradient(${stops.join(",")})"></div>` +
                 `<div class="ticks">${ticks.join("")}</div></div>`;
  el.classList.remove("hidden");
}

async function loadText(input) {
  const f = input.files[0];
  if (!f) return null;
  if (f.size > MAX_UPLOAD_BYTES) throw new Error(`${f.name} is ${(f.size / 1048576).toFixed(1)} MB; the tested limit is 64 MB.`);
  if (f.size === 0) throw new Error(`${f.name} is empty (0 bytes).`);
  return { name: f.name, text: await f.text() };
}

function applyStl(d) {
  stlData = d;
  viewer.setMesh(d.vertices, +$("#stlUnit").value);
  geoInfo();
}
function applyCloud(d) {
  cloud = { source: d.source, ids: d.ids, xyz: d.xyz, temp: d.temperature, tMin: d.tMin, tMax: d.tMax, warnings: d.warnings };
  viewer.setCloud(cloud);
  legend();
  geoInfo();
}
function geoError(e) { message($("#geoInfo"), e.message, "err"); }

$("#stlFile").addEventListener("change", e => busy(null, "Parsing STL in C++", async () => {
  const f = await loadText(e.target);
  if (f) applyStl(await Api.post("/api/geometry/stl", { name: f.name }, f.text));
}).catch(geoError).finally(() => { e.target.value = ""; }));
$("#csvFile").addEventListener("change", e => busy(null, "Parsing nodal CSV in C++", async () => {
  const f = await loadText(e.target);
  if (f) applyCloud(await Api.post("/api/results/nodal", { name: f.name }, f.text));
}).catch(geoError).finally(() => { e.target.value = ""; }));
$("#stlExample").addEventListener("click", e => busy(e.target, "Loading example STL",
  async () => applyStl(await Api.get("/api/examples/stl"))).catch(geoError));
$("#csvExample").addEventListener("click", e => busy(e.target, "Loading example CSV",
  async () => applyCloud(await Api.get("/api/examples/nodal"))).catch(geoError));
$("#stlUnit").addEventListener("change", () => { if (stlData) applyStl(stlData); });
$("#showMesh").addEventListener("change", e => { viewer.show.mesh = e.target.checked; viewer.render(); });
$("#showPoints").addEventListener("change", e => { viewer.show.points = e.target.checked; viewer.render(); });
$("#showWire").addEventListener("change", e => { viewer.show.wire = e.target.checked; viewer.render(); });
$("#fitBtn").addEventListener("click", () => viewer.fit());

// =====================================================================
// Thermal analysis (Task C)
// =====================================================================
const FEA_RULES = { L: { gt: 0 }, A: { gt: 0 }, k: { gt: 0 }, q: {}, T0: { min: -273.15 }, TL: { min: -273.15 } };
let lastStudy = null;

function feaParams() {
  const form = $("#feaForm");
  const p = readNumbers(form, FEA_RULES);
  const m = form.elements.meshes;
  const meshes = m.value.split(",").map(s => s.trim()).filter(Boolean);
  const meshOk = meshes.length >= 1 && meshes.length <= 8 && meshes.every(s => /^\d+$/.test(s) && +s >= 1 && +s <= 100000);
  m.classList.toggle("invalid", !meshOk);
  if (!meshOk) { status("Meshes must be 1–8 whole numbers between 1 and 100000, separated by commas.", "err"); return null; }
  return p && { ...p, meshes: meshes.join(",") };
}

function drawStudy(d) {
  const series = [{ label: "exact", x: d.exact.x, y: d.exact.T, color: "#111827", dashed: true }];
  d.meshes.forEach((m, i) => {
    if (m.x) series.push({ label: `FE N=${m.elements}`, x: m.x, y: m.T, color: Plot.COLORS[i], markers: m.elements <= 20, line: true });
  });
  Plot.draw($("#feaPlot"), { title: "Temperature along the rod: FE vs exact", xLabel: "x [m]", yLabel: "T [°C]" }, series);

  const h = d.meshes.map(m => m.h_m), e = d.meshes.map(m => m.maxMidpointError_C);
  const p = d.problem, theory = h.map(hh => Math.abs(p.q_W_m3) * hh * hh / (8 * p.k_W_mK));
  Plot.draw($("#convPlot"), { title: "Mesh convergence (log-log)", xLabel: "element size h [m]",
    yLabel: "max |T_FE − T_exact| at midpoints [°C]", logX: true, logY: true },
    [{ label: "q h²/(8k) theory", x: h, y: theory, color: "#9ca3af", dashed: true },
     { label: "FE midpoints", x: h, y: e, color: "#2563eb", markers: true, line: true }]);

  let t = `<table><tr><th>N</th><th>h [m]</th><th>max nodal err [°C]</th><th>max midpoint err [°C]</th><th>order</th></tr>`;
  for (const m of d.meshes)
    t += `<tr><td>${m.elements}</td><td>${num(m.h_m)}</td><td>${num(m.maxNodalError_C, 2)}</td><td>${num(m.maxMidpointError_C, 5)}</td><td>${num(m.observedOrder, 4)}</td></tr>`;
  t += `</table><p class="hint">Nodal errors are at round-off level (linear elements are nodally exact for this 1D problem), so convergence is measured at element midpoints using FE interpolation.</p>`;
  $("#feaTable").innerHTML = t;
}

$("#feaForm").addEventListener("submit", ev => {
  ev.preventDefault();
  const p = feaParams();
  if (!p) return;
  busy(ev.submitter, "Solving FEA in C++", async () => { lastStudy = await Api.get("/api/fea/study", p); drawStudy(lastStudy); })
    .catch(e => message($("#feaTable"), e.message, "err"));
});
$("#feaReset").addEventListener("click", () => {
  const f = $("#feaForm"), d = { L: 1, A: "1e-4", k: 10, q: 10000, T0: 100, TL: 20, meshes: "10,20,40,80" };
  for (const [k, v] of Object.entries(d)) { f.elements[k].value = v; f.elements[k].classList.remove("invalid"); }
  f.requestSubmit();
});
$("#feaCsv").addEventListener("click", () => { const p = feaParams(); if (p) Api.download("/api/fea/csv", p); });
$("#feaPng").addEventListener("click", () => {
  for (const [id, name] of [["#feaPlot", "fea_profiles.png"], ["#convPlot", "fea_convergence.png"]]) {
    const a = document.createElement("a");
    a.href = $(id).toDataURL("image/png");
    a.download = name;
    a.click();
  }
});

// =====================================================================
// Performance (Task D)
// =====================================================================
const BENCH_RULES = { cases: { int: true, min: 1, max: 100000 }, mesh: { int: true, min: 1, max: 2000000 },
                      reps: { int: true, min: 1, max: 50 }, workers: { int: true, min: 1, max: 256 }, seed: { int: true, min: 0 } };
let lastReport = null, pollTimer = null;

function drawSystem(s) {
  $("#sysInfo").innerHTML = `<table>
    <tr><td>CPU</td><td>${esc(s.cpu)}</td></tr>
    <tr><td>Physical / logical cores</td><td>${s.physicalCores} / ${s.logicalCpus}</td></tr>
    <tr><td>RAM</td><td>${num(s.ramGiB, 3)} GiB</td></tr>
    <tr><td>Compiler</td><td>${esc(s.compiler)}</td></tr>
    <tr><td>Optimisation flags</td><td>${esc(s.optFlags)}</td></tr>
    <tr><td>Launch → listening</td><td>${num(s.launchToListen_s * 1000, 3)} ms</td></tr>
    <tr><td>Launch → UI ready</td><td>${s.launchToUiReady_s ? num(s.launchToUiReady_s, 3) + " s" : "–"}</td></tr></table>`;
}

function drawBench(r) {
  const p = r.pool;
  Plot.draw($("#speedPlot"), { title: `Speedup T1/Tp — ${r.config.cases} cases × ${r.config.elements} elements`,
    xLabel: "worker threads p", yLabel: "speedup T1/Tp [–]", yMin: 0 },
    [{ label: "ideal", x: p.map(t => t.workers), y: p.map(t => t.workers), color: "#9ca3af", dashed: true },
     { label: "measured", x: p.map(t => t.workers), y: p.map(t => t.speedup), color: "#2563eb", markers: true, line: true }]);
  const row = (name, t) => `<tr><td>${name}</td><td>${t.seconds.map(s => s.toFixed(4)).join(", ")}</td><td>${t.median_s.toFixed(4)}</td>` +
    `<td>${t.speedup.toFixed(2)}</td><td>${t.efficiency.toFixed(2)}</td><td>${t.maxAbsDiff_C.toExponential(1)}</td></tr>`;
  let h = `<table><tr><th>Mode</th><th>5 measured runs [s]</th><th>median [s]</th><th>speedup</th><th>efficiency</th><th>max |ΔT| vs serial [°C]</th></tr>`;
  h += row("serial loop", r.serial);
  for (const t of p) h += row(`pool, p=${t.workers}`, t);
  h += `</table><div class="msg ${r.agreementOk ? "ok" : "err"}">Serial/parallel agreement ${r.agreementOk ? "PASSED" : "FAILED"} (tolerance 1e-8 °C).</div>`;
  h += `<p class="hint">Seed ${r.config.seed}, 1 warm-up + ${r.config.repetitions} measured runs per configuration. Timings cover the solves only (case generation, file I/O and rendering excluded). Workers capped at ${r.logicalCpus} logical CPUs.` +
       (r.logicalCpus < 2 ? " <b>Single logical CPU: the parallel path runs but speedup cannot be assessed.</b>" : "") + `</p>`;
  $("#benchTable").innerHTML = h;
}

function benchUi(s) {
  const running = s.state === "running";
  $("#benchStart").disabled = running;
  $("#benchCancel").disabled = !running;
  $("#benchCsv").disabled = !s.report;
  $("#benchBar").style.width = (100 * s.progress).toFixed(1) + "%";
  $("#benchPhase").textContent = running ? `Running: ${s.phase} (${s.elapsed_s.toFixed(1)} s)` :
    s.state === "idle" ? "Idle." : s.state === "cancelled" ? "Cancelled — previous results kept, nothing partial was saved." :
    s.state === "failed" ? "Failed: " + s.error : `Completed in ${s.elapsed_s.toFixed(1)} s.`;
  drawSystem(s.system);
  if (s.report) { lastReport = s.report; drawBench(s.report); }
  if (running) { status("Benchmark running in C++ worker threads…", "busy"); }
  else if (pollTimer) { clearInterval(pollTimer); pollTimer = null; status(s.state === "done" ? "Benchmark complete." : `Benchmark ${s.state}.`, s.state === "failed" ? "err" : "ok"); }
}

async function pollBench() {
  try { benchUi(await Api.get("/api/bench/status")); }
  catch (e) { status(e.message, "err"); }
}

$("#benchForm").addEventListener("submit", async ev => {
  ev.preventDefault();
  const n = readNumbers(ev.target, BENCH_RULES);
  if (!n) return;
  // The input is called "mesh" because a control named "elements" would shadow form.elements.
  const p = { cases: n.cases, elements: n.mesh, reps: n.reps, workers: n.workers, seed: n.seed };
  try {
    benchUi(await Api.post("/api/bench/start", p));
    if (!pollTimer) pollTimer = setInterval(pollBench, 400);
  } catch (e) { status(e.message, "err"); message($("#benchTable"), e.message, "err"); }
});
$("#benchCancel").addEventListener("click", () => Api.post("/api/bench/cancel").then(benchUi).catch(e => status(e.message, "err")));
$("#benchCsv").addEventListener("click", () => Api.download("/api/bench/csv"));

// =====================================================================
// Hardware (Task E)
// =====================================================================
const KIND_LABEL = { microcontroller: "Microcontroller", fpga: "FPGA board", jtag_adapter: "JTAG debug adapter" };
const telemetryById = {};

function drawHw(d) {
  if (d.telemetry) telemetryById[d.telemetry.deviceId] = d.telemetry;
  const busyIds = drawHw.busy || new Set();
  $("#hwDevices").innerHTML = d.devices.length ? d.devices.map(dev => {
    const t = telemetryById[dev.id];
    const tel = t ? `#${t.sequence} @ ${t.hostTime_s.toFixed(2)} s (SIMULATED)\n` + Object.entries(t.fields).map(([k, v]) => `${k}: ${v}`).join("\n") : "no telemetry yet";
    const dis = busyIds.has(dev.id) ? "disabled" : "";
    return `<div class="card" data-id="${dev.id}">
      <h4>${esc(KIND_LABEL[dev.kind] || dev.kind)} <span class="pill ${dev.state}">${dev.state}</span></h4>
      <div class="meta"><b>${esc(dev.id)}</b> · ${esc(dev.model)}</div>
      <div class="meta">Link: ${esc(dev.transport)}</div>
      <div class="meta">I/O: ${esc(dev.ioVoltage)}</div>
      <div class="caps">${dev.capabilities.map(c => `<span>${esc(c)}</span>`).join("")}</div>
      <div class="actions">
        <button data-act="connect" ${dis} ${dev.state === "connected" ? "disabled" : ""}>${dev.state === "error" ? "Retry connect" : "Connect"}</button>
        <button data-act="disconnect" class="ghost" ${dis} ${dev.state === "disconnected" ? "disabled" : ""}>Disconnect</button>
        <button data-act="telemetry" class="ghost" ${dis} ${dev.state !== "connected" ? "disabled" : ""}>Read telemetry</button>
        <select data-act="fault" ${dis} title="Simulated fault injection">
          ${["none", "timeout_once", "timeout_always"].map(f => `<option ${f === dev.fault ? "selected" : ""}>${f}</option>`).join("")}
        </select>
      </div>
      <pre>${esc(tel)}</pre>
    </div>`;
  }).join("") : `<p class="hint">No devices yet. Click “Discover devices”.</p>`;
  $("#hwLog").innerHTML = d.events.slice().reverse().map(e =>
    `<div class="${e.level}">[${e.t.toFixed(2)} s] ${esc(e.device)} ${e.level.toUpperCase()}: ${esc(e.message)}</div>`).join("");
}

async function hwCall(path, params) {
  try {
    const d = await Api.post(path, params, undefined, 60000);
    drawHw(d);
    status("Ready.", "ok");
  } catch (e) {
    if (e.data && e.data.devices) drawHw(e.data);  // failed ops still return the device state
    status(e.message, "err");
  }
}

$("#hwDiscover").addEventListener("click", () => hwCall("/api/hw/discover"));
$("#hwDevices").addEventListener("click", async e => {
  const btn = e.target.closest("button[data-act]");
  if (!btn) return;
  const id = btn.closest(".card").dataset.id, act = btn.dataset.act;
  const timeout = +$("#hwTimeout").value;
  if (!Number.isInteger(timeout) || timeout < 10 || timeout > 5000) { status("Timeout must be a whole number between 10 and 5000 ms.", "err"); return; }
  drawHw.busy = new Set([id]);
  status(`${act} ${id}…`, "busy");
  btn.disabled = true;
  await hwCall("/api/hw/" + act, { id, attempts: $("#hwAttempts").value, timeout });
  drawHw.busy = new Set();
  hwCall("/api/hw/state");
});
$("#hwDevices").addEventListener("change", e => {
  const sel = e.target.closest("select[data-act=fault]");
  if (sel) hwCall("/api/hw/fault", { id: sel.closest(".card").dataset.id, mode: sel.value });
});

// =====================================================================
// Backprop (Task F)
// =====================================================================
let lastBp = null;

// Explains the update direction from the actual signs: θ moves opposite to ∂J/∂θ.
function direction(g, f) {
  const up = ["w", "b", "v", "c"].filter(k => g[k] < 0), down = ["w", "b", "v", "c"].filter(k => g[k] > 0);
  return `ŷ ${f.yHat < 2 ? "is below" : "is above"} the target, so ` +
    (up.length ? `${up.join(", ")} increase (negative gradient)` : "") + (up.length && down.length ? " and " : "") +
    (down.length ? `${down.join(", ")} decrease (positive gradient)` : "") + ".";
}

function drawBp(d) {
  const g = d.analytic, n = d.numeric, u = d.updated, f = d.forward;
  const row = k => `<tr><td>${k}</td><td>${g[k].toFixed(10)}</td><td>${n[k].toFixed(10)}</td><td>${Math.abs(g[k] - n[k]).toExponential(2)}</td><td>${u[k].toFixed(10)}</td></tr>`;
  $("#bpTable").innerHTML =
    `<p>z = ${f.z.toFixed(10)}<br>h = tanh(z) = ${f.h.toFixed(10)}<br>ŷ = ${f.yHat.toFixed(10)}<br>J = ${f.J.toFixed(10)}</p>
     <table><tr><th>θ</th><th>∂J/∂θ (chain rule)</th><th>central diff ε=1e-6</th><th>|error|</th><th>θ after 1 step</th></tr>
     ${["w", "b", "v", "c"].map(row).join("")}</table>
     <div class="msg ${d.gradCheckPass ? "ok" : "err"}">Gradient check ${d.gradCheckPass ? "PASSED" : "FAILED"}: max |error| = ${d.maxAbsGradError.toExponential(2)} (limit 1e-5)</div>
     <p class="hint">${direction(g, f)} Loss after ${d.loss.length - 1} further steps: ${d.loss[d.loss.length - 1].toExponential(3)}.</p>`;
  Plot.draw($("#lossPlot"), { title: "Loss J over subsequent gradient-descent steps (log scale)", xLabel: "step", yLabel: "J [–]", logY: true },
    [{ label: "J", x: d.loss.map((_, i) => i), y: d.loss, color: "#2563eb" }]);
}

$("#bpForm").addEventListener("submit", ev => {
  ev.preventDefault();
  const p = readNumbers(ev.target, { w: {}, b: {}, v: {}, c: {}, lr: { gt: 0, max: 10 }, steps: { int: true, min: 1, max: 100000 } });
  if (!p) return;
  busy(ev.submitter, "Computing in C++", async () => { lastBp = await Api.get("/api/backprop", p); drawBp(lastBp); })
    .catch(e => message($("#bpTable"), e.message, "err"));
});

// =====================================================================
// Lifecycle
// =====================================================================
$("#quitBtn").addEventListener("click", async () => {
  if (!confirm("Stop the workbench backend? Running benchmarks are cancelled.")) return;
  try { await Api.post("/api/shutdown"); } catch (_) { /* already gone */ }
  document.body.innerHTML = "<p style='padding:40px;font-size:16px'>The workbench has stopped. You can close this tab.</p>";
});
window.addEventListener("pagehide", () => navigator.sendBeacon("/api/goodbye"));
setInterval(() => Api.get("/api/heartbeat").catch(() => status("Backend not reachable. Restart Launch.exe.", "err")), 15000);

(async function init() {
  try {
    await Api.get("/api/health", null, 5000);
    await Api.post("/api/ready");
    const s = await Api.get("/api/system");
    drawSystem(s.system);
    $("#readyTime").textContent = `launch → UI ready ${s.system.launchToUiReady_s.toFixed(2)} s · ${s.system.logicalCpus} logical CPUs`;
    status("Connected to C++ backend. Ready.", "ok");
    // Pre-populate each view so the first impression shows working results.
    applyStl(await Api.get("/api/examples/stl"));
    applyCloud(await Api.get("/api/examples/nodal"));
    $("#feaForm").requestSubmit();
    $("#bpForm").requestSubmit();
    drawHw(await Api.get("/api/hw/state"));
    benchUi(await Api.get("/api/bench/status"));
  } catch (e) {
    status(e.message, "err");
  }
})();
