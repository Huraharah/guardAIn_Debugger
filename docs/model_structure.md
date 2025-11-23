```mermaid
mindmap
  root((guardAIn_Debugger))
    docs
      Core diagrams
      MCP API
      Requirements
    code
      core
        include
        src
      ui
        views
        viewmodels
      backend
        mcp_py
          handlers
    scripts
    config
```
```text
guardAIn_Debugger/
├── docs/
│   ├── arch.md
│   ├── roles.md
│   ├── use_cases.md
│   ├── user_stories.md
│   ├── mcp_api.md
│   └── Diagrams/
│       ├── Core/
│       │   ├── core_arch.drawio
│       │   ├── components.md
│       │   ├── dependencies.md
│       │   └── ...   // other core diagrams
│       ├── UI/
│       └── MCP/
│           └── mcp_sequences.md
│
├── code/
│   ├── core/
│   │   ├── include/
│   │   │   ├── DebuggerCore.hpp
│   │   │   ├── SessionManager.hpp
│   │   │   ├── ...    // other core headers
│   │   │   └── Models/
│   │   │       ├── StaticAnalysisResult.hpp
│   │   │       ├── Snapshot.hpp
│   │   │       ├── SnapshotComparison.hpp
│   │   │       ├── BackendMessage.hpp
│   │   │       └── Types.hpp   // enums, aliases
│   │   └── src/
│   │       ├── DebuggerCore.cpp
│   │       ├── SessionManager.cpp
│   │       ├── AnalysisManager.cpp
│   │       ├── McpClient.cpp
│   │       └── Models/
│   ├── ui/
│   │   ├── include/
│   │   └── src/
│   │       ├── App.xaml
│   │       ├── MainPage.xaml
│   │       └── ViewModels/
│   └── backend/   // or mcp_py/
│       └── mcp_py/
│           ├── mcp_server/
│           │   ├── __init__.py
│           │   ├── main.py
│           │   └── handlers/
│           │       ├── context.py
│           │       ├── ghidra.py
│           │       └── naming.py
│           └── requirements.txt
│
├── scripts/
└── .gitignore
```