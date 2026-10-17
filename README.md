# LogLens

LogLens is a small log search tool I wrote in C++. You send it log lines over HTTP, and you can search them with queries like `payment AND timeout`. It runs in Docker with one command.

## Why I built it

I was testing a RISC core few months ago, and one of the modules I was verifying kept filling the log file with errors. A simulated core executes instructions very quickly, so a single run produces a lot of log lines in a short time, and the same few faults repeat many times. When I opened the log I couldn't tell which error to fix first. The questions I actually had were: which errors are there, which one happens most often, and which ones show up together in the same line?

`grep` can answer those, but every search rescans the whole file, and combining conditions means chaining several `grep` commands together. I also looked at open source log search tools. They solve the problem properly, but they are built for running a service, and they needed more setup and more memory and disk than I wanted to spend on a side project on my own machine. I wanted something I could run next to my work, that stays small, and where I know exactly where the memory goes.

So I built LogLens around one idea: pay a one-time cost to build an inverted index (word to the list of lines containing it), and then answer queries by looking up and merging those lists instead of rereading the file. A query like `error AND NOT timeout` becomes a merge of sorted line-id lists, and the number of matches comes back with every search (`total` in the API), so comparing how often different errors occur is one query each.

The measurements are in the Benchmarks section, and they show what this approach costs and where it pays off:

- **It is not faster than grep on small files.** At 250,000 lines grep answered a single-word search in 9 ms, faster than LogLens over HTTP.
- **It does win as the file grows.** At 1,000,000 lines grep took about 52 ms per search. The LogLens query took about 0.015 ms, after about 2 seconds to build the index.
- **The cost is memory.** The index and all the log lines are held in RAM, about 200 to 250 bytes per line in command-line mode (59.8 MiB for 250,000 lines, 198 MiB for 1,000,000). So LogLens is lighter than a full log platform to run, but it uses more memory than grep, which keeps almost nothing in RAM.

LogLens also doesn't yet rank errors by frequency by itself. At the moment you query each error and compare the match counts. Listing the most common messages directly is the first thing I'd add next.

I wrote it in C++ because I wanted to improve my C++, and because the project depends on things C++ gives you direct control over: how the index is laid out in memory, how lists are merged, and how much RAM each line costs. That is how I found, and could measure, that the server uses more memory per line than the command-line mode for the same data.

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

- I rebuild the index from a plain log file at startup instead of designing a binary index format. It's simple, but startup gets slower as the file grows.
- I used cpp-httplib instead of writing sockets myself, because the search engine was the part I wanted to spend time on.
- Logs come in over HTTP POST, not by reading Docker's log files. It's easier to test with `curl`, but the app has to send its own logs.
- The parser evaluates while parsing instead of building a tree. It's less code, but it would need to change if I ever wanted to optimise queries.
- I keep every line and the whole index in RAM. It makes queries fast and the code simple, and it is the main limit (see Benchmarks).

## Alternatives I considered

- **grep / ripgrep:** no setup, and my benchmarks show they are as fast or faster than LogLens up to a few hundred thousand lines. But each search rescans the file.
- **ELK (Elasticsearch, Logstash, Kibana):** full-featured, but heavier to run than I wanted for a side project. I didn't benchmark it.
- **Grafana Loki:** lighter than ELK because it indexes labels rather than the full text of each line. I didn't try it, so I can't say how it compares on searching arbitrary text.

## Benchmarks

I ran everything on a laptop with an AMD Ryzen 7 7435HS (16 threads), under WSL2 with 11 GiB of RAM available to it, using Docker 29.8.2 and a Release build. The data is fake logs from `scripts/gen_logs.py` (same seed every time, so anyone can regenerate the same file). Memory numbers come from `docker stats`, `VmRSS` inside the container, and `/usr/bin/time -v`. Search times are medians, with a warm cache.

### Search speed at 250,000 lines

I used the first 250,000 lines of the generated log (`mid.log`) and loaded them into the 128 MiB container.

| Query | grep | ripgrep | LogLens over HTTP (`limit=100`) |
|---|---|---|---|
| `timeout` | 9 ms | 16 ms | 21 ms |
| `payment AND timeout` | 32 ms | 23 ms | 18 ms |

Each is the median of 5 runs. grep and ripgrep have no AND operator, so for the second row I piped two commands together. Both queries match 7,510 lines.

An empty request to `/health` takes 19 ms with `curl`, so almost all of the LogLens HTTP time is `curl` starting up and the network round trip, not the search. The differences of a few milliseconds here are within the noise.

To see the search by itself, I ran the command-line mode, which times the index build and the query separately (10 runs):

- Building the index for 250,000 lines took about 490 ms (443 to 531 ms).
- The query itself took about 0.005 ms for `timeout` and about 0.09 ms for `payment AND timeout`.

At this size grep wins on a single word (9 ms vs 21 ms), and the AND query is close. The index's advantage is too small to show here, and the 490 ms build cost and the HTTP overhead hide it.

### Search speed at 1,000,000 lines

The container can't hold this much (see below), so I ran the command-line mode on the full `big.log`. Medians of 3 runs, `timeout` query.

| | 250,000 lines | 1,000,000 lines |
|---|---|---|
| grep | 9 ms | 52 ms |
| LogLens index build | ~490 ms | ~1,970 ms |
| LogLens query (lookup and merge only) | 0.005 ms | 0.015 ms |
| Matches | 7,510 | 30,198 |
| Peak memory (CLI) | 59.8 MiB | 198 MiB |

grep and LogLens both found 30,198 matching lines. grep matches substrings and LogLens matches whole words, so counts could differ on real logs, but not on this generated data.

