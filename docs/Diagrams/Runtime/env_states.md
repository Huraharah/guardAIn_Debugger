```mermaid
stateDiagram-v2
    [*] --> NotCreated

    NotCreated --> Creating: createEnvironment(config)
    Creating --> Ready: env created<br/>sandbox ready
    Creating --> Error: creation failed<br/>(e.g. QEMU error)

    Ready --> Running: launchTarget()
    Ready --> Destroying: destroyEnvironment()

    Running --> Paused: pauseAllTargets()
    Paused --> Running: resumeTargets()

    Running --> Stopping: destroyEnvironment()<br/>(all targets terminating)
    Paused --> Stopping: destroyEnvironment()

    Stopping --> Terminated: cleanup complete
    Stopping --> Error: shutdown failed

    Error --> Destroying: attempt cleanup
    Destroying --> Terminated

    Terminated --> [*]
```