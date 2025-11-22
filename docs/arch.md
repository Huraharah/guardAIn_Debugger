+-------------------------------------------------------------+
|                        UI Layer / Win                      |
|  - App.xaml / MainPage.xaml                                |
|  - Views: SessionListView, AnalysisView, SnapshotView      |
|  - Uses DebuggerCore API                                   |
+-------------------------------------------------------------+
                            
+-------------------------------------------------------------+
|                   Core Engine / C++ Library                 |
|  Namespace: guardain::core                                  |
|                                                             |
|  - DebuggerCore                                             |
|      - manages sessions, coordinates requests               |
|  - Session / Snapshot / AnalysisResult models               |
|  - IBackendClient (interface)                               |
|  - McpClient (implementation of IBackendClient)             |
|                                                             |
|  Responsibilities:                                          |
|   - Translate user actions into backend requests            |
|   - Maintain session state and artifacts                    |
|   - Present clean, structured data to UI                    |
+-------------------------------------------------------------+
                             
                 (HTTP/WebSocket/stdio, JSON)
        intermediaries between C++ core and Python MCP
                              
+-------------------------------------------------------------+
|                   MCP Backend / Python                      |
|  - mcp_server/main.py                                       |
|  - handlers/context.py                                      |
|  - handlers/ghidra.py                                       |
|  - handlers/model_introspection.py                          |
|                                                             |
|  Responsibilities:                                          |
|   - Implement endpoints:                                    |
|       - /ghidra/analyze                                     |
|       - /ghidra/behavior_scan                               |
|       - /context/explain                                    |
|       - /context/compare                                    |
|   - Interface with Ghidra (Java)                            |
|   - Call AI/LLM/ML models as tools                          |
|   - Ensure responses are structured JSON for C++ core       |
+-------------------------------------------------------------+

+-------------------------------------------------------------+
|               External Tools / AI / Ghidra                  |
|  - Ghidra (headless, scripts)                               |
|  - AI models / LLMs / CUDA-accelerated analyzers (future)   |
+-------------------------------------------------------------+
