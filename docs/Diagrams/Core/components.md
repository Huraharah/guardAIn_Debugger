```mermaid
flowchart LR
    UI[UI Layer -- Win/XAML] 
    DC[DebuggerCore]
    SM[SessionManager]
    AM[AnalysisManager]
    BC[IBackendClient / McpClient]
    MCP[MCP Backend -- Python]
    GH[Ghidra -- Java]
    LLM[LLM / AI Model]

    UI --> DC

    DC --> SM
    DC --> AM

    AM --> BC

    BC --> MCP

    MCP --> GH
    MCP --> LLM
```