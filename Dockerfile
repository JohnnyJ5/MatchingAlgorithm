# ── Dev stage (toolchain only; source bind-mounted at runtime) ─────────────────
FROM debian:bookworm-slim AS dev

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        g++ \
        git \
        make \
        cmake \
        libasio-dev \
        libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# ── Builder stage (CI / production image) ─────────────────────────────────────
FROM dev AS builder

COPY . .

RUN cmake -S . -B build && cmake --build build --parallel

# ── Runtime stage ─────────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
        libpq5 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/build/server ./build/server
COPY --from=builder /app/server/static ./server/static

EXPOSE 8080

CMD ["./build/server"]
