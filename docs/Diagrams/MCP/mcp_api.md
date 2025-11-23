# **mcp_api.md — MCP Backend API Specification**

**Masters Thesis Project — guardAIn Debugger**
**Version:** 0.1 (Draft)

---

# 1. Overview

The **MCP Backend** is a Python-based microservice that provides:

* Static analysis via **Ghidra headless scripts**
* Dynamic explanation and comparison via **LLM/AI models**
* A clean and typed JSON API for the **C++ Core Engine**
* A uniform `BackendResponse` contract for error handling

All communication between the C++ DebuggerCore and the MCP backend is performed using:

* **HTTP POST** requests
* **JSON bodies**
* **JSON responses**

This document defines the **official request/response contract** for all backend endpoints.

---

# 2. Architectural Context

The MCP backend sits between:

### C++ Core

* Orchestrates sessions, static analysis, snapshots
* Sends structured requests to the backend
* Parses results into models defined in `/docs/Diagrams/Core/data_model.md`

### Python Backend (MCP)

* Routes requests to the correct handler
* Calls out to:

  * Ghidra (Java) headless scripts
  * AI/LLM models
  * Local analysis utilities
* Returns structured JSON responses

### External Tools

* Ghidra
* Future CUDA-based or local ML models

---

# 3. Conventions

### 3.1 HTTP Method

All endpoints use:

```
POST /<endpoint>
Content-Type: application/json
```

### 3.2 Response Envelope

Every endpoint must return:

```json
{
  "success": true,
  "error": null,
  "data": { ... }
}
```

Or in case of error:

```json
{
  "success": false,
  "error": "Human-readable error message.",
  "data": null
}
```

### 3.3 Timestamps

Formatted as ISO8601 strings (UTC):

```
2025-11-22T14:33:05Z
```

### 3.4 Integers

*MEMORY ADDRESSES* are always returned as **strings**, not raw integers:

```json
"0x401000"
```

This ensures compatibility across Python, Java, C++ and JSON.

---

# 4. Endpoint Summary Table

| Endpoint                         | Purpose                                      | Returns                |
| -------------------------------- | -------------------------------------------- | ---------------------- |
| `/ping`                          | Connectivity + health check                  | `{status: "ok"}`       |
| `/ghidra/analyze`                | Full static analysis of binary               | `StaticAnalysisResult` |
| `/ghidra/behavior_scan` (future) | Behavior-focused scan                        | TBA                    |
| `/context/explain`               | LLM-based explanation of a snapshot          | `Explanation`          |
| `/context/compare`               | AI-assisted diff between snapshots           | `SnapshotComparison`   |
| `/context/snapshot` (optional)   | Dynamic snapshot capture (if backend-driven) | `Snapshot`             |
| `/naming/suggest`   (future)     | Propose better names for functions/labels    | TBD           |
| `/ghidra/apply_renames` (future) | Persist chosen renames into Ghidra & session | TBD              |


---

# 5. Endpoints in Detail

---

## 5.1 `/ping`

### Description

Simple health check to verify MCP backend availability.

### Request

```json
{}
```

### Response (Success)

```json
{
  "success": true,
  "error": null,
  "data": {
    "status": "ok",
    "version": "0.1"
  }
}
```

---

## 5.2 `/ghidra/analyze`

### Description

Runs full static analysis of a binary using Ghidra headless mode.

### Request Schema

```json
{
  "session_id": "string",
  "binary_path": "C:/path/to/binary.exe"
}
```

### Response (`StaticAnalysisResult`)

```json
{
  "success": true,
  "error": null,
  "data": {
    "summary": "Analysis summary text...",
    "functions": [
      {
        "name": "sub_401000",
        "address": "0x401000",
        "size_bytes": 96,
        "summary": "Initializes network stack",
        "suspicious_score": 0.82
      }
    ],
    "strings": [
      {
        "value": "http://malicious.example.com",
        "location": "0x402000"
      }
    ],
    "imports": [
      {
        "name": "CreateRemoteThread",
        "library": "kernel32.dll"
      }
    ],
    "suspicious_regions": [
      {
        "id": "sr01",
        "description": "Writes to unusual registry paths",
        "severity": "High",
        "related_functions": ["sub_401000"],
        "tags": ["registry", "persistence"]
      }
    ],
    "warnings": [
      "Binary is stripped; symbol names are synthetic."
    ]
  }
}
```

### Error Example

```json
{
  "success": false,
  "error": "Ghidra failed: Could not open binary.",
  "data": null
}
```

---

## 5.3 `/context/explain`

### Description

Generates an AI explanation for a captured program state.

### Request Schema

```json
{
  "session_id": "abc123",
  "snapshot_id": "snap001",
  "call_stack": [
    {
      "function": "foo",
      "source": "main.cpp:42",
      "instruction_ptr": "0x0040201A"
    }
  ],
  "locals": {
    "status": "ERROR_TIMEOUT",
    "retry_count": 3
  },
  "notes": ["Breakpoint hit", "User requested explanation"],
  
  "static_context": {   // optional hybrid field
    "related_functions": [
      {
        "name": "foo",
        "summary": "Performs network wait with 5-second timeout."
      }
    ]
  }
}
```

### Response (`Explanation`)

