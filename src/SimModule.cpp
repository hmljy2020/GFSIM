#include "SimModule.h"

namespace gfsim {

void SimModule::WorkChildren()
{
    if (GetSim() && GetSim()->ExecuteChildren(this, true)) return;
    for (const auto& child : submodules_) child->Work();
}

void SimModule::XferChildren()
{
    if (GetSim() && GetSim()->ExecuteChildren(this, false)) return;
    for (const auto& child : submodules_) child->Xfer();
}

} // namespace gfsim
