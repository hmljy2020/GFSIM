# Reporter API

Header: `include/Reporter.h`. Namespace: `gfsim`.

`Reporter` formats module statistics into an `std::ostream`. A `SimSystem` owns one reporter; concrete `SimModule` classes use `Report()` inside `ReportStat()`.

| Method | Meaning |
| --- | --- |
| `Reporter(std::ostream& output = std::cout)` | Set a non-owning output sink. |
| `SetOutput(std::ostream&)` | Replace the sink; the caller keeps it alive. |
| `ReportTitle(title)` | Write a section heading padded with `=`. |
| `ReportVal(name, integral)` | Write an integer counter. |
| `ReportVal(name, double, precision = 2)` | Write a fixed-precision value. |
| `ReportAvg(name, numerator, denominator)` | Write a two-decimal ratio or `N/A` for zero denominator. |
| `ReportPct(name, numerator, denominator)` | Write a two-decimal percentage or `N/A`. |
| `ReportValAndPct(name, numerator, denominator)` | Write count and percentage together. |

Example from a module:

```cpp
void MyModule::ReportStat()
{
    auto& report = Report();
    report.ReportTitle("MyModule Stats");
    report.ReportVal("Accepted requests", accepted_);
    report.ReportValAndPct("Blocked cycles", blockedCycles_, workCycles_);
    ReportChildren();
}
```

Counter names should state what is counted; percentages should name the denominator in the module document.
