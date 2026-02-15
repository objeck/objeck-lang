FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    ruby \
    luajit \
    bc \
    sed \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /benchmarks
COPY benchmarks/ /benchmarks/

RUN sed -i 's/\r$//' /benchmarks/*.sh /benchmarks/*.py /benchmarks/*.rb /benchmarks/*.lua && \
    chmod +x /benchmarks/run_cross_lang.sh

CMD ["bash", "/benchmarks/run_cross_lang.sh"]
