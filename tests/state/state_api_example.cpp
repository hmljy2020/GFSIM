#include <cstdint>
#include "SimState.h"
#include "SimAssert.h"

int main()
{
    gfsim::SimState<std::uint64_t> pc("example.pc", 0x1000);
    gfsim::SimState<unsigned> count("example.count", 0);
    pc.Build();
    count.Build();
    pc.Write(0x1004);
    count.Write(1);
    count.Write(2);
    GFSIM_ASSERT(pc.Read() == 0x1000 && count.Read() == 0);
    pc.Xfer();
    count.Xfer();
    GFSIM_ASSERT(pc.Read() == 0x1004 && count.Read() == 2);
    pc.Write(0x1008);
    pc.Reset();
    count.Reset();
    pc.Xfer();
    count.Xfer();
    GFSIM_ASSERT(pc.Read() == 0x1000 && count.Read() == 0);
}
