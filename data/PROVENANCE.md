# Example data provenance

Both files are **self-created** by `wb_cli examples data` (source: `src/cli/wb_cli.cpp`, `cmdExamples`), so they can be regenerated exactly. Licence: CC0 1.0 (public-domain dedication). No third-party or proprietary data is included.

## `heatsink_bar.stl`: ASCII STL geometry

| Item | Value |
|---|---|
| Origin | Generated programmatically: five axis-aligned boxes, 12 triangles each = 60 triangles |
| Shape | Base bar 100 mm × 20 mm × 10 mm with four fins of 2 mm × 20 mm × 15 mm at x = 10, 35, 60, 85 mm |
| Units | **metres** (STL itself stores no units; the coordinates are written in metres, e.g. `0.1`) |
| Format notes | ASCII STL, one `solid heatsink_bar`, outward facet normals, counter-clockwise vertex order seen from outside. The fins sit on the base top face (z = 0.010 m), so the faces touch but are not merged. That is valid for display but means the solid is not one watertight shell. |
| Export equivalent | In CAD tools: *Export → STL → ASCII*, units metres. |

## `synthetic_rod_nodal_temperatures.csv`: nodal temperature results (**SYNTHETIC**)

> **This file is synthetic. It is not an actual ANSYS export.** It follows the column schema of an ANSYS nodal-temperature export so the import path can be exercised.

| Item | Value |
|---|---|
| Origin | Analytical solution of the Task C rod, T(x) = 100 − 80x + 500·x(1 − x) °C (L = 1 m, k = 10 W/(m·K), q = 1e4 W/m³), sampled on a cylinder of cross-section A = 1e-4 m² (radius 5.642 mm) |
| Nodes | 41 stations along x × (1 centre node + 8 ring nodes) = 369 nodes, ids 1…369 |
| Units | x_m, y_m, z_m in metres; temperature_C in degrees Celsius |
| Schema | `node_id,x_m,y_m,z_m,temperature_C`; lines starting with `#` are comments and carry this provenance inside the file |
| Physics note | The lateral surface is insulated, so temperature is uniform over each cross-section (1D problem). |

### How a real ANSYS export maps to this schema

In ANSYS Mechanical: select the *Temperature* result → right-click → *Export → Export Text File*. The export has node number, X/Y/Z location and temperature columns, with unit labels in the header that depend on the project unit system. To use it here:

1. Rename the columns to `node_id,x_m,y_m,z_m,temperature_C`.
2. Convert coordinates to metres and temperatures to °C if the project uses other units.
3. Save as comma-separated text. The parser rejects tab-separated files with an error that names the header column.

## Test fixtures (`tests/data/`)

These are small hand-written valid and deliberately malformed STL/CSV files used by `wb_tests`. They are also self-created and CC0.
