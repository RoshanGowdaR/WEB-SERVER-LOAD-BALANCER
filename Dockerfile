# Multi-stage Dockerfile for High-Performance C++ Load Balancer
FROM ubuntu:24.04 AS builder

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt update && apt install -y \
    build-essential \
    cmake \
    git \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source tree
COPY . .

# Build release binary
RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc) --target load_balancer_server

# Runtime Stage
FROM ubuntu:24.04 AS runtime

WORKDIR /app

# Install minimal runtime dependencies (ca-certificates, curl)
RUN apt update && apt install -y curl ca-certificates && rm -rf /var/lib/apt/lists/*

# Copy compiled binary and configuration
COPY --from=builder /app/build/load_balancer_server /app/load_balancer_server
COPY --from=builder /app/config/config.example.json /app/config.json

# Expose HTTP port
EXPOSE 8080

# Health check
HEALTHCHECK --interval=10s --timeout=3s --retries=3 \
    CMD curl -f http://127.0.0.1:8080/health || exit 1

ENTRYPOINT ["/app/load_balancer_server", "-c", "/app/config.json"]
