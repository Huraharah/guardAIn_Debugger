```mermaid
flowchart LR
    subgraph UIThread[UI Thread]
        UI[UI Layer -- XAML Views] --> VM[ViewModels / UI Logic]
        VM --> DC[DebuggerCore API]
    end

    subgraph CoreThread[Core / Worker Threads]
        DC --> TM[Task/Job Manager]
        TM --> AM[AnalysisManager]
        TM --> SM[SessionManager]
    end

    subgraph BackendThread[Backend Communication]
        AM --> BC[IBackendClient/McpClient]
        BC --> NET[HTTP Client / IPC]
    end

    subgraph External[External Processes]
        NET --> MCP[MCP Backend Process]
        MCP --> GH[Ghidra Headless]
        MCP --> LLM[LLM / AI Service]
    end
