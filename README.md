# guardAIn Debugger - An AI-Powered Debugging Tool

building checklists by modules and their interactions with the CoreEngine.

## Runtime

### Development Roadmap

High-level milestones for the LLM-driven QEMU debugger prototype.  
Each milestone is broken down into concrete, checkable tasks.

---

#### :white_check_mark:  Milestone 0 – Project Skeleton & Scaffolding

- [x] Create core project structure
  - [x] `core/` for backend logic
  - [x] `cli/` for command-line entry point
  - [x] `config/` for config files
- [x] Create initial core headers
  - [x] `RuntimeManager.hpp` stub
  - [x] `QemuController.hpp` stub
  - [x] Add `#pragma once` guards
- [x] Add basic build setup
  - [x] Configure project in IDE / build system (VS solution / CMake, etc.)
  - [x] Ensure `core/` and `cli/` targets build (even if mostly empty)
- [x] Initialize README with project overview and roadmap

---

#### :white_check_mark:  Milestone 1 – QEMU Controller Can Launch & Stop QEMU

**Goal:** From `main.cpp`, start and stop QEMU in a controlled way.

- [x] Decide canonical QEMU install path for this project
  - [x] Choose primary path (`A:\QEMU\`)
  - [x] Store in a config file (`config/config.json` or `.ini`)
- [x] Implement `QemuController` process management
  - [x] Add code to construct a QEMU command line from config
  - [x] Implement `bool QemuController::startVm(const std::string& vmName)`
    - [x] Spawn QEMU process (initially via `std::system` or similar)
    - [x] Log full command line on launch
  - [x] Implement `bool QemuController::stopVm()`
    - [x] For now, send a simple kill/terminate to the process
  - [x] Implement `bool QemuController::isRunning() const`
- [x] Add a simple CLI harness
  - [x] `cli/main.cpp` calls `QemuController::startVm()`
  - [x] Wait a few seconds, then call `stopVm()`
  - [x] Print success/failure status to console
- [x] Manual smoke test
  - [x] Run binary and confirm QEMU appears briefly, then exits
  - [x] Capture any QEMU stdout/stderr to a log file

---

#### :white_check_mark:  Milestone 2 – QMP Control & Baseline Snapshot

**Goal:** Programmatically talk to QEMU via QMP and create a baseline snapshot.

- [x] Extend QEMU launch options in `QemuController`
  - [x] Start QEMU with QMP TCP or UNIX socket, e.g. `-qmp tcp:localhost:4444,server,nowait`
  - [x] Ensure QEMU starts successfully with these options
- [x] Implement a minimal `QmpClient` inside `QemuController`
  - [x] Connect to QMP socket
  - [x] Read initial QMP greeting
  - [x] Send `{"execute": "qmp_capabilities"}` and parse response
  - [x] Implement `queryStatus()` (e.g., QMP `query-status`)
- [x] Add snapshot methods to `QemuController`
  - [x] `bool createSnapshot(const std::string& name)`
  - [x] `bool loadSnapshot(const std::string& name)`
  - [x] Handle errors from QMP and log them
- [x] Baseline workflow
  - [x] Boot VM manually to a clean state (guest OS installed & configured)
  - [x] From code, call `createSnapshot("baseline_clean")`
  - [x] Verify you can `loadSnapshot("baseline_clean")` and the VM resumes correctly
- [x] Update README with:
  - [x] QMP usage explanation
  - [x] Instructions for creating the first baseline snapshot

---

#### :white_check_mark: Milestone 3 – First Run: “Natural” Execution & Trace Collection

**Goal:** Boot a real guest Linux VM under QEMU, then (eventually) run a sample with basic tracing. Start simple and grow.

##### 3.1 – Boot a real Linux guest under QEMU from code
- [x] Create or pick a qcow2 Linux image (minimal distro).
- [x] Verify QEMU command line manually boots the guest.
- [x] Update `QEMUController` to use the “real guest” command line.
- [x] Start/stop the guest VM from `main.cpp`.

##### 3.2 – Prepare the guest environment
- [x] Install `strace` (or equivalent) in the guest.
- [x] Set up SSH in the guest (optional but recommended).
- [x] Configure QEMU networking with host port forwarding to guest SSH (e.g. host `10022` → guest `22`).

##### 3.3 – Implement RuntimeManager::runFirstPass (v0)
- [x] Add `RuntimeManager` implementation file.
- [x] Implement `runFirstPass(const std::string& samplePath)`:
  - [x] Start VM (restore baseline later; for now just boot).
  - [x] Connect QMP and wait for “running”.
  - [x] (Later) Copy sample into guest.
  - [x] (Later) Invoke guest script to run sample under `strace`.
  - [x] (Later) Fetch trace file back to host.

##### 3.4 – Integrate basic tracing
- [x] Decide on a host→guest mechanism (SSH, virtio-serial, shared folder, etc.).
- [x] Execute `strace -f -o /tmp/sample.strace ./sample` inside guest.
- [x] Retrieve `/tmp/sample.strace` to `artifacts/<sample>/run1/`.
- [x] Implement a minimal `TraceCollector` to summarize syscalls.
---

#### :white_check_mark: Milestone 4 – GDB Remote Controller & Second Run (No LLM Yet)

**Goal:** Scriptable control of a debugged process inside QEMU via GDB remote protocol.

- [x] Extend QEMU launch options
  - [x] Add `-gdb tcp:localhost:1234 -S` (or `-s -S`) for debug runs
  - [x] Ensure VM halts at startup and waits for debugger
- [x] Implement `GdbRemoteController`
  - [x] Connect to `localhost:1234`
  - [x] Implement minimal packet send/receive (RSP – Remote Serial Protocol)
  - [x] Implement:
    - [x] `getStopReason()`
    - [x] `readRegisters()`
    - [x] `continueExecution()` (`c`)
    - [x] `stepInstruction()` (`s`)
  - [x] Implement setting and removing a software breakpoint at a given address (`Z0`/`z0`)
- [x] Simple demo flow (hard-coded)
  - [x] Restore `baseline_clean`
  - [x] Launch QEMU in debug mode
  - [x] Set a breakpoint at a known address in a toy program
  - [x] Run program, confirm breakpoint hit
  - [x] Log registers and resume
- [x] Integrate with `RuntimeManager`
  - [x] Add `runSecondPassDebug()` that:
    - [x] Uses `QemuController` to start in debug mode
    - [x] Uses `GdbRemoteController` to apply a hard-coded debug plan
- [x] Document basic RSP usage and demo in README

---

#### :bangbang: Milestone 5 – LLM Interface & Debug Plan Schema

**Goal:** Let the LLM propose a structured “debug plan” based on first-run traces.

- [x] Define a JSON schema for an LLM “debug plan”
  - [x] Breakpoints (addresses, reasons)
  - :exclamation: Patches (address + bytes or semantic action)
  - [x] Snapshot triggers (when/where to snapshot)
  - [x] Branch exploration hints (“explore alternative path at location X”)
- [x] Implement `LlmInterface`
  - [x] Define `DebugPlan LlmInterface::generateDebugPlan(const TraceSummary& summary, /* static info later */)`
  - [x] For now, stub in a fake implementation that returns a hard-coded plan
  - [x] Later, connect to actual LLM API/tooling
- [x] RuntimeManager integration
  - [x] Add `runTwoPhaseAnalysis(const std::string& samplePath)`:
    - [x] Call `runFirstPass()` → get `TraceSummary`
    - [x] Call `LlmInterface::generateDebugPlan()` → get `DebugPlan`
    - [x] Call `runSecondPassDebug(DebugPlan)` to apply it via `GdbRemoteController`
- [x] Logging
  - [x] Log plan + results (which breakpoints hit, which patches applied)
  - [x] Store under `artifacts/<sample_name>/run2/`
- [x] Update README with:
  - [x] Description of the LLM role
  - [x] Example debug plan JSON

---

#### :white_large_square: Milestone 6 – Branch Exploration & Remaining Runs

**Goal:** Iterate over multiple debug plans to explore alternate branches and collect snapshots.

- [ ] Extend `DebugPlan` to support multiple “scenarios” or branches
  - [ ] Each scenario = a specific combination of breakpoints/patches to explore a path
- [ ] Add branch-aware control to `RuntimeManager`
  - [ ] For each scenario:
    - [ ] Restore `baseline_clean`
    - [ ] Start debug run
    - [ ] Apply scenario’s plan via `GdbRemoteController`
    - [ ] Collect snapshots and logs
- [ ] LLM feedback loop
  - [ ] Allow LLM to:
    - [ ] See which branches were already explored
    - [ ] Propose next branches/scenarios
- [ ] Artifacts & reporting
  - [ ] Organize per-scenario results in `artifacts/<sample_name>/branches/`
  - [ ] Generate a high-level report summarizing discovered behaviors
- [ ] README
  - [ ] Add a section on “Branch exploration workflow”
  - [ ] Include a small example of multiple scenarios on a toy program

---

#### :white_large_square: Milestone 7 – (Later) UI / Visualization (Optional for Thesis Core)

**Goal:** Provide a user-friendly front-end on top of the working backend.

- [ ] Basic CLI quality-of-life
  - [ ] Commands like:
    - [ ] `analyze <sample>`
    - [ ] `run-first-pass <sample>`
    - [ ] `run-second-pass <sample>`
  - [ ] Flags for specifying image, snapshot, timeouts
- [ ] Optional GUI / Web UI (stretch goal)
  - [ ] Visualize:
    - [ ] Timeline of runs & breakpoints
    - [ ] Syscall frequency charts
    - [ ] Snapshot list & metadata
  - [ ] Buttons to trigger new analyses / scenarios
- [ ] Documentation
  - [ ] User guide for running analyses
  - [ ] Developer guide for extending the system

---

### Roadmap Issues

#### :white_check_mark: Milestone 2

- [x] Snapshot QEMU/QMP commands (savevm/loadvm) not found
 
---

#### :white_large_square: Milestone 5

- [ ] Additional fields/interupt types for DebugPlan schema JSON
    - [ ] Memory read/write commands
    - [ ] Byte patching commands
    - [ ] Anti-disassembly / obfuscation handling commands
    - [ ] Encryption/decryption handling commands

### Development Notes

- Snapshot commands failed in minimal test environment; need to verify QEMU build has snapshot support, and how to invoke them correctly.
    - Problem fixed by using qcow2 images and proper QEMU options ('-snapshot', creating per-sample image files). 
---
- During Milestone 3, decided to work a major refactor of "RuntimeManager" to allow for a single orchestration function that can be called
 in CLI with most of the info to configure the run passed in as parameters. This should simplify the UI portion significantly.
- Additionally, switched from using a Fedora Server image to a minimal Debian cloud image for the QEMU guest - greatly reduced boot time per cycle
    - Further optimized run by refactoring groups of processes together, such as all of the static tools are run in the same instance as the diff, etc. This reduced the number of boot cycles required.
    - Total run time for full analysis at roughly 5 minutes per sample through Milestone 3, down from about 10-15 minutes.
---
-  During Milestone 4, modified the GDB pipeline to create a GDB script on-the-fly that is passed to GDB at runtime, rather than issuing commands one at a time over the RSP connection.
    - This greatly simplified the GDBRemoteController implementation, as it no longer needs to handle the full RSP protocol.
    - Additionally, this allows for easier debugging of the GDB commands themselves, as the script can be inspected directly.
    - Script generation is currently very basic, but can be expanded in future milestones to support more complex plans from the LLM.
    - GdbScriptGenerator class builds the script based on a JSON debug plan input - this JSON will be wired up to be created by the LLM based on the earlier artifacts

---
- During Milestone 5, created a basic LLM interface that currently stubs out a hard-coded debug plan.
    - The DebugPlan schema was defined in JSON, with fields for breakpoints, patches, and snapshot triggers.
    - Updated RuntimeManager to include a two-phase analysis function that ties together the first pass trace collection
    - Major portion of Milestone 5 was spent fine-tuning the prompt for the LLM to generate the correct JSON plan, while maintaining enough generalization to work for other samples, rather than overfitting to the test sample used.
    - Future work will involve refining the prompt and possibly adding more context to improve plan quality, including adding additional patern recognitions for various other evasion, obfuscation, anti-RE (debug and/or disassembly), and encryption
    - Also fixed a long-standing issue with running a sample that has an image already - the code to set the image file for the run was embedded in the prepare section, so it was never hit if the image already exists
---

## Core

### Development Roadmap

High-level design of core components and their interactions.

#### Milestone 0 – Project Skeleton & Scaffolding
```
TODO: figure out milestones for core components and their interactions within and without the CoreEngine.
```

---
---

## UI

### Development Roadmap

High-level design of UI components and their interactions.

#### Milestone 0 – Project Skeleton & Scaffolding
```
TODO: figure out milestones for UI components and their interactions.
```

---
---

## MCP

### Development Roadmap

High-level design of MCP components and their interactions.

#### Milestone 0 – Project Skeleton & Scaffolding
```
TODO: figure out milestones for MCP components and their interactions.
```