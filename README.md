# Unified Engineering Workbench

A small Windows 11 x64 engineering workbench for the Pass Science Works R&D internship assignment. It:

- imports ASCII STL geometry and ANSYS-style nodal temperature CSV,
- solves a 1D steady-state thermal FEA problem and verifies it against the exact solution,
- benchmarks serial vs. multithreaded batches of solves,
- demonstrates simulated (mock) MCU / FPGA / JTAG hardware adapters,
- checks a hand-derived backpropagation step.

All backend logic (parsing, numerics, concurrency, device mocks, file processing) is **C++17**. The browser UI only presents results.

---

## 1. Run it (one click)

**Prerequisites:** Windows 10/11 x64 and any modern browser (Edge is built in). No installation, admin rights, internet, PowerShell or build step is needed.

1. Extract `CandidateID_Workbench.zip` **completely** (not only `Launch.exe`).
2. Double-click **`Launch.exe`**.
3. Your default browser opens `http://127.0.0.1:8765/` (or the next free port up to 8805).
4. Click **Quit** (top right) when you are done. Closing the tab also stops the backend after about 15 s.

Notes:

- Windows SmartScreen may warn about an unsigned executable downloaded in a ZIP. Click *More info → Run anyway*, or right-click the ZIP → *Properties → Unblock* before extracting.
- The server binds to the loopback interface only (127.0.0.1), so it is not reachable from the network.
- Logs are written to `logs/workbench.log` next to `Launch.exe` (falls back to `%TEMP%` if that folder is read-only).

### From VS Code / Antigravity

Open the extracted folder, then use either option:

- **No extension needed:** *Terminal → Run Task… → Launch Workbench*. It is also the default build task (`Ctrl+Shift+B`).
- **With the Microsoft C/C++ extension (`ms-vscode.cpptools`):** *Run and Debug → "Launch Workbench (packaged)" → F5*.

Both start the same packaged `Launch.exe`. The standalone launcher never needs the editor.

---

## 2. Architecture

```
Browser UI (src/frontend, plain JS, no libraries)
      │  HTTP/JSON on 127.0.0.1 (transport only)
      ▼
Launch.exe ─ src/server ─ http_server  (Winsock, loopback, thread per request)
                        ─ api          (routes → core calls, JSON/CSV out)
                        ─ jobs         (background benchmark thread, progress, cancel)
      │
      ▼
wb_core (src/core, C++17 static library, no I/O or UI dependencies)
  fea1d       1D linear-element FEA, Dirichlet elimination, Thomas solver, error norms
  stl_parser  strict ASCII STL parser
  nodal_csv   node_id,x_m,y_m,z_m,temperature_C parser
  batch       seeded case generator, bounded WorkerPool, serial/parallel benchmark
  hardware    IDeviceAdapter + MCU/FPGA/JTAG mocks, fault injection, DeviceManager
  backprop    hand-derived gradients, central differences, training loop
  sysinfo     CPU/cores/RAM/compiler, page-fault counter
  svg_plot    labelled SVG charts for results/
```

- **Responsive UI:** long benchmarks run on a background C++ thread. The UI polls progress, and **Cancel** sets an atomic flag checked before every case. A cancelled or failed run never replaces the last completed report.
- **Parallelism:** `WorkerPool` is a fixed set of `std::thread`s. `parallelFor` hands out case indices through an atomic counter, and each case writes only its own pre-sized output slot. That keeps results in deterministic input order with no locks on the result data.
- **Separation of concerns:** `wb_core` has no knowledge of HTTP or the UI, and `src/server` contains no numerics. The CLI (`wb_cli`) and the tests link the same core.

---

## 3. Supported formats

| Format | Required content | Units | Validation |
|---|---|---|---|
| ASCII STL | `solid` … `facet normal` / `outer loop` / 3× `vertex` / `endloop` / `endfacet` … `endsolid` | No units in STL; assumed **m** (UI can treat the file as mm) | Rejects: empty, whitespace-only, binary STL, non-finite numbers, wrong vertex count, missing `endsolid` (truncated), zero triangles. Warns on degenerate triangles. Errors give line numbers. |
| Nodal CSV | Header exactly `node_id,x_m,y_m,z_m,temperature_C`; `#` comment lines and a UTF-8 BOM allowed | m, °C | Rejects: empty, header-only, wrong/missing columns, non-numeric, NaN/Inf, duplicate `node_id`, T < −273.15 °C. |

Native ANSYS project/result files and native CAD solids are out of scope. Mesh-to-result registration is not performed; both are shown in the same world frame.

### Tested file-size limits (i7-14650HX, Edge/Chromium)