grep gets slower as the file grows because it reads every line each time. The LogLens query time depends on how many lines match, not on the file size. The price is the index build: about 2 seconds for 1M lines, which grep makes up for after roughly 40 queries on the same data. Through the HTTP server each request also costs around 20 ms of overhead, so I'd say the break-even is somewhere between 40 and 60 queries. That is an estimate from these numbers, not something I measured directly.

The 0.015 ms only covers finding the matching line ids. It doesn't include printing 30,000 lines.

### Ingest speed

Sending the 250,000 lines to the container over HTTP in 5 requests of 50,000 lines took 0.82 seconds, about 300,000 lines per second. This was a single run.

### Memory

**How much fits in 128 MiB.** I started a fresh container with a 128 MiB limit and sent 1,000,000 generated lines in requests of 50,000. After each request I recorded the line count and memory from `docker stats`.

| Lines indexed | Memory |
|---|---|
| 50,000 | 15.5 MiB |
| 100,000 | 34.0 MiB |
| 150,000 | 53.9 MiB |
| 200,000 | 70.4 MiB |
| 250,000 | 91.6 MiB |
| 300,000 | 112.7 MiB |
| 350,000 | 127.8 MiB |
| 400,000 to 800,000 | stayed between 123 and 128 MiB |

Memory grew in a straight line up to about 350,000 lines, where it hit the limit. After that it stayed pinned near 128 MiB while the server kept accepting data, and the server stopped responding between 800,000 and 850,000 lines. I don't know why it kept going past the limit for so long, and I didn't capture the crash details, so I can't say what finally stopped it.

**The server uses more memory than the CLI for the same data.** At 250,000 lines:

| | Memory |
|---|---|
| CLI mode, peak (`/usr/bin/time -v`) | 59.8 MiB (about 250 bytes per line) |
| Server in the container, `VmRSS` | 98.2 MiB (about 410 bytes per line) |
| Server, `docker stats` | 98.6 MiB |

`VmRSS` and `docker stats` agree, so the extra memory belongs to the server process and isn't just cache. The same data costs about 64% more through the server than through the CLI. I haven't found out why. My guesses are that each 50,000-line request body is held in memory and copied while it's parsed, and that glibc's per-thread memory pools keep freed memory around. I haven't tested either.

Two runs at 250,000 lines in the container gave 91.6 and 98.6 MiB, so expect around 7 MiB of variation between runs. At about 410 bytes per line, a 128 MiB container holds around 325,000 lines, which matches where the first test hit the limit.

### What I take from this

- For a few hundred thousand lines, grep is as fast or faster than LogLens, so there is no reason to use LogLens at that size.
- At a million lines the index starts to win: grep takes 52 ms per search and LogLens takes microseconds after a 2 second build. But that needs about 200 MiB of RAM, more than the container allows.
- Keeping every line and the whole index in RAM is the main limit. That is the first thing I would change (see "What I'd do next").

## What's missing

- Everything is held in RAM. A 128 MiB container holds about 325,000 lines (about 410 bytes per line through the server), and the server stops responding somewhere above 800,000. The command-line mode needs about 200 MiB for 1,000,000 lines.
- Startup gets slower as `lines.log` grows, and I haven't tested what happens on restart if `lines.log` is bigger than the memory limit.
- There's no login on any endpoint, so only run it on a network you trust.
- It doesn't count or rank the most frequent errors by itself. You query each one and compare `total`.
- No time filters, phrase search, ranking or log expiry.
- The tokenizer only handles plain letters and digits.

## What I'd do next

1. List the most frequent error messages directly. That was the question that started this project.
2. Store the index on disk in a compressed form (delta and varint encoding) and use `mmap`, so memory isn't tied to data size.
3. Find out why the server uses more memory per line than the CLI.
4. Add time filters like `--since 1h`.
5. Index with multiple threads and measure the difference.
6. Add auth and log retention.

## Third-party code

`third_party/httplib.h` is [cpp-httplib](https://github.com/yhirose/cpp-httplib), which uses the MIT license.

## How I used AI

I used Claude to plan the project, write the first version of the code, set up Docker, and design the benchmarks. The code was proofread in case any hallucination by the LLM. 

Two places where I checked its work instead of trusting it. In my first container test, a search for `bench` returned `"total":0` even though memory had jumped to 108 MiB, so I threw that run away and redid the test from a clean container instead of using those numbers. Later, the AI guessed that the container's extra memory was probably file cache. I measured `VmRSS` inside the container, it matched `docker stats`, and that showed the guess was wrong, so the README says I don't know the cause yet.



following are the snippets of the benchmark statistics.

<img width="865" height="553" alt="image" src="https://github.com/user-attachments/assets/c176602f-8a10-40f5-82f0-8a64c3d73d79" />

<img width="940" height="642" alt="image" src="https://github.com/user-attachments/assets/0464f6a2-76a7-4fe9-9c86-3ef2fec86638" />

<img width="940" height="235" alt="image" src="https://github.com/user-attachments/assets/028b94d0-f9e1-4543-b1c0-6f1d9c1f21f7" />

<img width="940" height="119" alt="image" src="https://github.com/user-attachments/assets/20d9df3d-7b45-4bd2-95a9-97aa68b73ad5" />

<img width="940" height="140" alt="image" src="https://github.com/user-attachments/assets/f8c6b5bf-f63b-49dc-a7af-04b2bc8c10ca" />

<img width="940" height="155" alt="image" src="https://github.com/user-attachments/assets/2d4645da-e35e-4f4c-8b1b-f93b66c64c5d" />





