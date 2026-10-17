"""Generate a big reproducible log file for benchmarks.
Usage: python3 scripts/gen_logs.py 5000000 > big.log"""
import random
import sys
from datetime import datetime, timedelta

n = int(sys.argv[1]) if len(sys.argv) > 1 else 1_000_000
random.seed(42)  # same file every run
t = datetime(2026, 10, 1)
msgs = [("INFO", "user={u} logged in"), ("INFO", "order {o} created"),
        ("WARN", "slow query took {ms}ms"), ("ERROR", "payment failed: timeout for order {o}"),
        ("ERROR", "payment gateway refused order {o}"), ("INFO", "cache hit for key {o}")]
weights = [30, 30, 10, 3, 2, 25]
out = sys.stdout
for _ in range(n):
    t += timedelta(milliseconds=random.randint(1, 50))
    lvl, m = random.choices(msgs, weights)[0]
    out.write(f"{t:%Y-%m-%d %H:%M:%S} {lvl:<5} "
              f"{m.format(u=random.randint(1, 50000), o=random.randint(1000, 999999), ms=random.randint(200, 3000))}\n")