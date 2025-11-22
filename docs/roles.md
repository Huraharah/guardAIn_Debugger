## Persona 1 – “Alex, Systems Developer”

### Goal: Debug complex C++/Rust/whatever systems that misbehave in non-obvious ways.

Pain points:

Traditional debuggers don’t explain why something is happening.

Multi-step, stateful bugs are hard to reason about from raw stack traces.

#### Needs from guardAIn Debugger:

See a structured timeline of execution.

Ask “What changed between here and here?” and get explanations.

Correlate memory/state changes with code regions.

## ---------------------------------------------------------------------------

## Persona 2 – “Sam, Reverse Engineer / Security Analyst”

### Goal: Understand what a suspicious binary does and how.

Pain points:

Ghidra output is powerful but overwhelming.

Manually stitching together control-flow, strings, and behavior is time-consuming.

#### Needs from guardAIn Debugger:

Summarize functions and code regions in human terms.

Highlight behavior patterns (networking, persistence, crypto, etc.).

Trace suspicious behavior with call graphs, not just disassembly.

## ---------------------------------------------------------------------------

## Persona 3 – “Dr. Lee, Researcher / Instructor”

### Goal: Use the tool as a teaching/demo platform for debugging and analysis workflows.

Pain points:

Students struggle to connect theory (call stacks, memory, CFGs) with real-world tools.

#### Needs from guardAIn Debugger:

Reproducible sessions they can save, replay, and share.

Clean explanations of decisions / analyses.

Ability to show “before/after” contexts.