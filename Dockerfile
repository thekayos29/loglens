# ---- Stage 1: build with the full toolchain ----
FROM gcc:13 AS build
WORKDIR /src
RUN apt-get update && apt-get install -y --no-install-recommends cmake \
    && rm -rf /var/lib/apt/lists/*
COPY CMakeLists.txt ./
COPY src ./src
COPY tests ./tests
COPY third_party ./third_party
# static libstdc++/libgcc avoids "GLIBCXX not found" in the slim runtime image
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
 && cmake --build build -j --target loglens

# ---- Stage 2: tiny runtime image, only the binary ----
FROM debian:bookworm-slim
RUN useradd --no-create-home --shell /usr/sbin/nologin app \
    && mkdir /data && chown app /data
COPY --from=build /src/build/loglens /usr/local/bin/loglens
USER app
EXPOSE 8080
ENTRYPOINT ["loglens", "serve", "--data", "/data", "--port", "8080"]