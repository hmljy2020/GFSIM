# PURE methods and atomic queue transactions

`PURE` is an empty declaration marker from `include/SimModule.h`. Put it before a method that computes outputs from explicit input values without changing module state. A module may define many `PURE` methods. Use `PURE static` where possible so the compiler prevents access to instance fields.

For a queue-to-queue operation, `Work()` follows this order:

1. Check **every** required input for nonempty state and **every** selected output for enough free capacity, plus any other firing conditions. For `n` writes to one output, check `!Full(n - 1)`.
2. Inspect input heads with `Front()` or `Peek()` only. `Peek()` supplies a `const T&` to `PURE` methods. Do not mutate the input message.
3. Compute all output packets locally, then write them to the output queues.
4. After output writes succeed, `Pop()` each consumed input and record progress.

If any readiness condition fails, the transaction does not fire: no input is consumed and no output is written. `Read()` is unsuitable for deciding whether to fire because it consumes before output admission. For class payloads, `Front()` yields a `shared_ptr<T>` handle, while `Peek()` yields `const T&`. A new output message can be allocated by the module that produces it.

```cpp
PURE static int Double(int value) { return value * 2; }

void Work() override
{
    Transfer();
    WorkChildren(); // Local blocking must not skip child evaluation.
}

void Transfer()
{
    if (input_->Empty() || output_->Full()) return;
    const int result = Double(input_->Peek());
    output_->Write(result);
    input_->Pop();
    GetSim()->MarkProgress();
}
```

The complete [atomic transaction test](../../tests/pure/test_pure_transaction.cpp) uses a class payload and backpressure to verify that a blocked output leaves the input pointer and object unchanged. The guarantee describes model-level admission and commit during normal execution; host allocation failures propagate as exceptions.
