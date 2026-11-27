#pragma once

namespace mesh_example {

struct Packet {
    int id;
    int source;
    int destination;
    int hops;
};

// Direction order defines round-robin priority after reset.
enum class Direction { None, Local, West, East, North, South };

} // namespace mesh_example
