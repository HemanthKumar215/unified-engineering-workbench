# AI-use log

> **Status: the factual session record below was written during development. Sections marked `CANDIDATE` must be completed by the candidate before submission.** The brief requires that no interactions are fabricated. Do not add prompts, outputs or reviews that did not happen.

## Tools

| Tool | Model / version | Used for |
|---|---|---|
| Claude Code (Claude desktop app, Code tab) | Claude Opus 5.5 (`claude-opus-5-5`) | Reading the brief, designing the architecture, writing the C++/JS code, tests, docs and packaging, running builds/tests/benchmarks, browser-testing the UI |
| `CANDIDATE`: add any other assistant you used (e.g. ChatGPT for learning FEA), with model names | | |

## Session record (Claude Code, 31 Oct – 1 Nov 2026)

**Prompts given by the candidate (verbatim or near-verbatim):**

1. The college placement notice was pasted together with the assignment PDF (`35fdd634-….pdf`). No other instruction was given; the AI treated the request as "help complete this assignment".
2. Answer to the AI's question about which compiler to install: chose *"WinLibs GCC (Recommended)"*.
3. `"C:\Users\…\passscince/" use this folder`
4. `ok can u continue`

**What the AI produced:** the whole code base in this repository (C++ core, server, CLI, tests, frontend), the example data, the packaging script, the README and these docs. Every commit carries a `Co-Authored-By: Claude` trailer.

**How the AI validated its output:**
- `wb_tests` (23 tests, 260 checks);
- comparison of FEA errors with the closed form q·h²/(8k);
- central-difference gradient check;
- serial/parallel agreement;
- driving the UI in a browser (all five views, invalid inputs, cancel, device timeout → retry);
- launching the packaged ZIP from a different folder;
- checking `Launch.exe` DLL imports with `objdump`;
- timing parsers and renderers on generated 1.6–41 MB files.

### Incorrect or unsuitable AI output identified during the session (factual)

| # | What the AI did | How it was detected | Resolution |
|---|---|---|---|
| 1 | Used a `thread_local` workspace inside the worker pool | `wb_cli bench` exited with code 127 when a worker thread ended (statically linked MinGW TLS destructor problem) | Replaced with an explicit worker index passed to tasks (commit "Benchmark: per-worker workspaces…") |
| 2 | Proposed that per-solve memory allocation caused the poor scaling at 192 × 100k | A/B benchmark of the old vs new binary showed no difference | Hypothesis rejected. Four more hypotheses were tested and rejected; the anomaly is reported as unresolved (report §4) |
| 3 | Named a form input `elements` | The benchmark button did nothing; the console showed `form.elements` resolving to the input | Renamed to `mesh` |
| 4 | Read `t.median` in the UI while the JSON field is `median_s` | The status bar showed "Cannot read properties of undefined (reading 'toFixed')" | Field name fixed |
| 5 | Tried the Windows short (8.3) path, then a directory junction, to work around GCC failing on a toolchain path containing spaces | Both still failed (GCC resolves its real path) | Copied the toolchain to `C:\wlgcc`. A leftover junction `C:\winlibs` remains on the dev machine. |
| 6 | Drafted report text in the first person ("My first convergence check…") describing work the candidate did not do | Caught in the AI's own review before commit | Rewritten in neutral voice; attribution kept in this log |

## CANDIDATE: your own review and validation

Fill in only what you actually did:

- **Code you read and understood** (name files/functions you can explain in the interview):
- **Your edits** to AI output (what and why):
- **Your own validation** (e.g. you re-ran `wb_tests`, hand-solved N = 2, checked the backprop numbers against your handwritten pages):
- **An incorrect or unsuitable suggestion you identified yourself**, or a **deliberate challenge test** and its outcome. One suggestion: ask an assistant "does a nodal error of 1e-13 for N = 10 prove my FEA converges?". A correct answer must mention nodal exactness for 1D linear elements and point to midpoint errors. Record the prompt, the answer and your judgement.
- **Learning prompts** you used (e.g. asking for an explanation of the weak form or the Thomas algorithm), with representative excerpts.
