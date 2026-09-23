#pragma once

#include <memory>
#include <ostream>

namespace gfsim_test {

struct Number {
    explicit Number(int initial) : value(initial) {}
    int value;
};

struct WideNumber {
    explicit WideNumber(long initial) : value(initial) {}
    long value;
};

inline std::shared_ptr<Number> MakeNumber(int value)
{
    return std::make_shared<Number>(value);
}

inline std::shared_ptr<WideNumber> MakeWideNumber(long value)
{
    return std::make_shared<WideNumber>(value);
}

inline std::ostream& operator<<(std::ostream& output, const Number& packet)
{
    return output << packet.value;
}

inline std::ostream& operator<<(std::ostream& output, const WideNumber& packet)
{
    return output << packet.value;
}

} // namespace gfsim_test
