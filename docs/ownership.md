# Ownership statement

> **CANDIDATE: review, edit and sign.** It must accurately describe your own contribution.

## Reused work

- **Third-party code:** none. Only the C++ standard library and the Win32 API are used. The frontend uses no external libraries.
- **Toolchain (not redistributed in source form):** WinLibs MinGW-w64 GCC 16.2.0, CMake 4.4.4, Ninja 1.13.2. The executables link GCC's runtime statically. libgcc and libstdc++ are covered by the GCC Runtime Library Exception, and winpthreads by an MIT-style licence, so static linking places no licence obligation on this package.
- **Data:** self-created (CC0). The CSV is synthetic. See `data/PROVENANCE.md`.
- **Reference knowledge:** standard 1D FEA formulation (weak form, linear shape functions, Dirichlet elimination), the Thomas algorithm, the tanh derivative, and the ASCII STL grammar.

## AI-generated work

The source code, tests, frontend, packaging script and the first drafts of the documentation were generated with Claude Code (Claude Opus 5.5), as recorded in `docs/ai_use_log.md`.

## Original work by the candidate

`CANDIDATE`: list what is genuinely yours. For example:
- the handwritten backpropagation derivation (appendix);
- the "Interdisciplinary learning" section of the report;
- the demo video;
- your review and edits of the code and docs;
- your validation steps;
- the AI-use log entries in the `CANDIDATE` section.

Signed: `<name, candidate ID, date>`
