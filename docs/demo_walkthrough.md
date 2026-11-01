# Demo walkthrough (script for the 3–5 minute video)

Record the screen with **Win + Alt + R** (Xbox Game Bar) or OBS. Use a freshly extracted ZIP so the launch is shown from a clean folder.

| Time | Show | Say |
|---|---|---|
| 0:00 | Extracted folder → double-click `Launch.exe` | One click: browser opens, no install; status bar shows "Connected to C++ backend" and launch → ready time |
| 0:20 | **Geometry**: bundled STL, rotate/pan/zoom/fit, wireframe | ASCII STL parsed and validated in C++; units assumed metres |
| 0:45 | Load the bundled CSV, click a node → inspect tooltip, show legend | Synthetic data, labelled as such; legend in °C |
| 1:05 | Load `tests/data/nan_vertex.stl` and `tests/data/duplicate_id.csv` | Malformed files rejected with line-numbered messages |
| 1:25 | **Thermal Analysis**: default solve, both plots, table | Linear elements, Dirichlet elimination; nodal error ~1e-13 (nodally exact); midpoint error = qh²/8k, order 2 |
| 2:05 | Type `-1` in k → red field and message; *Export CSV* | C++ re-validates; CSV export of the underlying data |
| 2:20 | **Performance**: Run benchmark → progress → table/plot; start a bigger run and *Cancel* | Bounded worker pool, seed, 1 warm-up + 5 reps, median, speedup/efficiency, agreement 0.0; cancel keeps the last good results |
| 3:05 | **Hardware**: Discover → connect MCU, read telemetry; FPGA fault `timeout_once` → Connect fails → Retry succeeds; JTAG IDCODE scan | Everything simulated and labelled; JTAG is a debug bridge, not compute |
| 3:50 | **Backprop**: table and loss plot | Matches the handwritten derivation; central difference within 1e-10 |
| 4:15 | Terminal: `wb_tests.exe tests\data` → 23 passed; VS Code *Run Task → Launch Workbench* | Tests and editor one-click run |
| 4:40 | Click **Quit** | Clean shutdown |

If no video is possible, take one screenshot per row, put a timestamp in each file name, and include `logs/workbench.log`.
