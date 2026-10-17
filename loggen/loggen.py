"""Fake application: sends a few realistic log lines per second to LogLens."""
import os
import random
import time
import urllib.request
from datetime import datetime

URL = os.environ.get("LOGLENS_URL", "http://localhost:8080/ingest?container=app")

INFO = ["user={u} logged in", "user={u} viewed order {o}", "order {o} created",
        "cache hit for key {o}", "user={u} logged out"]
WARN = ["slow query took {ms}ms", "retrying payment for order {o}"]
ERROR = ["payment failed: timeout for order {o}", "payment gateway refused order {o}",
         "database connection reset", "unhandled exception in checkout"]


def make_line():
    r = random.random()
    level, pool = ("INFO", INFO) if r < 0.80 else ("WARN", WARN) if r < 0.93 else ("ERROR", ERROR)
    msg = random.choice(pool).format(u=random.randint(1, 500), o=random.randint(1000, 9999),
                                     ms=random.randint(200, 3000))
    return f"{datetime.now():%Y-%m-%d %H:%M:%S} {level:<5} {msg}"


def send(lines):
    req = urllib.request.Request(URL, data="\n".join(lines).encode(), method="POST",
                                 headers={"Content-Type": "text/plain"})
    urllib.request.urlopen(req, timeout=5).read()


while True:
    batch = [make_line() for _ in range(random.randint(1, 5))]
    try:
        send(batch)
    except Exception as e:  # LogLens may be restarting; keep going
        print("send failed:", e)
    time.sleep(0.5)