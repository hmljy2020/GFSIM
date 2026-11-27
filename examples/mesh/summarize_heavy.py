#!/usr/bin/env python3
"""Summarize per-run CSV as median batch means, retaining the observed range."""
import argparse
import csv
import statistics
from collections import defaultdict


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv")
    args = parser.parse_args()
    groups = defaultdict(lambda: defaultdict(list))
    checksums = defaultdict(set)
    with open(args.csv, newline="") as stream:
        for row in csv.DictReader(stream):
            iterations, workers = int(row["iterations"]), int(row["workers"])
            if tuple(int(row[k]) for k in ("cycles", "packets", "hops", "total_latency")) != (132, 1560, 6760, 19246):
                raise ValueError("network baseline mismatch")
            if int(row["fake_rounds"]) != iterations * 40 * 132:
                raise ValueError("fake ALU round count mismatch")
            checksums[iterations].add(row["checksum"])
            groups[iterations, workers][int(row["batch"])].append(float(row["wall_ms"]))
    if not groups:
        raise ValueError("empty benchmark CSV")
    if any(len(values) != 1 for values in checksums.values()):
        raise ValueError("checksum differs between runs/worker counts")
    summaries = {}
    for key, batches in groups.items():
        means = [statistics.mean(values) for values in batches.values()]
        summaries[key] = statistics.median(means), min(means), max(means)
    print("iterations,workers,median_ms,min_batch_ms,max_batch_ms,speedup,efficiency")
    for (iterations, workers), (median, low, high) in sorted(summaries.items()):
        speedup = summaries[iterations, 0][0] / median
        efficiency = speedup / workers if workers else 1.0
        print(f"{iterations},{workers},{median:.3f},{low:.3f},{high:.3f},{speedup:.3f},{efficiency:.3f}")


if __name__ == "__main__":
    main()
