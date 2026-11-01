#!/usr/bin/env bash
#
# local_docker.sh [REF] [--jit1] [--flags] [--skip-regression]
#
# The Linux leg of ci-build.yml in a clean ubuntu:24.04 container: the same image the
# runner is pinned to, with the same apt packages, the same ODBC data source, and the
# committed tree built from scratch. The arguments go to local_linux.sh, which does the
# work; this only supplies the machine. From Git Bash, WSL2 or Linux, with Docker running.
#
# The image is cached after the first build (about two minutes); the build tree lives in
# a Docker volume (objeck-ci-work), so it is not slowed by the bind mount and survives
# between runs, and the QUIC libraries (AWS-LC, ngtcp2, nghttp3, nghttp2: about ten
# minutes the first time) in another (objeck-ci-deps). `docker volume rm objeck-ci-work
# objeck-ci-deps` clears them.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"
IMAGE=objeck-ci-linux:24.04

if ! docker info >/dev/null 2>&1; then
  echo "Docker is not running: start Docker Desktop first"
  exit 2
fi

# The package list is ci-build.yml's "Install Linux dependencies" step, plus what the
# runner image already carries (git, python3, openssl, certificates).
docker build -q -t "$IMAGE" - <<'EOF'
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential libopencv-dev libmbedtls-dev unixodbc-dev libsqliteodbc \
      libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-mixer-dev \
      libegl1 libegl-mesa0 libgl1-mesa-dri libreadline-dev libeigen3-dev libmp3lame-dev \
      ccache expect git python3 openssl ca-certificates cmake \
    && rm -rf /var/lib/apt/lists/*
EOF

# MSYS_NO_PATHCONV: Git Bash must not rewrite the container-side paths.
src="$PWD"
if command -v cygpath >/dev/null 2>&1; then
  src="$(cygpath -w "$PWD")"
fi
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "$src":/src:ro \
  -v objeck-ci-work:/work -e OBJECK_CI_WORK=/work \
  -v objeck-ci-deps:/root/objeck-deps \
  -w /src "$IMAGE" bash tools/cicd/local_linux.sh "$@"
