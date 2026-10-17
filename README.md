# LogLens

LogLens is a small log search tool I wrote in C++. You send it log lines over HTTP, and you can search them with queries like `payment AND timeout`. It runs in Docker with one command.

## Why I built it

[Write 3-4 sentences in your own words. Who did you talk to, how do they search logs today, and what annoyed them? Use real details and a real quote if you have one. If you haven't talked to anyone yet, do that before submitting, because this section matters more than the code.]

I picked C++ because [your real reason, e.g. wanting to control memory, or wanting the index to stay small].

## Run it

You need Docker.

```bash
git clone https://github.com/thekayos29/loglens.git
cd loglens
docker compose up --build
```

That starts LogLens and a fake app that sends it logs twice a second. In another terminal:

```bash
curl "localhost:8080/search?q=error&limit=5"
curl "localhost:8080/search?q=payment+AND+timeout"
curl -i "localhost:8080/search?q=(payment"     # bad query, returns a 400
```

`docker compose down` stops everything but keeps the logs. `docker compose down -v` deletes them too.

To run it without Docker:

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build
./build/loglens search some.log "payment AND timeout"
```

`scripts/gen_logs.py` makes a big fake log file if you want one to test with:

```bash
python3 scripts/gen_logs.py 1000000 > big.log
```

## API

- `GET /health` returns `{"status":"ok"}`
- `POST /ingest?container=NAME` takes log lines in the body, one per line
- `GET /search?q=...&limit=N` returns `{"total": N, "lines": [...]}`. `limit` defaults to 100.

## Queries

- `timeout` finds lines with that word (not case sensitive)
- `payment AND timeout` needs both
- `payment timeout` does the same thing, because two words next to each other mean AND
- `timeout OR refused` needs either
- `error AND NOT timeout` finds errors without "timeout"
- `payment AND (timeout OR refused)` uses brackets to group

AND, OR and NOT have to be in capitals. A lowercase `and` is treated as a normal search word. NOT binds tightest, then AND, then OR.

## How it works

Searching a big file line by line, like grep does, is slow. So when a line comes in, I split it into words and store which lines each word appears in. That's an inverted index, like the index at the back of a book. Looking up `timeout` then means reading one list instead of scanning everything.

For `AND`, `OR` and `NOT` I merge two sorted lists with two pointers. The query parser is a small recursive descent parser that works out the answer as it reads the query.

Every ingested line is also appended to `lines.log` on a Docker volume. On startup, LogLens reads that file again and rebuilds the index.

The code is in `src/`: `tokenizer`, `index`, `query`, and `main.cpp` for the CLI and HTTP server. The server uses a `shared_mutex`, so many searches can run at once but only one ingest at a time.

## Choices I made

[Rewrite these in your own words. You will be asked about each one, so only keep the ones you can actually explain.]

- I rebuild the index from a plain log file at startup instead of designing a binary index format. It's simple, but startup gets slower as the file grows.
- I used cpp-httplib instead of writing sockets myself, because the search engine was the part I wanted to spend time on.
- Logs come in over HTTP POST, not by reading Docker's log files. It's easier to test with `curl`, but the app has to send its own logs.
- The parser evaluates while parsing instead of building a tree. It's less code, but it would need to change if I ever wanted to optimise queries.

## Benchmarks

[Run these yourself and fill this in. Don't copy numbers from anywhere else.]

I tested on [your machine, e.g. WSL2 on a laptop with X cores and Y GB RAM], using a Release build and `gen_logs.py` with [N] lines. I ran each test [N] times and used the median. The cache was [cold/warm].

| | grep | LogLens |
|---|---|---|
| one word | [ ] | [ ] |
| `payment AND timeout` | [ ] | [ ] |
| index build time | n/a | [ ] |
| memory used | [ ] | [ ] |

LogLens has to build its index first, so it only wins if you search the same data more than once. [Add what your results show, including if grep was faster for some cases.]

## What's missing

- The whole index and all the log lines sit in RAM. A lot of logs will go over the 128 MB limit in `docker-compose.yml`.
- Startup gets slower as `lines.log` grows.
- There's no login on any endpoint, so only run it on a network you trust.
- No time filters, phrase search, ranking or log expiry.
- The tokenizer only handles plain letters and digits.

## What I'd do next

1. Store the index on disk in a compressed form (delta and varint encoding) and use `mmap`.
2. Add time filters like `--since 1h`.
3. Index with multiple threads and measure the difference.
4. Add auth and log retention.

## Third-party code

`third_party/httplib.h` is [cpp-httplib](https://github.com/yhirose/cpp-httplib), which uses the MIT license.

## How I used AI

[Write this honestly in 3-5 sentences. What did you ask it to do? What did it get wrong or what did you change? Name one specific thing, like a bug you caught or a suggestion you turned down. Link to your chat logs in `transcripts/`.]
