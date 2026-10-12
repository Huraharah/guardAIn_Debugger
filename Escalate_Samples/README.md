# Escalate Reverse Engineering Challenge Series

## Overview

This directory contains the complete 11-level Escalate reverse-engineering challenge collection used in computer science and cybersecurity instruction at the University of North Georgia.

The challenges progressively introduce more sophisticated binary validation, encryption, debugging obstacles, and anti-analysis techniques.

The binaries are provided as research and educational artifacts to support independent analysis and reproducibility of the experiments conducted using the guardAInDBG framework.

## Challenge Progression

| Level | Primary challenge |
|---|---|
| 1 | Hardcoded plaintext flag comparison |
| 2 | Runtime construction of the expected flag on the stack |
| 3 | Runtime XOR-based flag decryption |
| 4 | Encrypted input validation with increased control-flow complexity |
| 5 | More advanced encryption and flag-validation logic |
| 6 | Signal-based anti-debugging using `SIGTRAP` |
| 7 | Debugger detection through process-name inspection |
| 8 | Salsa-based encryption, signal handling, and anti-patching behavior |
| 9 | More complex stream-cipher-based validation and integrated encryption logic |
| 10 | Encrypted machine code embedded in the `.data` section, requiring extraction and decryption |
| 11* | Advanced anti-disassembly and flag-decryption behavior, including destructive data handling |

*The mapping of the final challenge to Level 11 is provisional. Historical course materials used overlapping Level 10 labels for two different binaries.*

The descriptions summarize the principal mechanisms identified in the accompanying research and historical reverse-engineering exercises. Individual binaries and course variants may differ.

## Scope of the FTC 2026 Research

**Only Levels 1–6 were included in the experimental evaluation reported at the Future Technologies Conference 2026.**

The evaluation consisted of:

- Six Linux x86-64 challenge levels
- Two LLM systems
- Twenty independent runs per model per level
- A maximum of ten reasoning iterations per run
- 240 total experimental runs

A fully successful run required both recovery of the correct flag and successful validation through execution of the original challenge binary.

Levels 7–11 are included for completeness and to facilitate future research. **No experimental performance results for these levels are claimed in the conference paper.**

## Binary Variants and Reproducibility

The Escalate challenges were used across undergraduate and graduate reverse-engineering courses. Some versions retain the same underlying challenge mechanisms while using different embedded flag values.

Researchers attempting to reproduce the published results should use the exact Linux binaries corresponding to the FTC 2026 experimental dataset. Different binary variants may produce different flags and may require different analysis steps.

## Provenance and Redistribution

These challenge binaries were obtained through reverse-engineering coursework at the University of North Georgia.

Their inclusion in this repository was discussed with and approved by Dr. Bryson Payne, who provided the challenge materials for instructional use.

The binaries are redistributed here for educational use, research transparency, and experimental reproducibility.

## Security Notice

These files are executable reverse-engineering challenges and should be treated as untrusted binaries.

Analysis should be conducted in an isolated environment using appropriate debugging, virtualization, and system-monitoring tools.

## Associated Research

**Schoolcraft, A., & Payne, B. R. (2026).** *An AI-Assisted Framework for Automated Malware Reverse Engineering and Analysis.* Future Technologies Conference 2026.

For implementation details, evaluation methodology, and results, refer to the conference manuscript and the main repository documentation.