```json
{
  "success": true,
  "error": null,
  "data": {
    "summary": "The program timed out waiting for a network response.",
    "suspected_issue": "The timeout value may be too low.",
    "important_signals": [
      "status == ERROR_TIMEOUT",
      "foo() summary indicates fixed wait"
    ],
    "raw_model_output": "... full LLM output ..."
  }
}
```

---

## 5.4 `/context/compare`

### Description

Produces an AI-assisted explanation of differences between two snapshots.

### Request Schema

```json
{
  "session_id": "abc123",
  "snapshot_a": {
    "id": "snap1",
    "locals": { "counter": 10, "state": "CONNECTING" }
  },
  "snapshot_b": {
    "id": "snap2",
    "locals": { "counter": 10, "state": "CLOSED" }
  }
}
```

or full snapshots (both allowed):

```json
{
  "session_id": "abc123",
  "snapshot_a": { ...full snapshot... },
  "snapshot_b": { ...full snapshot... }
}
```

### Response (`SnapshotComparison`)

```json
{
  "success": true,
  "error": null,
  "data": {
    "summary": "Connection state changed from CONNECTING to CLOSED.",
    "key_changes": [
      {
        "field_path": "locals.state",
        "before_value": "CONNECTING",
        "after_value": "CLOSED",
        "reason": "Remote host reset the connection"
      }
    ],
    "raw_diff_json": "{ ... }"
  }
}
```

---

## 5.5 `/context/snapshot` (optional endpoint)

This is included **only if snapshot collection is performed by the backend** instead of by the C++ core.

### Request

```json
{
  "session_id": "abc123",
  "capture_locals": true,
  "capture_stack": true
}
```

### Response

```json
{
  "success": true,
  "error": null,
  "data": {
    "snapshot": { ... id, call_stack, locals, timestamp ... }
  }
}
```

---

## 5.6 `/naming/suggest`

Given a set of functions/labels/variables (with context), propose better names.

### Request

```json
{
  "session_id": "abc123",
  "targets": [
    {
      "id": "0x401000",
      "kind": "Function",
      "current_name": "sub_401000",
      "summary": "Initializes network and sends beacon...",
      "context": {
        "strings": ["http://example.com/beacon"],
        "imports": ["send", "socket"]
      }
    }
  ]
}
```

### Response

```json
{
  "success": true,
  "error": null,
  "data": {
    "suggestions": [
      {
        "id": "0x401000",
        "kind": "Function",
        "current_name": "sub_401000",
        "proposed_name": "init_and_send_beacon",
        "confidence": 0.91,
        "rationale": "Function initializes Winsock, builds HTTP request, and sends..."
      }
    ]
  }
}
```

The UI can then show these suggestions and let the user accept/modify them.

---

## `/ghidra/apply_renames`

Apply one or more renames in the Ghidra project and return the updated symbol info.

### Request

```json
{
  "session_id": "abc123",
  "renames": [
    {
      "id": "0x401000",
      "kind": "Function",
      "old_name": "sub_401000",
      "new_name": "init_and_send_beacon"
    },
    {
      "id": "0x402000",
      "kind": "GlobalLabel",
      "old_name": "DAT_402000",
      "new_name": "CONFIG_URL"
    }
  ]
}
```

### Response

```json
{
  "success": true,
  "error": null,
  "data": {
    "applied": [
      {
        "id": "0x401000",
        "kind": "Function",
        "old_name": "sub_401000",
        "new_name": "init_and_send_beacon",
        "status": "ok"
      },
      {
        "id": "0x402000",
        "kind": "GlobalLabel",
        "old_name": "DAT_402000",
        "new_name": "CONFIG_URL",
        "status": "ok"
      }
    ],
    "failed": []
  }
}
```

If something goes wrong (name collision, invalid target, etc.):

```json
{
  "success": true,
  "error": null,
  "data": {
    "applied": [],
    "failed": [
      {
        "id": "0x401000",
        "kind": "Function",
        "old_name": "sub_401000",
        "new_name": "main",
        "status": "error",
        "message": "Name 'main' already used by function at 0x400000."
      }
    ]
  }
}
```

Note: top-level success is still true because the request was processed; per-item errors are in failed[].

# 6. Error Handling

All failures must return:

```json
{
  "success": false,
  "error": "Human-readable explanation",
  "data": null
}
```

Backend SHOULD set appropriate HTTP statuses (`200` for handled errors is fine; MCP-style).

---

# 7. Versioning

### File Version

`version: 0.1-draft`

### Backend Version Negotiation

The C++ Core may call:

```http
POST /ping
```

Backend should return:

```json
{
  "status": "ok",
  "version": "0.1"
}
```

If major versions mismatch, DebuggerCore can warn the user.

---

# 8. Future Endpoints (Reserved)

| Endpoint                 | Purpose                            |
| ------------------------ | ---------------------------------- |
| `/ghidra/behavior_scan`  | Malware-pattern scanning           |
| `/analysis/flowgraph`    | Control/data flow graph extraction |
| `/context/trace`         | Timeline reconstruction            |
| `/context/explain_trace` | Higher-level behavioral summary    |
| `/ghidra/rename_function`| Single function rename             |
| `/ghidra/rename_variable`| Single variable rename             |
| `/ghidra/rename_label`   | Single label rename                |

These are **not implemented** in v0.1 but are reserved for v1.x.