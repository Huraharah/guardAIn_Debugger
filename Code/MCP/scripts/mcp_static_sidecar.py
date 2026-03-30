#!/usr/bin/env python3
"""
Host-side static sidecar: run Ghidra headless to export JSON, then build llm_context.txt
for the GDB/LLM prompt. Designed to run in parallel with VM static/dynamic passes.

Optional Cursor/MCP workflow:
  - After this script, open Cursor with Ghidra MCP enabled and use the generated
    mcp/cursor_handoff.md as a checklist for deeper passes.
  - Append any MCP findings to mcp/cursor_enrichment.txt (one block per pass);
    they are merged into the LLM context on the next collectStaticArtifacts.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def find_ghidra_headless(ghidra_arg: str | None) -> Path | None:
    env = os.environ.get("GHIDRA_INSTALL_DIR", "").strip()
    roots = []
    if ghidra_arg:
        roots.append(Path(ghidra_arg))
    if env:
        roots.append(Path(env))
    for r in roots:
        if not r.is_dir():
            continue
        win = r / "support" / "analyzeHeadless.bat"
        if win.is_file():
            return win
        nix = r / "support" / "analyzeHeadless"
        if nix.is_file():
            return nix
    return None


def write_skip(out_dir: Path, reason: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    ctx = out_dir / "llm_context.txt"
    ctx.write_text(
        "## MCP / Ghidra static sidecar (skipped)\n\n"
        f"{reason}\n\n"
        "Set GHIDRA_INSTALL_DIR or pass --ghidra to enable headless export.\n",
        encoding="utf-8",
    )
    handoff = out_dir / "cursor_handoff.md"
    handoff.write_text(
        "# Cursor + Ghidra MCP (manual passes)\n\n"
        "1. Import the same binary in Ghidra and connect the Ghidra MCP server in Cursor.\n"
        "2. Ask the model to locate crypto/flag handling, xref `strcmp`-like calls, and summarize.\n"
        "3. Paste findings into `mcp/cursor_enrichment.txt` in the artifacts tree.\n",
        encoding="utf-8",
    )


def run_headless(
    headless: Path,
    project_dir: Path,
    project_name: str,
    binary: Path,
    script_dir: Path,
    json_out: Path,
) -> int:
    project_dir.mkdir(parents=True, exist_ok=True)
    cmd: list[str]
    if headless.suffix.lower() == ".bat":
        cmd = [
            str(headless),
            str(project_dir),
            project_name,
            "-import",
            str(binary),
            "-scriptPath",
            str(script_dir),
            "-postScript",
            "ExportStaticSummary.java",
            str(json_out),
            "-deleteProject",
        ]
    else:
        cmd = [
            str(headless),
            str(project_dir),
            project_name,
            "-import",
            str(binary),
            "-scriptPath",
            str(script_dir),
            "-postScript",
            "ExportStaticSummary.java",
            str(json_out),
            "-deleteProject",
        ]
    print("[mcp_static_sidecar] Running:", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, cwd=str(headless.parent))
    return int(r.returncode)


def json_to_llm_text(data: dict) -> str:
    lines: list[str] = []
    lines.append("## MCP / Ghidra static enrichment (headless export)\n")
    lines.append(f"- Program: `{data.get('program', '')}`\n")
    lines.append(f"- Language: `{data.get('language', '')}`\n")
    lines.append(f"- Image base: `{data.get('image_base', '')}`\n")

    blocks = data.get("memory_blocks") or []
    lines.append("\n### Executable / notable memory blocks\n")
    for b in blocks[:40]:
        if b.get("execute"):
            lines.append(
                f"- {b.get('name')} [{b.get('start')}] size={b.get('size')} "
                f"exec={b.get('execute')} write={b.get('write')}\n"
            )

    imps = data.get("imports") or []
    lines.append("\n### Imports (truncated)\n")
    lines.append(", ".join(imps[:80]) + "\n")

    funcs = data.get("functions") or []
    lines.append("\n### Functions (truncated)\n")
    for f in funcs[:80]:
        lines.append(f"- {f.get('name')} @ {f.get('entry')}\n")

    strings = data.get("strings") or []
    lines.append("\n### Defined strings (keyword filter: flag/crypto/xor/password/usage/etc.)\n")
    for s in strings[:120]:
        lines.append(f"- {s}\n")

    return "".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description="guardAInDBG MCP static sidecar (Ghidra headless)")
    ap.add_argument("--binary", required=True, help="Path to sample binary on host")
    ap.add_argument("--artifacts", required=True, help="Artifacts root directory")
    ap.add_argument("--ghidra", default="", help="Ghidra installation directory (optional)")
    args = ap.parse_args()

    binary = Path(args.binary).resolve()
    artifacts = Path(args.artifacts).resolve()
    mcp_dir = artifacts / "mcp"
    mcp_dir.mkdir(parents=True, exist_ok=True)

    if not binary.is_file():
        write_skip(mcp_dir, f"Binary not found: {binary}")
        return 1

    script_dir = Path(__file__).resolve().parent.parent / "ghidra_scripts"
    if not (script_dir / "ExportStaticSummary.java").is_file():
        write_skip(mcp_dir, f"Ghidra script not found under {script_dir}")
        return 1

    headless = find_ghidra_headless(args.ghidra or None)
    json_out = mcp_dir / "ghidra_export.json"

    if headless is None:
        write_skip(
            mcp_dir,
            "Ghidra analyzeHeadless not found (set GHIDRA_INSTALL_DIR or --ghidra).",
        )
        return 0

    project_dir = mcp_dir / "ghidra_project"
    code = run_headless(
        headless,
        project_dir,
        "guardain_mcp_tmp",
        binary,
        script_dir,
        json_out,
    )
    if code != 0 or not json_out.is_file():
        write_skip(
            mcp_dir,
            f"Ghidra headless failed (exit {code}) or JSON missing. Check console output.",
        )
        return 0

    try:
        data = json.loads(json_out.read_text(encoding="utf-8", errors="replace"))
    except json.JSONDecodeError as e:
        write_skip(mcp_dir, f"Invalid JSON from Ghidra export: {e}")
        return 0

    llm_path = mcp_dir / "llm_context.txt"
    text = json_to_llm_text(data)
    llm_path.write_text(text, encoding="utf-8")

    handoff = mcp_dir / "cursor_handoff.md"
    handoff.write_text(
        "# Ghidra MCP follow-up (optional)\n\n"
        f"- Binary (host): `{binary}`\n"
        f"- Ghidra JSON: `{json_out}`\n"
        "- In Cursor with Ghidra MCP: decompile candidates near xref to strcmp/memcmp, "
        "trace flag decryption, note algorithm + ciphertext locations.\n"
        "- Write condensed notes to `mcp/cursor_enrichment.txt` for the next run.\n",
        encoding="utf-8",
    )
    print("[mcp_static_sidecar] Wrote", llm_path, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
