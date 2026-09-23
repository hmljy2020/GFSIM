# Logger API

Header: `include/Logger.h`. Namespace: `gfsim`.

`Logger` emits one line per stream-style call. A `SimSystem` owns one logger and updates its cycle before each `Work()` phase. Concrete `SimModule` classes use `Log()`.

| Method | Meaning |
| --- | --- |
| `Logger(std::ostream& output = std::clog)` | Set a non-owning output sink. |
| `SetOutput(std::ostream&)` | Replace the sink; the caller keeps it alive. |
| `SetLevel(LogLevel)` / `Level()` | Configure/read the severity threshold. Default is `Warn`; `Off` suppresses all lines. |
| `SetCycle(uint64_t)` / `Cycle()` | Set/read the cycle field. The system manages this during stepping. |
| `Debug(unit, stage)` / `Info` / `Warn` / `Error` | Return a temporary stream line. The line is emitted when the expression ends. |
| `Start(level, unit, stage)` | Emit with an explicit `LogLevel`. |

Example from a module:

```cpp
Log().Info("dispatch", "accept") << "request=" << requestId;
Log().Warn("dispatch", "stall") << "output_full";
```

Output format: `[C:<cycle>][<unit>.<stage>][<LEVEL>] <message>`. Use stable unit and stage strings so logs can be filtered or compared in tests.
