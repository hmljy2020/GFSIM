#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "SimMemory.h"
#include "SimModule.h"

namespace {

class MemoryRoot final : public gfsim::SimModule {
public:
    explicit MemoryRoot(gfsim::SimSystem* system) : SimModule("memory_root", system) {}
    void Build() override { SetObjectCapacity(0); }
    void Work() override {}
    void Xfer() override {}
    void Reset() override {}
    void ReportStat() override {}
};

void Check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void Put(std::vector<std::uint8_t>& bytes, std::size_t offset,
         std::uint64_t value, unsigned width)
{
    for (unsigned index = 0; index < width; ++index) {
        bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (8 * index));
    }
}

void WriteFile(const std::string& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Check(static_cast<bool>(file), "ELF fixture write must succeed");
}

std::vector<std::uint8_t> MakeElf64()
{
    std::vector<std::uint8_t> bytes(0x103, 0);
    bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 2; bytes[5] = 1; bytes[6] = 1;
    Put(bytes, 24, 0x4000, 8); // entry
    Put(bytes, 32, 64, 8);     // program header offset
    Put(bytes, 52, 64, 2);     // ELF header size
    Put(bytes, 54, 56, 2);     // program header size
    Put(bytes, 56, 1, 2);      // program header count
    Put(bytes, 64, 1, 4);      // PT_LOAD
    Put(bytes, 72, 0x100, 8);  // file offset
    Put(bytes, 80, 0x4000, 8); // virtual address
    Put(bytes, 96, 3, 8);      // file size
    Put(bytes, 104, 8, 8);     // memory size
    bytes[0x100] = 0x11;
    bytes[0x101] = 0x22;
    bytes[0x102] = 0x33;
    return bytes;
}

std::vector<std::uint8_t> MakeElf32()
{
    std::vector<std::uint8_t> bytes(0x82, 0);
    bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 1; bytes[5] = 1; bytes[6] = 1;
    Put(bytes, 24, 0x2000, 4);
    Put(bytes, 28, 52, 4);
    Put(bytes, 40, 52, 2);
    Put(bytes, 42, 32, 2);
    Put(bytes, 44, 1, 2);
    Put(bytes, 52, 1, 4);
    Put(bytes, 56, 0x80, 4);
    Put(bytes, 60, 0x2000, 4);
    Put(bytes, 68, 2, 4);
    Put(bytes, 72, 4, 4);
    bytes[0x80] = 0xaa;
    bytes[0x81] = 0xbb;
    return bytes;
}

void TestMemoryAccess()
{
    gfsim::SimMemory memory;
    memory.Map(0x1000, 16);
    memory.Map(0x1010, 16); // adjacent regions merge
    memory.Store(0x100f, 0x1234, 2);
    Check(memory.Load(0x100f, 2) == 0x1234,
          "cross-region load/store must preserve little-endian value");
    memory.Store(0x1002, 0x80, 1);
    Check(memory.Load(0x1002, 1, true) == 0xffffffffffffff80ULL,
          "signed byte load must sign extend");
    memory.WriteBytes(0x1004, std::vector<std::uint8_t>{1, 2, 3});
    Check(memory.ReadBytes(0x1004, 3) == std::vector<std::uint8_t>({1, 2, 3}),
          "byte-range access must round trip");

    memory.Map(0x1fff, 2);
    memory.Store(0x1fff, 0xabcd, 2);
    Check(memory.Load(0x1fff, 2) == 0xabcd,
          "a scalar access must cross sparse page boundaries");

    std::uint64_t value = 99;
    Check(!memory.TryLoad(0x101f, 2, false, value) && value == 99,
          "TryLoad must reject a range crossing into unmapped memory");
    Check(!memory.IsMappedRange(0x3000, 1), "unmapped address must be distinct from zero");
    memory.Reset();
    Check(!memory.IsMappedRange(0x1000, 1), "Reset must remove mappings");

    gfsim::SimSys system;
    system.GetMemory().Map(0x3000, 4);
    system.GetMemory().Store(0x3000, 0x5a, 1);
    system.AddModule(std::make_shared<MemoryRoot>(&system));
    system.Build();
    system.Reset();
    Check(system.GetMemory().Load(0x3000, 1) == 0x5a,
          "system Reset must preserve the loaded memory image");
}

void TestElfLoading(const std::string& directory)
{
    gfsim::SimMemory memory;
    memory.Map(0x1000, 4);
    memory.Store(0x1000, 7, 1);

    const std::string path = directory + "/simmemory-fixture.elf";
    WriteFile(path, MakeElf64());
    const auto loaded64 = memory.LoadElf(path);
    Check(loaded64.entry == 0x4000 && loaded64.loadSegments == 1 &&
              memory.Load(0x4000, 4) == 0x00332211 &&
              memory.Load(0x4003, 1) == 0 &&
              !memory.IsMappedRange(0x1000, 1),
          "ELF64 loader must replace memory, copy PT_LOAD, and zero BSS");

    auto invalid = MakeElf64();
    Put(invalid, 104, 2, 8); // memsz < filesz
    WriteFile(path, invalid);
    bool rejected = false;
    try {
        memory.LoadElf(path);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected && memory.Load(0x4000, 1) == 0x11,
          "invalid ELF must leave the prior memory image intact");

    WriteFile(path, MakeElf32());
    const auto loaded32 = memory.LoadElf(path);
    Check(loaded32.entry == 0x2000 && loaded32.loadSegments == 1 &&
              memory.Load(0x2000, 2) == 0xbbaa &&
              memory.Load(0x2002, 2) == 0,
          "ELF32 loader must map file bytes and zero-fill memory tail");
    std::remove(path.c_str());
}

} // namespace

int main(int argc, char** argv)
{
    try {
        Check(argc == 2, "test requires a temporary directory");
        TestMemoryAccess();
        TestElfLoading(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
