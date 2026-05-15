# Notepatra palette preview — synthetic; no real data
# Multi-stage build demo

ARG RUST_VERSION=1.78
ARG DEBIAN_VERSION=bookworm-slim

# ---- builder stage ----
FROM rust:${RUST_VERSION}-bookworm AS builder

LABEL maintainer="Alice <alice@example.com>"
LABEL org.opencontainers.image.source="https://example.com/notepatra"

ENV CARGO_TERM_COLOR=always \
    RUST_BACKTRACE=1

WORKDIR /app

# Cache dependency build
COPY Cargo.toml Cargo.lock ./
RUN mkdir -p src && echo "fn main() {}" > src/main.rs && \
    cargo build --release && \
    rm -rf src target/release/deps/app*

COPY . .

RUN cargo build --release && \
    strip target/release/app

# ---- runtime stage ----
FROM debian:${DEBIAN_VERSION} AS runtime

RUN apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates && \
    rm -rf /var/lib/apt/lists/*

RUN groupadd --system app && useradd --system --gid app --uid 1000 app

WORKDIR /opt/app

COPY --from=builder /app/target/release/app /usr/local/bin/app
COPY --chown=app:app config/ /opt/app/config/

ENV APP_ENV=production \
    PORT=8080

EXPOSE 8080/tcp

USER app

HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8080/healthz || exit 1

VOLUME ["/opt/app/data"]

ENTRYPOINT ["/usr/local/bin/app"]
CMD ["--config", "/opt/app/config/app.toml"]
