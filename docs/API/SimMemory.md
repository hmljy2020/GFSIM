# SimMemory API

Header: `include/SimMemory.h`. Namespace: `gfsim`. Requires C++11.

`SimMemory` stores exact mapped address ranges and allocates 4 KiB data pages only when nonzero bytes are written. Unwritten mapped bytes read as zero. `SimSystem::GetMemory()` returns the system-owned instance; `SimSystem::Reset()` preserves it. Call `SimMemory::Reset()` to clear mappings and data.

| Method | Contract |
| --- | --- |
| `Map(address, size)` | Map a half-open byte range. Adjacent and overlapping ranges merge; overflow is rejected. |
| `IsMappedRange(address, size)` | Return whether the complete range is mapped. |
| `Load(address, width, signedLoad = false)` | Read 1, 2, 4, or 8 bytes in little-endian order; optionally sign-extend. Invalid width or unmapped access throws. |
| `TryLoad(address, width, signedLoad, value)` | Return `false` for invalid width or unmapped range without changing `value`. |
| `Store(address, value, width)` | Write 1, 2, 4, or 8 bytes in little-endian order. Invalid width or unmapped access throws. |
| `ReadBytes(address, size)` / `WriteBytes(address, bytes)` | Read or write a complete mapped byte range. |
| `LoadElf(path)` | Replace the memory image with `PT_LOAD` segments and return `ElfImage {entry, loadSegments}`. Failure leaves the old image unchanged. |
| `Reset()` | Remove all mapped ranges and data pages. |

`LoadElf` accepts ELF32 and ELF64 little-endian files. It copies each loadable segment's file bytes at its virtual address and leaves the remaining `p_memsz - p_filesz` bytes zero. It validates header and segment bounds. It does not perform relocation, dynamic linking, stack setup, or permission checks. An application that executes the image must supply those policies separately.

```cpp
#include "SimMemory.h"

int main()
{
    gfsim::SimMemory memory;
    memory.Map(0x1000, 16);
    memory.Store(0x1000, 0x1234, 2);
    return memory.Load(0x1000, 2) == 0x1234 ? 0 : 1;
}
```
