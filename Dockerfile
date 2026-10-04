# syntax=docker/dockerfile:1
FROM alpine:3.23 AS builder

RUN apk add --no-cache build-base cmake ninja git ca-certificates \
    jsoncpp-dev openssl-dev c-ares-dev zlib-dev util-linux-dev libpq-dev

# Build only the framework libraries; this backend uses libpq directly.
RUN git clone --branch v1.9.13 --depth 1 --recurse-submodules --shallow-submodules \
        https://github.com/drogonframework/drogon.git /tmp/drogon \
    && cmake -S /tmp/drogon -B /tmp/drogon-build -G Ninja \
        -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_CXX_STANDARD=20 \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_CTL=OFF -DBUILD_EXAMPLES=OFF \
        -DBUILD_TESTING=OFF -DBUILD_ORM=OFF -DBUILD_BROTLI=OFF \
        -DBUILD_YAML_CONFIG=OFF \
        -DCMAKE_CXX_FLAGS="-ffunction-sections -fdata-sections" \
    && cmake --build /tmp/drogon-build --parallel 2 \
    && cmake --install /tmp/drogon-build

WORKDIR /src
COPY CMakeLists.txt ./
COPY src/ ./src/
RUN cmake -S . -B /tmp/backend-build -G Ninja \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_CXX_FLAGS="-ffunction-sections -fdata-sections" \
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections" \
    && cmake --build /tmp/backend-build --parallel 2 \
    && strip /tmp/backend-build/cpp_backend

FROM alpine:3.23 AS runtime-rootfs
RUN apk upgrade --no-cache \
    && apk add --no-cache ca-certificates libstdc++ libpq jsoncpp libuuid c-ares zlib \
    && addgroup -S -g 10001 backend \
    && adduser -S -D -H -u 10001 -G backend backend

# Flatten the updated Alpine filesystem so replaced packages don't add layers.
FROM scratch AS runtime
COPY --from=runtime-rootfs / /
WORKDIR /app
COPY --from=builder /tmp/backend-build/cpp_backend ./cpp_backend
COPY public/ ./public/
USER 10001:10001
ENV LISTEN_HOST=0.0.0.0 UPLOAD_PATH=/tmp/uploads
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s --retries=3 \
    CMD wget -q -T 2 -O /dev/null http://127.0.0.1:8080/health || exit 1
ENTRYPOINT ["/app/cpp_backend"]