| Input | Size | Backend parse + JSON (C++) | Full render | Drag frame (level of detail) |
|---|---|---|---|---|
| STL 10k triangles | 1.6 MB | 1.1 s | 77 ms | n/a |
| STL 100k triangles | 16 MB | 4.2 s | 0.9 s | ≈0.1 s |
| STL 250k triangles | 41 MB | 9.0 s | 8.7 s | 0.2 s |
| CSV 100k nodes | 4 MB | 2.5 s | 0.4 s | ≈0.05 s |
| CSV 1M nodes | 41 MB | 15.7 s | 5.1 s | 0.09 s |

Hard limits enforced in C++: 64 MB per file, 500,000 triangles, 1,000,000 nodes. The HTTP transport limit is 70 MB. Smooth interaction is comfortable up to about 20k triangles or 50k nodes.

---

## 4. Build from source (CMake)

Building from source is only needed to rebuild; the packaged `Launch.exe` needs none of this. Requirements: CMake ≥ 3.20, Ninja, and a C++17 compiler. It was tested with WinLibs MinGW-w64 GCC 16.2.0 (UCRT, POSIX threads), CMake 4.4.4 and Ninja 1.13.2.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/path/to/mingw64/bin/g++.exe
cmake --build build
build\wb_tests.exe tests\data
```

> MinGW's linker fails when the **toolchain** path contains spaces (for example a user profile like `C:\Users\First Last\...`). Copy the toolchain to a space-free folder such as `C:\wlgcc` first.

Regenerate the evidence in `data/` and `results/`:

```bat
build\wb_cli.exe examples data
build\wb_cli.exe fea results
build\wb_cli.exe backprop results
build\wb_cli.exe bench results\bench_small --cases 96 --elements 50000 --reps 5 --workers 4
build\wb_cli.exe bench results\bench_large --cases 192 --elements 100000 --reps 5 --workers 4
```

Package the submission (copies executables, `app/`, sources and evidence, and writes a Git bundle and the ZIP):

```bat
cmake -DCANDIDATE_ID=YourID -P scripts/package.cmake
```

MSVC should also work (the code uses only the standard library and Win32), but it was **not tested**.

---

## 5. Tests

`wb_tests` is a framework-free runner with 23 test cases and 260 checks:

- **FEA:** nodal exactness, midpoint error equal to q·h²/(8k), observed order 2, q = 0 gives a linear profile, single element, global energy balance, input validation.
- **Parsers:** valid files, the bundled examples, and 16 malformed fixtures (`tests/data/`), each checked for the expected error message.
- **Concurrency:** reproducible generator, parallel == serial at p = 1–4 with input order kept, pool reuse after an exception, cancellation, a small benchmark.
- **Hardware mocks:** full lifecycle of the 3 device types, timeout followed by recovery (one-shot fault, retry policy, persistent fault then cleared).
- **Backprop:** forward/backward values, central-difference check (ε = 1e-6, tolerance 1e-5), monotone loss decrease.

Run with `wb_tests.exe tests\data` (exit code 0 means all tests pass) or `ctest --test-dir build`.

---

## 6. Repository layout

```
Launch.exe, wb_cli.exe, wb_tests.exe   packaged binaries (ZIP only)
app/                                   packaged copy of src/frontend (ZIP only)
src/core  src/server  src/cli  src/frontend
tests/                                 test runner, malformed fixtures, scaling diagnostic
data/                                  example STL + synthetic CSV, PROVENANCE.md
results/                               raw CSV + labelled SVG plots
docs/                                  report, embedded design, AI-use log, ownership
.vscode/                               tasks.json, launch.json
vcs/                                   git bundle + readable commit log (ZIP only)
CMakeLists.txt  scripts/package.cmake
```

---

## 7. Known limitations

- **3D viewer:** a canvas-2D painter's-algorithm renderer, not WebGL. Intersecting triangles can sort incorrectly, and very large models are slow (see the limits above).
- **STL units:** STL has no units. Metres are assumed, and the UI offers a mm interpretation for display only.
- **Example CSV is synthetic:** it is the analytical rod solution sampled on a cylinder, not an ANSYS export.
- **Large-batch scaling anomaly:** at about 16M+ stored nodes (for example 192 cases × 100k elements), the benchmark harness shows poor parallel speedup (p = 2 slower than p = 1). A standalone loop of the same size scales normally. The root cause is not yet identified; the ruled-out causes are listed in `docs/report.md` §4.
- **Launch → UI-ready time** is measured from process start to the first page report. It includes the browser start-up, so it varies with the browser.
- **Hardware:** all device behaviour is simulated. No USB, serial or JTAG I/O exists.
- **Single user:** one benchmark job runs at a time, and the state lives in the running process only.
