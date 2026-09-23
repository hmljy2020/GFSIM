# Assertion API

Header: `include/SimAssert.h`. Namespace: `gfsim`. Requires C++11.

Use `GFSIM_ASSERT(condition) << context` for internal invariants. The condition is evaluated once. On failure, the temporary assertion stream throws `gfsim::AssertionError` with the expression, source file, line, function, and optional streamed context. The check remains active in release builds.

```cpp
#include "SimAssert.h"

int main()
{
    const int ready = 1;
    GFSIM_ASSERT(ready == 1) << "input must be ready";
    return 0;
}
```

Use ordinary return values or documented exceptions for recoverable input errors. `SimQueue` uses this interface for empty reads and capacity violations. A testbench can use `ExpectEqual()` for output comparisons, which reports failures through the same assertion type.
