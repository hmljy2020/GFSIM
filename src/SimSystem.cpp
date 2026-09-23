#include "SimSystem.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>

#include "SimModule.h"

namespace gfsim {

void SimSystem::AddModule(std::shared_ptr<SimObj> module)
{
    if (buildAttempted_) {
        throw std::logic_error("cannot add a root object after Build");
    }
    if (!module) {
        throw std::invalid_argument("root object must not be null");
    }
    if (std::find(modules_.begin(), modules_.end(), module) != modules_.end()) {
        throw std::invalid_argument("root object is already registered");
    }
    if (dynamic_cast<TraceableQueue*>(module.get()) != nullptr) {
        throw std::invalid_argument("queues must be owned by a parent module");
    }
    modules_.push_back(module);
}

void SimSystem::RegisterTraceQueue(TraceableQueue& queue, const std::string& path)
{
    if (path.empty()) {
        throw std::invalid_argument("queue trace path must not be empty");
    }
    for (const auto& entry : traceQueues_) {
        if (entry.queue == &queue) {
            return;
        }
    }
    std::string unique = path;
    for (std::size_t suffix = 2;; ++suffix) {
        const auto found = std::find_if(traceQueues_.begin(), traceQueues_.end(),
                                        [&unique](const TraceQueueRegistration& entry) {
                                            return entry.path == unique;
                                        });
        if (found == traceQueues_.end()) {
            break;
        }
        unique = path + "#" + std::to_string(suffix);
    }
    if (trace_) {
        trace_->RegisterQueue(queue, unique);
    }
    traceQueues_.push_back(TraceQueueRegistration{&queue, unique});
}

void SimSystem::EnableTrace(std::ostream& output, std::uint64_t sampleEvery)
{
    if (!built_) {
        throw std::logic_error("call Build before EnableTrace");
    }
    if (trace_) {
        throw std::logic_error("trace is already enabled");
    }
    std::unique_ptr<SimTrace> trace(new SimTrace(output, sampleEvery));
    for (const auto& entry : traceQueues_) {
        trace->RegisterQueue(*entry.queue, entry.path);
    }
    trace->Capture(cycles_);
    trace_ = std::move(trace);
}

void SimSystem::DisableTrace()
{
    if (trace_) {
        trace_->Close();
        trace_.reset();
    }
}

void SimSystem::Build()
{
    if (buildAttempted_) {
        throw std::logic_error("SimSystem build has already been attempted");
    }
    if (modules_.empty()) {
        throw std::logic_error("SimSystem has no root objects to build");
    }
    buildAttempted_ = true;
    for (const auto& module : modules_) {
        module->Build();
    }
    for (const auto& module : modules_) {
        module->FreezeObjects();
    }

    struct QueueUse {
        std::size_t owners = 0;
        std::size_t producers = 0;
        std::size_t consumers = 0;
    };
    std::unordered_map<const SimObj*, QueueUse> queues;
    std::function<void(const SimModule*)> collect;
    collect = [&](const SimModule* parent) {
        for (const auto& queue : parent->innerQueues_) {
            ++queues[queue.get()].owners;
        }
        for (const SimObj* queue : parent->outputs_) {
            ++queues[queue].producers;
        }
        for (const SimObj* queue : parent->inputs_) {
            ++queues[queue].consumers;
        }
        for (const auto& child : parent->submodules_) {
            collect(child.get());
        }
    };
    for (const auto& root : modules_) {
        if (const auto* parent = dynamic_cast<const SimModule*>(root.get())) {
            collect(parent);
        }
    }
    std::function<void(const SimObj*)> collectPhysicalQueues;
    collectPhysicalQueues = [&](const SimObj* object) {
        if (dynamic_cast<const TraceableQueue*>(object) != nullptr) {
            queues[object];
        }
        for (const SimObj* child : object->Objects()) {
            collectPhysicalQueues(child);
        }
    };
    for (const auto& root : modules_) {
        collectPhysicalQueues(root.get());
    }
    for (const auto& entry : queues) {
        const QueueUse& use = entry.second;
        if (use.owners != 1 || use.producers != 1 || use.consumers != 1) {
            std::ostringstream message;
            message << "queue " << entry.first->Name()
                    << " requires one parent owner, one output, one input; got "
                    << use.owners << "/" << use.producers << "/"
                    << use.consumers;
            throw std::logic_error(message.str());
        }
    }
    built_ = true;
    Reset();
}

void SimSystem::Step()
{
    if (!built_) {
        throw std::logic_error("call Build before Step");
    }
    if (terminated_) {
        throw std::logic_error("simulation has terminated");
    }
    if (deadlocked_) {
        throw DeadlockError("simulation is deadlocked; call Reset before stepping");
    }
    ++cycles_;
    logger_.SetCycle(cycles_);
    progressThisStep_ = false;
    stepping_ = true;
    try {
        for (const auto& module : modules_) {
            module->Work();
        }
        for (const auto& module : modules_) {
            module->Xfer();
        }
    } catch (...) {
        stepping_ = false;
        throw;
    }
    stepping_ = false;
    if (trace_) {
        trace_->Capture(cycles_);
    }
    stalledCycles_ = progressThisStep_ ? 0 : stalledCycles_ + 1;
    CheckDeadlock();
}

void SimSystem::Run(std::uint64_t maxSteps)
{
    if (maxSteps == 0) {
        throw std::invalid_argument("maxSteps must be positive");
    }
    for (std::uint64_t step = 0; step < maxSteps && !NeedTerminate(); ++step) {
        Step();
    }
    if (!NeedTerminate()) {
        throw std::runtime_error("simulation reached the step limit before termination");
    }
}

void SimSystem::Reset()
{
    if (!built_) {
        throw std::logic_error("call Build before Reset");
    }
    if (trace_) {
        throw std::logic_error("disable trace before Reset");
    }
    cycles_ = 0;
    stalledCycles_ = 0;
    progressThisStep_ = false;
    stepping_ = false;
    terminated_ = false;
    deadlocked_ = false;
    terminationReason_.clear();
    logger_.SetCycle(0);
    for (const auto& module : modules_) {
        module->Reset();
    }
}

void SimSystem::ReportStat()
{
    if (!built_) {
        throw std::logic_error("call Build before ReportStat");
    }
    for (const auto& module : modules_) {
        module->ReportStat();
    }
}

void SimSystem::RequestTerminate(std::string reason)
{
    if (!built_) {
        throw std::logic_error("call Build before RequestTerminate");
    }
    terminated_ = true;
    terminationReason_ = std::move(reason);
    logger_.Info("system", "terminate") << terminationReason_;
}

void SimSystem::MarkProgress()
{
    if (!stepping_) {
        throw std::logic_error("MarkProgress is valid only during Step");
    }
    progressThisStep_ = true;
}

void SimSystem::SetDeadlockThreshold(std::uint64_t cycles)
{
    deadlockThreshold_ = cycles;
}

void SimSystem::CheckDeadlock()
{
    if (deadlocked_) {
        throw DeadlockError("simulation is deadlocked; call Reset before stepping");
    }
    if (terminated_ || deadlockThreshold_ == 0 || stalledCycles_ < deadlockThreshold_) {
        return;
    }
    std::ostringstream message;
    message << "deadlock at cycle " << cycles_ << ": no progress for "
            << stalledCycles_ << " cycles";
    deadlocked_ = true;
    logger_.Error("system", "deadlock") << message.str();
    throw DeadlockError(message.str());
}

} // namespace gfsim
