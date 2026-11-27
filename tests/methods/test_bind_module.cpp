#include <sstream>
#include <type_traits>
#include "ReferenceNode.h"
#include "SimAssert.h"
#include "SimExecutor.h"

using namespace gfsim;
using method_test::ReferenceNode;

static_assert(std::is_same<decltype(std::declval<ReferenceNode&>().BindModule(
                  std::declval<ReferenceNode*>())), ReferenceNode*>::value, "retain concrete type");
static_assert(std::is_same<decltype(std::declval<ReferenceNode&>().BindModule(
                  std::declval<const ReferenceNode*>())), const ReferenceNode*>::value, "retain const");

template <typename Exception, typename Action>
void Reject(Action action)
{
    bool rejected = false;
    try { action(); }
    catch (const Exception&) { rejected = true; }
    GFSIM_ASSERT(rejected) << "expected binding error";
}

void ArgumentsAndOwnership()
{
    SimSystem sim;
    auto target = std::make_shared<ReferenceNode>("target", &sim);
    const long owners = target.use_count();
    auto caller = std::make_shared<ReferenceNode>("caller", &sim, target.get());
    GFSIM_ASSERT(target.use_count() == owners) << "reference must not own target";
    GFSIM_ASSERT(caller->OptionalTarget() == target.get());
    GFSIM_ASSERT(target->OptionalTarget() == nullptr) << "optional null bypasses binding";
    Reject<std::invalid_argument>([&] { caller->BindModule(static_cast<ReferenceNode*>(nullptr)); });
    Reject<std::invalid_argument>([&] { caller->BindModule(target.get()); });
    Reject<std::invalid_argument>([&] { caller->BindModule(static_cast<const ReferenceNode*>(target.get())); });
    GFSIM_ASSERT(target->BindModule(target.get()) == target.get());
    target->BindModule(caller.get()); // mutual references do not imply ownership cycles
    sim.AddModule(caller); // target root is deliberately later
    sim.AddModule(target);
    sim.Build();
    GFSIM_ASSERT(caller->Objects().empty() && caller->Submodules().empty());
    GFSIM_ASSERT(target->Objects().empty() && target->Submodules().empty());
    GFSIM_ASSERT(caller->Parent() == nullptr && target->Parent() == nullptr);
    Reject<std::logic_error>([&] { caller->BindModule(caller.get()); });
    sim.Reset();
    Reject<std::logic_error>([&] { caller->BindModule(caller.get()); });
}

void InvalidTargets()
{
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        SimSystem sim, other;
        // An unregistered object claiming the right context still is not a member.
        auto target = std::make_shared<ReferenceNode>("outside", scenario == 0 ? &sim : &other);
        if (scenario != 0) {
            other.AddModule(target);
            if (scenario == 2) other.Build();
        }
        auto caller = std::make_shared<ReferenceNode>("caller", &sim, target.get());
        sim.AddModule(caller);
        Reject<std::logic_error>([&] { sim.Build(); });
        GFSIM_ASSERT(!sim.IsBuilt());
        Reject<std::logic_error>([&] { sim.Step(); });
        std::ostringstream dot;
        Reject<std::logic_error>([&] { sim.WriteDot(dot); });
    }
}

std::string Topology(bool references, const std::string& path)
{
    SimSystem sim;
    auto leaf = std::make_shared<ReferenceNode>("leaf");
    auto branch = std::make_shared<ReferenceNode>("branch", nullptr, nullptr,
                         std::vector<std::shared_ptr<ReferenceNode>>{leaf});
    auto root = std::make_shared<ReferenceNode>("root", &sim, nullptr,
                         std::vector<std::shared_ptr<ReferenceNode>>{branch}, true);
    auto other = std::make_shared<ReferenceNode>("other", &sim);
    if (references) {
        root->BindModule(other.get());
        root->BindModule(root.get()); // order differs from target node IDs
        branch->BindModule(static_cast<const ReferenceNode*>(other.get()));
        leaf->BindModule(root.get());
        other->BindModule(leaf.get()); // root -> other -> leaf -> root cycle
    }
    sim.AddModule(root);
    sim.AddModule(other);
    sim.Build();
    GFSIM_ASSERT(root->Objects().size() == 2 && root->Submodules().size() == 1);
    GFSIM_ASSERT(branch->Objects().size() == 1 && leaf->Objects().empty());
    GFSIM_ASSERT(branch->Parent() == root.get() && leaf->Parent() == branch.get());
    GFSIM_ASSERT(other->Parent() == nullptr);
    SimExecutionPlan plan(root.get(), 4);
    unsigned tasks = 0;
    for (const auto& worker : plan.workers) {
        for (const auto& task : worker) {
            ++tasks;
            GFSIM_ASSERT(task.root == branch.get());
        }
    }
    GFSIM_ASSERT(tasks == 1) << "references must not become worker tasks";
    sim.Step(); // reference cycles must not recurse through lifecycle traversal
    std::ostringstream dot, again;
    sim.WriteDot(dot);
    sim.WriteDot(again);
    GFSIM_ASSERT(dot.str() == again.str());
    if (!path.empty()) sim.SaveDot(path);
    return dot.str();
}

void Graph(const std::string& path)
{
    const std::string plain = Topology(false, "");
    const std::string graph = Topology(true, path);
    GFSIM_ASSERT(plain.find("method") == std::string::npos);
    const std::string edges[] = {
        "  m0 -> m3 [style=dashed, label=\"method\"];\n",
        "  m0 -> m0 [style=dashed, label=\"method\"];\n",
        "  m1 -> m3 [style=dashed, label=\"method\"];\n",
        "  m2 -> m0 [style=dashed, label=\"method\"];\n",
        "  m3 -> m2 [style=dashed, label=\"method\"];\n"
    };
    std::size_t position = 0;
    std::string stripped = graph;
    for (const auto& edge : edges) {
        const auto found = graph.find(edge, position);
        GFSIM_ASSERT(found != std::string::npos) << "edge direction, style, and binding order";
        position = found + edge.size();
        stripped.erase(stripped.find(edge), edge.size());
    }
    GFSIM_ASSERT(stripped == plain) << "only method edges may change the graph";
    GFSIM_ASSERT(graph.find("q0 -> m0 [label=\"input\"]") != std::string::npos);
    GFSIM_ASSERT(graph.find("m0 -> q0 [label=\"output\"]") != std::string::npos);
}

int main(int argc, char** argv)
{
    GFSIM_ASSERT(argc == 2) << "expected DOT output path";
    ArgumentsAndOwnership();
    InvalidTargets();
    Graph(argv[1]);
}
