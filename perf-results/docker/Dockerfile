FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# ============================================
# Install build dependencies + benchmark runtimes
# ============================================
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    zlib1g-dev \
    libmbedtls-dev \
    cmake \
    git \
    ca-certificates \
    libreadline-dev \
    python3 \
    ruby \
    luajit \
    default-jdk-headless \
    bc \
    time \
    && rm -rf /var/lib/apt/lists/*

# ============================================
# Copy Objeck source (only what we need to build obc + obr)
# ============================================
WORKDIR /build/objeck

# Compiler source
COPY core/compiler/ core/compiler/
COPY core/shared/   core/shared/

# VM source
COPY core/vm/ core/vm/

# Builds obr's static HTTP/2 + HTTP/3 libraries
COPY tools/deps/ tools/deps/

# Pre-built standard libraries (.obl files)
COPY core/lib/*.obl core/lib/
COPY core/lib/*.ini core/lib/

# ============================================
# Build compiler (obc)
# ============================================
WORKDIR /build/objeck/core/compiler
RUN cp make/Makefile.amd64 Makefile && \
    make clean && make -j$(nproc) OBJECK_LIB_PATH=///".///"

# ============================================
# HTTP/2 + HTTP/3 libraries, linked statically into obr
# ============================================
WORKDIR /build/objeck
RUN bash tools/deps/build_quic_deps.sh /opt/objeck-deps

# ============================================
# Build VM runtime (obr)
# ============================================
WORKDIR /build/objeck/core/vm
RUN cp make/Makefile.amd64 Makefile && \
    make clean && make -j$(nproc) OBJECK_DEPS=/opt/objeck-deps

# ============================================
# Assemble minimal deploy
# ============================================
RUN mkdir -p /opt/objeck/bin /opt/objeck/lib && \
    cp /build/objeck/core/compiler/obc /opt/objeck/bin/ && \
    cp /build/objeck/core/vm/obr /opt/objeck/bin/ && \
    cp /build/objeck/core/lib/*.obl /opt/objeck/lib/ && \
    cp /build/objeck/core/lib/*.ini /opt/objeck/lib/ && \
    chmod +x /opt/objeck/bin/obc /opt/objeck/bin/obr

ENV PATH="/opt/objeck/bin:${PATH}"
ENV OBJECK_LIB_PATH="/opt/objeck/lib"

# ============================================
# Copy benchmark sources
# ============================================
COPY programs/tests/clbg/ /benchmarks/clbg/
COPY programs/tests/perf/ /benchmarks/perf/
COPY perf-results/cross-lang/benchmarks/ /benchmarks/cross-lang/

# Strip UTF-8 BOM and Windows CRLF from all source files
RUN find /benchmarks -type f \( -name '*.obs' -o -name '*.py' -o -name '*.rb' -o -name '*.lua' -o -name '*.java' -o -name '*.sh' \) \
    -exec sed -i '1s/^\xEF\xBB\xBF//' {} \; \
    -exec sed -i 's/\r$//' {} \;

# Copy the benchmark runner
COPY perf-results/docker/run_all_benchmarks.sh /benchmarks/
RUN sed -i '1s/^\xEF\xBB\xBF//' /benchmarks/run_all_benchmarks.sh && \
    sed -i 's/\r$//' /benchmarks/run_all_benchmarks.sh && \
    chmod +x /benchmarks/run_all_benchmarks.sh

WORKDIR /benchmarks
CMD ["bash", "/benchmarks/run_all_benchmarks.sh"]
