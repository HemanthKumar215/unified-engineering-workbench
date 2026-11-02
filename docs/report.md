# Unified Engineering Workbench: Technical Report

*Pass Science Works R&D Internship 2026–27, common technical assignment. Candidate: P Hemanth Kumar (RA2311003011874). Recorded effort: `<fill in actual hours>`.*

## 1. Design choices

**Delivery.** The UI is a local web page served by `Launch.exe`, a single statically linked C++17 executable. It binds only to 127.0.0.1, opens the default browser, and shuts itself down on *Quit*, when the tab closes (15 s grace for reloads), or after 4 minutes with no page activity. A browser UI avoids shipping a GUI toolkit and runtime. Static linking (`-static`) means the package imports only Windows system DLLs, which was verified with `objdump -p`, so no redistributables are needed.

**Layering.** `wb_core` (numerics, parsing, concurrency, device mocks) has no knowledge of HTTP or the UI. `src/server` holds the transport (a Winsock loopback server), the route mapping and the background job runner, with no numerics. The frontend is plain JavaScript with no third-party libraries, so it works offline and has no licence obligations. It validates inputs for fast feedback, but the C++ side re-validates everything, because the UI is not trusted.

**Responsiveness and cancellation.** The benchmark runs on its own thread, and the UI polls `/api/bench/status` every 0.4 s. *Cancel* sets an atomic flag that is checked before each case. The job reports `cancelled` and keeps the previous completed report, so partial results are never published or exported. All other operations are short and stateless.

**Dependencies.** None beyond the C++ standard library and Win32 (`ws2_32`, `shell32`, `psapi`). The toolchain is WinLibs GCC 16.2.0 (UCRT), CMake 4.4.4 and Ninja 1.13.2, recorded in `docs/build_versions.txt`.

## 2. Thermal FEA (Task C)

**Problem.** Steady conduction in a uniform rod with uniform volumetric heat generation:

−d/dx(kA dT/dx) = qA, 0 < x < L, T(0) = 100 °C, T(L) = 20 °C,

with L = 1 m, A = 1e-4 m², k = 10 W/(m·K) and q = 10,000 W/m³.

**Assumptions.**
- Steady state.
- Constant k, A and q.
- Insulated lateral surface, so there is no convection term and the problem is truly 1D (temperature uniform over the cross-section).
- Both ends are Dirichlet boundaries.

**Units.** SI with temperature in °C. Only differences of T appear in the physics, so °C and K give the same result.

**Formulation.** Multiplying by a test function w and integrating by parts gives the weak form:

∫ kA w′T′ dx = ∫ w qA dx

The boundary term vanishes because w = 0 at both Dirichlet ends. With two-node linear elements of length h = L/N, each element contributes:

- stiffness **K**ₑ = (kA/h)·[[1, −1], [−1, 1]]
- consistent load **f**ₑ = (qAh/2)·[1, 1]

Assembly gives a symmetric tridiagonal system. The prescribed temperatures are imposed by **elimination**: the known column is moved to the right-hand side (f₁ −= K₁₀·T₀ and f_{N−1} −= K_{N−1,N}·T_L), the two constrained rows are removed, and the interior system is solved with the Thomas algorithm in O(N). Both end values are then set exactly. Elimination keeps the reduced matrix symmetric positive definite. The alternative, a large penalty on the diagonal, would only approximate the boundary values.

**Exact solution.** T(x) = 100 − 80x/L + [q/(2k)]·x(L − x). Setting dT/dx = −80 + 1000(1 − 2x) = 0 gives the peak at x = 0.42 m, where T = 100 − 33.6 + 500·0.42·0.58 = 188.2 °C. That is consistent with the plotted profile.

**Verification** (`results/fea_convergence.csv`, `fea_temperature_profiles.svg`, `fea_convergence.svg`):

| N | h [m] | max nodal error [°C] | max midpoint error [°C] | q h²/(8k) [°C] | observed order |
|---|---|---|---|---|---|
| 10 | 0.1 | 2.8e-14 | 1.250000 | 1.25 | n/a |
| 20 | 0.05 | 2.0e-13 | 0.312500 | 0.3125 | 2.000 |
| 40 | 0.025 | 8.8e-13 | 0.078125 | 0.078125 | 2.000 |
| 80 | 0.0125 | 1.2e-12 | 0.019531 | 0.01953125 | 2.000 |

The midpoint values come from **FE interpolation**, (Tᵢ + Tᵢ₊₁)/2, not from the exact formula. The error equals the interpolation error of a parabola: T_exact(mid) − T_FE(mid) = (q/2k)(h/2)² = qh²/(8k), so it converges at exactly second order. Tests also check:
- q = 0 gives a linear profile;
- one element gives T(0.5) = 60 °C by interpolation;
- global energy balance: the reactions at both ends sum to −qAL = −1 W.

