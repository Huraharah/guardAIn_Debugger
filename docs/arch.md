
+-------------------------------------------------------------+
|                        UI Layer / Win                      |
|  - App.xaml / MainPage.xaml                                |
|  - Views: SessionListView, AnalysisView, SnapshotView      |
|  - Uses DebuggerCore API                                   |
+-------------------------------------------------------------+
                            
+-------------------------------------------------------------+-------------------------------------------------------------+
|                   Core Engine / C++ Library                 |                     RuntimeManager (C++)                    |
|  Namespace: guardain::core                                  |  Namespace: guardain::runtime                               |
|                                                             |                                                             |
|  - DebuggerCore                                             |  - Lives in Core Engine, interfaces with Runtime Backend    |
|      - manages sessions, coordinates requests               |  - Orchestrates:									        |
|  - Session / Snapshot / AnalysisResult models               |    -VM/container lifecycle management                       |
|  - IBackendClient (interface)                               |    - Target process launch                                  |
|  - McpClient (implementation of IBackendClient)             |    - Snapshot collection (raw context)                      |
|                                                             |  - Feeds captured context to AnalysisManager                |
|  Responsibilities:                                          |                                                             |
|   - Translate user actions into backend requests            |                                                             |
|   - Maintain session state and artifacts                    |                                                             |
|   - Present clean, structured data to UI                    |                                                             |
+-------------------------------------------------------------+-------------------------------------------------------------+
                                                            
```                               
        intermediaries between C++ core and MCP/Runtime backends (HTTP/WebSocket/stdio, JSON)       
```
                                                             
+-------------------------------------------------------------+----------------------------------------------------------+
|                   MCP Backend / Python                      |                  Runtime Backend / QEMU                  |
|  - mcp_server/main.py                                       |                                                          |
|  - handlers/context.py                                      |  - QEMU / container Manager                              |
|  - handlers/ghidra.py                                       |    - create/teardown sandboxes                           |
|  - handlers/model_introspection.py                          |    - configure isolation (network, FS, CPU/mem)			 |
|                                                             |  - Debug/Control interface                               |
|  Responsibilities:                                          |    - GDB server / debug adapter /  custom protocols      |
|   - Implement endpoints:                                    |  - SnapshotCollector                                     |
|       - /ghidra/analyze                                     |    - extract call stack, registers, key memory/states    |
|       - /ghidra/behavior_scan                               |                                                          |
|       - /context/explain                                    |  Responsibilities:                                       |
|       - and others                                          |     - Launch target program in isolated environment      |
|   - Interface with Ghidra (Java)                            |     - Provide runtime control (start/stop/break/step)     |
|   - Call AI/LLM/ML models as tools                          |     - Capture raw runtime context for RuntimeManager     |
|   - Ensure responses are structured JSON for C++ core       |                                                          |
+------------------------------------------------------------------------------------------------------------------------+

+-------------------------------------------------------------+
|               External Tools / AI / Ghidra                  |
|  - Ghidra (headless, scripts)                               |
|  - AI models / LLMs / CUDA-accelerated analyzers (future)   |
+-------------------------------------------------------------+

