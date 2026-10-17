// LogLens: tiny log search engine.
//   loglens search <file-or-dir> "<query>"     one-off search
//   loglens serve [--data DIR] [--port N]      HTTP server (/ingest, /search, /health)
//   loglens healthcheck [port]                 exit 0 if the server answers
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>

#include "httplib.h"
#include "index.h"
#include "query.h"

namespace fs = std::filesystem;

static void indexStream(Index& idx, std::istream& in) {
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) idx.addLine(line);
    }
}

static void indexPath(Index& idx, const fs::path& p) {
    if (fs::is_directory(p)) {
        for (const auto& e : fs::recursive_directory_iterator(p)) {
            if (!e.is_regular_file()) continue;
            std::ifstream f(e.path());
            indexStream(idx, f);
        }
    } else {
        std::ifstream f(p);
        if (!f) throw std::runtime_error("cannot open " + p.string());
        indexStream(idx, f);
    }
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

static int cmdSearch(const std::string& path, const std::string& query) {
    using Clock = std::chrono::steady_clock;
    Index idx;
    auto t0 = Clock::now();
    indexPath(idx, path);
    auto t1 = Clock::now();
    Postings hits = runQuery(idx, query);
    auto t2 = Clock::now();

    for (int id : hits) std::cout << (id + 1) << ": " << idx.line(id) << "\n";
    auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::cerr << hits.size() << " matches | indexed " << idx.size() << " lines in "
              << ms(t0, t1) << " ms | query took " << ms(t1, t2) << " ms\n";
    return 0;
}

static int cmdServe(const std::string& dataDir, int port) {
    fs::create_directories(dataDir);
    const fs::path logPath = fs::path(dataDir) / "lines.log";

    Index idx;
    {
        std::ifstream f(logPath);
        if (f) indexStream(idx, f);  // rebuild index from the persisted log
    }
    std::cout << "Loaded " << idx.size() << " lines from " << logPath << std::endl;

    std::ofstream out(logPath, std::ios::app);
    if (!out) {
        std::cerr << "cannot open " << logPath << " for writing\n";
        return 1;
    }

    std::shared_mutex mtx;  // many searches at once, one ingest at a time
    httplib::Server svr;

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    svr.Post("/ingest", [&](const httplib::Request& req, httplib::Response& res) {
        std::string container = req.get_param_value("container");
        std::istringstream in(req.body);
        std::string line;
        int added = 0;
        std::unique_lock<std::shared_mutex> lock(mtx);
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (!container.empty()) line = "[" + container + "] " + line;
            out << line << '\n';
            idx.addLine(line);
            ++added;
        }
        out.flush();
        res.set_content("{\"ingested\":" + std::to_string(added) + "}", "application/json");
    });

    svr.Get("/search", [&](const httplib::Request& req, httplib::Response& res) {
        std::string q = req.get_param_value("q");
        size_t limit = 100;
        if (req.has_param("limit")) {
            try { limit = static_cast<size_t>(std::stoul(req.get_param_value("limit"))); }
            catch (...) { /* keep default */ }
        }
        try {
            std::shared_lock<std::shared_mutex> lock(mtx);
            Postings hits = runQuery(idx, q);
            std::string json = "{\"total\":" + std::to_string(hits.size()) + ",\"lines\":[";
            for (size_t i = 0; i < hits.size() && i < limit; ++i) {
                if (i) json += ",";
                json += "\"" + jsonEscape(idx.line(hits[i])) + "\"";
            }
            json += "]}";
            res.set_content(json, "application/json");
        } catch (const QueryError& e) {
            res.status = 400;
            res.set_content("{\"error\":\"" + jsonEscape(e.what()) + "\"}", "application/json");
        }
    });

    std::cout << "Listening on 0.0.0.0:" << port << std::endl;
    // 0.0.0.0 (not 127.0.0.1) so the server is reachable from outside a container
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "failed to listen on port " << port << "\n";
        return 1;
    }
    return 0;
}

static int cmdHealthcheck(int port) {
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(2);
    auto r = cli.Get("/health");
    return (r && r->status == 200) ? 0 : 1;
}

static int usage() {
    std::cerr << "usage:\n"
                 "  loglens search <file-or-dir> \"<query>\"\n"
                 "  loglens serve [--data DIR] [--port N]\n"
                 "  loglens healthcheck [port]\n";
    return 2;
}

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::string cmd = argv[1];
    try {
        if (cmd == "search" && argc == 4) return cmdSearch(argv[2], argv[3]);
        if (cmd == "serve") {
            std::string data = "./data";
            int port = 8080;
            for (int i = 2; i + 1 < argc; i += 2) {
                std::string flag = argv[i];
                if (flag == "--data") data = argv[i + 1];
                else if (flag == "--port") port = std::stoi(argv[i + 1]);
                else return usage();
            }
            return cmdServe(data, port);
        }
        if (cmd == "healthcheck") return cmdHealthcheck(argc > 2 ? std::stoi(argv[2]) : 8080);
    } catch (const QueryError& e) {
        std::cerr << "query error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return usage();
}