**Debugging insight.** The nodal-error column is 1e-14 to 1e-12 for every N and *grows* slightly with N (round-off accumulation). A convergence check based on nodal values alone would therefore suggest either a perfect solver or a broken test, and it would show no convergence. The explanation is a known 1D property: for constant coefficients, linear elements are **nodally exact**, because the FE solution interpolates the exact solution at the nodes. The discretisation error lives *between* the nodes, which is why the brief asks for midpoint errors. Measuring at the midpoints recovers the expected O(h²) behaviour and matches the closed form exactly. The nodal-error column is kept as evidence of this.

**Interdisciplinary learning.** `<Candidate: 4–6 sentences in your own words. Cover what was new to you (weak form, assembly, Dirichlet elimination, why nodal exactness happens) and how you checked your understanding (hand-solving N = 2, energy balance, the q = 0 case). This section is assessed and must be your own writing.>`

## 3. CAD / ANSYS visualisation (Task B)

- **Parsing.** Both formats are parsed and validated in C++ (`stl_parser.cpp`, `nodal_csv.cpp`). Invalid input is rejected with line-numbered messages, as listed in README §3. 16 malformed fixtures are covered by tests.
- **STL viewer.** Flat-shaded triangles with rotate, pan, zoom, fit-to-view, wireframe and an optional mm interpretation.
- **Nodal CSV.** Shown as a point cloud coloured by temperature, with a legend in °C, min/max values, and click-to-inspect (node_id, x/y/z in m, T in °C).
- **Example data.** Provenance is in `data/PROVENANCE.md`. The CSV is labelled **synthetic** in the file, in the docs and in the UI.
- **Tested size limits.** See README §3: parsing takes 4.2 s for 100k triangles and 15.7 s for 1M nodes. Rendering subsamples to about 20k items while dragging, which brought a 1M-point drag frame from 5.1 s to 0.09 s.

## 4. Multicore performance (Task D)

**Workload.**
- `generateBatch(seed)` draws k ∈ [1, 400] W/(m·K) and q ∈ [1e3, 1e5] W/m³ from `std::mt19937_64`. It maps the raw 64-bit output itself, because `uniform_real_distribution` is implementation-defined, so the cases are identical on every compiler.
- Each case is an independent FEA solve on the same mesh, solved by the same numerical method in both the serial and parallel versions.
- The serial version is a plain loop.
- The parallel version uses `WorkerPool`: p `std::thread`s created once per configuration (outside timing), with dynamic index hand-out via an atomic counter. Each case writes only its own output slot, so the result order is deterministic and needs no locks.
- Per configuration there is one warm-up run followed by five measured runs, and the median is reported. Only the solve loop is timed (`steady_clock`); generation, comparison, file output and rendering are excluded.
- Agreement: max |T_parallel − T_serial| over all nodes and cases, with tolerance 1e-8 °C. Launch-to-listen time is reported separately (3–5 ms).

**Machine.**
- Intel Core i7-14650HX: 16 physical cores (8 P + 8 E), 24 logical CPUs.
- 15.6 GiB RAM, Windows 11.
- GCC 16.2.0, `-O3 -DNDEBUG`, static runtime.

| Workload | p | median [s] | speedup T1/Tp | efficiency | max \|ΔT\| [°C] |
|---|---|---|---|---|---|
| small: 96 cases × 50k elements | 1 | 0.0632 | 1.00 | 1.00 | 0 |
| | 2 | 0.0305 | 2.07 | 1.03 | 0 |
| | 4 | 0.0175 | 3.61 | 0.90 | 0 |
| large: 192 cases × 100k elements | 1 | 0.2153 | 1.00 | 1.00 | 0 |
| | 2 | 0.3468 | 0.62 | 0.31 | 0 |
| | 4 | 0.1780 | 1.21 | 0.30 | 0 |

Raw runs are in `results/bench_*/bench_raw_timings.csv`. Agreement is bit-exact (0.0), because every case runs the same deterministic code on the same inputs regardless of which thread runs it.

**Analysis.**
- **Small workload.** It scales close to ideal: 3.6× on 4 workers. The cases are fully independent, there is no shared mutable state, and the working set per worker (about 2.8 MB) fits in cache. Efficiency slightly above 1 at p = 2 is within run-to-run noise (single-thread turbo and scheduling vary by about ±10%). Overheads are the condition-variable wake-up and join, the atomic counter, and turbo frequency falling as more cores become active.
- **Large workload.** It reproducibly scales *badly*. This was investigated rather than reporting only the good case:
  1. **Allocation per solve.** The code was refactored to reuse per-worker workspaces and output buffers, then the old and new binaries were A/B tested. **No change**, so this hypothesis was rejected. The refactor is kept as a sound improvement.
  2. **Page faults.** Counting them gave fewer than about 1,200 per run, which is negligible. **Rejected.**
  3. **E-core placement.** Pinning to P-cores only still collapsed, and E-cores alone scaled normally (1.8×). **Rejected.**
  4. **SMT siblings.** One logical CPU per P-core still collapsed. **Rejected.**
  5. **The comparison pass** between runs, which reads about 300 MB. **Rejected.**
  6. **Which thread first-touches the output.** **Rejected.**

  The decisive observation came from a standalone diagnostic (`tests/diag_scaling.cpp`). The **same size runs at 1.85× on 2 workers outside the benchmark harness**. So the anomaly depends on the harness sequence, not on the solver. It appears once stored results exceed about 13–16M nodes (about 100–128 MB per result set), and that threshold varies between runs. Remaining hypotheses are memory-system state after a long single-threaded phase (cache/TLB or prefetcher interaction with roughly 300 MB of resident results) and power management. **The root cause is not yet identified.** It is stated here as a limitation instead of tuning the workload to hide it.

