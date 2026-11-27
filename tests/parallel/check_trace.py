"""Validate serialized records from concurrently emitted module events."""
import json
import sys

with open(sys.argv[1]) as stream:
    events = json.load(stream)["traceEvents"]
durations = [e for e in events if e["ph"] == "X"]
assert len(durations) == 21
assert all(e["args"]["detail"] == 'probe\n"' for e in durations)
lanes = {e["tid"] for e in events if e["ph"] == "M" and "tid" in e}
assert len(lanes) == 7
starts = [e for e in events if e["ph"] == "s"]
ends = [e for e in events if e["ph"] == "f"]
assert len(starts) == len(ends) == 21
assert len({e["id"] for e in starts}) == 21
assert {e["id"] for e in starts} == {e["id"] for e in ends}
with open(sys.argv[2]) as stream:
    events = json.load(stream)["traceEvents"]
snapshots = [e["args"] for e in events if e["name"] == "queue_snapshot"]
assert len(snapshots) == 22
assert {e["cycle"] for e in snapshots} == set(range(11))
assert any(e["staged"] == ["7:1"] and e["delays"] == [1] for e in snapshots)
assert any(e["visible"] == ["7:1"] and e["delays"] == [] for e in snapshots)
assert all(e["capacity"] == 1 for e in snapshots)
