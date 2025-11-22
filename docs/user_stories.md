## Group A – Project & session management

### Create debugging session

As a developer, I want to create a new debugging session for a local binary or project so that I can analyze its behavior in a structured, repeatable way.

### Attach to running process

As a developer, I want to attach the debugger to an already running process so that I can inspect its state without restarting it.

### Save & reload sessions

As a researcher, I want to save a session’s configuration, analysis artifacts, and AI explanations so that I can reload them later or share them with others.

## --------------------------------------------------------------------------

## Group B – Ghidra & static analysis integration

### Quick binary overview

As a reverse engineer, I want to send a binary to the backend and get a high-level summary of its capabilities (I/O, network, persistence, crypto) so that I can decide whether it’s worth deeper analysis.

### Function summaries

As a reverse engineer, I want to select one or more functions in the disassembly and receive natural-language summaries of what they appear to do so that I can navigate large binaries more efficiently.

### Suspicious pattern detection

As a security analyst, I want the system to flag functions or code regions that match known suspicious patterns (e.g., shellcode decoding, process injection) so that I can prioritize where to look.

## --------------------------------------------------------------------------

## Group C – Dynamic/contextual debugging with AI

### Context snapshot & explanation

As a developer, I want to request a “context snapshot” at a breakpoint (call stack, local variables, key memory locations) so that I can ask the AI to explain what the program is doing at this moment.

### Compare two states

As a developer, I want to compare two snapshots (e.g., before/after a function call) so that I can see what changed and get an explanation of why those changes matter.

### Natural-language queries

As a developer, I want to ask questions like “Why did this function fail?” or “Where does this value come from?” and have the system use the collected traces and static info to answer.

## --------------------------------------------------------------------------

## Group D – Explainability & trace visualization

### Execution trace visualization

As a developer, I want to see a visual trace of function calls and important events over time so that I can understand the overall execution path.

### Highlight root causes

As a researcher, I want the system to propose likely root causes for observed failures (e.g., incorrect assumptions, invalid states, race conditions) so that I can focus my attention on verifying or refuting them.

## --------------------------------------------------------------------------

## Group E – Advanced / performance / future CUDA

### Parallel analysis on large traces

As a power user, I want large traces and logs to be processed in parallel (GPU-accelerated if available) so that the AI summaries and pattern detection remain responsive even for big systems.

### Configurable AI analysis depth

As a user, I want to configure the level of analysis (quick/high-level vs deep/slow) so that I can trade off speed and thoroughness depending on the situation.