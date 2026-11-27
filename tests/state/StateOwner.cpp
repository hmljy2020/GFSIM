#include "StateOwner.h"
#include "SimAssert.h"

namespace state_test {

StateOwner::StateOwner(gfsim::SimSystem* system, std::uint64_t completionCycles,
                       bool enabled)
    : SimModule("state_owner", system), pc_("shared_pc", 0), count_("shared_count", 0),
      auxiliary_("auxiliary_count", 0),
      bankPc_{{"bank_pc_0", 0}, {"bank_pc_1", 0}},
      bankCount_{{"bank_count_0", 0}, {"bank_count_1", 0}},
      completionCycles_(completionCycles), enabled_(enabled) {}

void StateOwner::Build()
{
    SetObjectCapacity(2); // only the two access modules; no state objects
    pc_.Build();
    count_.Build();
    auxiliary_.Build();
    for (auto& bank : bankPc_) bank.Build();
    for (auto& bank : bankCount_) bank.Build();
    access_[0] = std::make_shared<StateAccess>("pc_writer", &pc_, &count_, StateAccess::Field::Pc, enabled_);
    access_[1] = std::make_shared<StateAccess>("count_writer", &pc_, &count_, StateAccess::Field::Count, enabled_);
    for (const auto& access : access_) AddSubmodule(access);
    BuildInnerQueues();
    BuildChildren();
}

void StateOwner::UpdateState()
{
    const std::uint64_t next = NextCount(auxiliary_.Read());
    auxiliary_.Write(next);
    for (std::size_t i = 0; i < 2; ++i) {
        bankPc_[i].Write(bankPc_[i].Read() + i + 1);
        // Distinct array elements have independent pending and current storage.
        if (next % 2 == 0) bankCount_[i].Write(bankCount_[i].Read() + i + 3);
    }
    Log().Debug(Name(), "work") << "work_cycles=" << next;
    if (completionCycles_ != 0 && next == completionCycles_)
        GetSim()->RequestTerminate("state sample complete");
}

void StateOwner::Work()
{
    const std::uint64_t oldPc = pc_.Read(), oldCount = count_.Read();
    UpdateState();
    WorkChildren();
    GFSIM_ASSERT(pc_.Read() == oldPc && count_.Read() == oldCount)
        << "worker writes must remain pending until owner Xfer";
}

void StateOwner::Xfer()
{
    pc_.Xfer();
    count_.Xfer();
    auxiliary_.Xfer();
    for (auto& bank : bankPc_) bank.Xfer();
    for (auto& bank : bankCount_) bank.Xfer();
    AdvanceInnerQueues();
    XferChildren();
}

void StateOwner::Reset()
{
    pc_.Reset();
    count_.Reset();
    auxiliary_.Reset();
    for (auto& bank : bankPc_) bank.Reset();
    for (auto& bank : bankCount_) bank.Reset();
    ResetInnerQueues();
    ResetChildren();
}

void StateOwner::ReportStat()
{
    Report().ReportVal("Owner work cycles (cycles since reset)", auxiliary_.Read());
    pc_.ReportStat();
    count_.ReportStat();
    auxiliary_.ReportStat();
    for (auto& bank : bankPc_) bank.ReportStat();
    for (auto& bank : bankCount_) bank.ReportStat();
    ReportChildren();
}

const gfsim::SimState<std::uint64_t>& StateOwner::BankPc(std::size_t i) const
{
    GFSIM_ASSERT(i < 2) << "bank index";
    return bankPc_[i];
}

const gfsim::SimState<std::uint32_t>& StateOwner::BankCount(std::size_t i) const
{
    GFSIM_ASSERT(i < 2) << "bank index";
    return bankCount_[i];
}

const StateAccess& StateOwner::Access(std::size_t i) const
{
    GFSIM_ASSERT(i < 2 && access_[i]) << "access index or owner not built";
    return *access_[i];
}

} // namespace state_test
