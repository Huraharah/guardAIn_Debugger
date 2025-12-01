```mermaid
flowchart TD
    subgraph CallStackMCP[Call Path]
        UI[UI Layer] --> DC[DebuggerCore] --> AM[AnalysisManager] --> IBC[IBackendClient/McpClient] --> MCP[MCP Backend]
    end
    subgraph CallStackRuntime[Call Path]
        UI --> DC --> AM --> IBC --> RT[RuntimeManager]
    end

    subgraph ErrorSources[Possible Error Sources]
        ES1[Network failure <br/> backend unreachable]
        ES2[Ghidra error <br/> analysis failure]
        ES3[JSON parse error]
        ES4[Invalid session state]
    end

    ES1 --> MCP
    ES2 --> MCP
    ES3 --> AM
    ES4 --> DC

    MCP -->|returns success:false<br/>or HTTP error| IBC
    RT -->|VM/container error<br/>or snapshot failure| IBC
    IBC -->|BackendResponse success=false, <br/> errorMessage| AM
    AM -->|marks Session.state=Error<br/>adds BackendMessage log | DC
    DC -->|ErrorResult / status| UI
    UI -->|Display user-friendly error<br/>and possible recovery actions| User

    %% Local core errors
    AM -->|local validation error| DC
    DC -->|cannot perform operation<br/>in current SessionState| UI
```