**On a 1-logical-CPU machine,** the pool path still runs (p = 1) and agreement is still checked, but the UI states that speedup cannot be assessed.

## 5. Embedded integration (Task E)

`docs/embedded_design.md` contains:
- the block diagram, which is also shown in the app;
- device assumptions;
- the interface and pin-voltage table (power, ground star point, voltage domains, level shifting for 3.3 V ↔ 1.8 V banks, connectors, host links, JTAG ownership);
- why a JTAG adapter is a debug bridge and not interchangeable compute;
- a 7-step safe integration plan, and limitations.

The C++ mocks implement discovery, capabilities, connect/disconnect, one telemetry message per device type (the JTAG telemetry is an IDCODE scan), and fault injection. The demonstrated recoverable failure is a simulated handshake timeout: the device moves to `error`, then a retry connects. Everything simulated is labelled SIMULATED.

## 6. Backpropagation (Task F)

The forward pass at x = 1, y = 2 and (w, b, v, c) = (0.5, 0, 1, 0):

- z = 0.5
- h = tanh 0.5 = 0.4621171573
- ŷ = 0.4621171573
- J = 1.1825418190

The chain rule gives:

- e = ŷ − y = −1.5378828427
- ∂J/∂c = e = −1.5378828427
- ∂J/∂v = e·h = −0.7106820475
- ∂J/∂z = e·v·(1 − h²) = −1.2094644752, so ∂J/∂w = ∂J/∂z·x = −1.2094644752 and ∂J/∂b = −1.2094644752

The simultaneous update with η = 0.1 gives w = 0.6209464475, b = 0.1209464475, v = 1.0710682047, c = 0.1537882843.

All four gradients are negative: ŷ is below the target, and increasing any parameter raises ŷ, so descent increases all four.

The C++ central differences (ε = 1e-6) agree to at most 1.13e-10, well within 1e-5. Over the next 100 steps the loss falls from 0.686 to 1.1e-20 (`results/backprop_loss.svg`). The fall is fast and monotone because a single sample can be fitted exactly. Near the optimum the loss is locally quadratic, so the error contracts by a roughly constant factor per step (linear convergence, a straight line on a log scale). It finally levels off at the floating-point limit. The handwritten derivation is in the appendix: `docs/backprop_handwritten.pdf` (two pages, scanned).

## 7. Test outcomes

`wb_tests` reports **23 tests, 0 failed, 260 checks** (positive, negative and numerical). The coverage is listed in README §5. In addition, these were checked manually through the UI:

- every view;
- invalid inputs (the field is highlighted and the status bar explains the problem);
- cancellation (previous results are kept);
- device timeout and recovery;
- launching from a fresh extraction in another folder;
- the 64 MB and 1M-node limits.

**Bugs found during development** (see the Git history):
- A form input named `elements` shadowed `HTMLFormElement.elements`, which broke validation.
- The UI read `median` where the JSON field is `median_s`.
- A `thread_local` workspace crashed at thread exit with statically linked MinGW. It was replaced by an explicit per-worker index.

## 8. Limitations

- The 3D viewer is canvas-2D (painter's algorithm), not WebGL, so large models are slow and intersecting faces can sort wrongly.
- STL units are assumed to be metres.
- The example CSV is synthetic.
- The large-batch scaling anomaly is unresolved (§4).
- The hardware is fully simulated.
- MSVC builds are untested.
- Windows SmartScreen may warn about the unsigned `Launch.exe`.

## 9. Ownership statement

**Reused work**
- **Code:** no third-party source code or libraries. The project uses only the C++ standard library and the Win32 API; the frontend uses no external JavaScript libraries.
- **Toolchain (not redistributed):** WinLibs MinGW-w64 GCC 16.2.0, CMake 4.4.4 and Ninja 1.13.2. GCC's runtime is linked statically under the GCC Runtime Library Exception (libgcc, libstdc++) and an MIT-style licence (winpthreads).
- **Standard methods:** the 1D linear finite-element formulation, Dirichlet elimination, the Thomas algorithm, the derivative of tanh, and the ASCII STL grammar are textbook material, implemented here from their standard definitions.
- **Data:** none reused. All example and test data is self-created (CC0); the nodal CSV is synthetic (`data/PROVENANCE.md`).

**Original work for this assignment**
- **Produced for this submission:** the architecture, the C++ core, server and CLI, the browser UI, the tests and fixtures, the example data, the benchmark investigation, the embedded design and this documentation. How they were produced, including all tools and assistance used, is disclosed in `docs/ai_use_log.md`.
- **Handwritten by the candidate:** the backpropagation derivation (appendix).
- **Recorded by the candidate:** the demonstration video.

Signed: P Hemanth Kumar, RA2311003011874, 1 November 2026
