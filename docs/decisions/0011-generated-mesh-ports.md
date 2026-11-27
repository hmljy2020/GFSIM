# 0011 Explicit mesh ports and generated routing code

The mesh uses one concrete MeshNode class instantiated 40 times. Like Counter, this class directly derives from SimModule and implements all five lifecycle methods. Its constructor receives an instance ID and explicitly named queue pointers for local traffic and four neighbor directions. Each instance owns its arbitration state. The parent owns every physical queue and passes the same pointer to the producer and consumer.

Missing boundary neighbors use null pointer pairs. The constructor binds actual queues, and Work and capacity sampling guard optional ports. This permits one reusable node type with exact physical queue ownership and connections.

Each generated Work method snapshots the input requests and emits an arbitration block for each output. A scalar per output holds the last granted direction, and another holds the available capacity sampled at the cycle boundary. Each winning branch calls Front, Write, and Pop on its named ports. The generator emits the node implementation once and emits each top-level instance with its queue arguments.

Generated sources accompany the example for inspection and standalone C++11 builds. The generator check compares them with the templates and topology. Regression coverage uses packet timing, reset, registration-order reversal and backpressure from the serial mesh baseline.
