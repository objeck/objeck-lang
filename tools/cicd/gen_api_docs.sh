#!/usr/bin/env bash
# gen_api_docs.sh <version> [<deploy dir>]
#
# Generates the API reference with code_doc and packages it as docs/api.zip (and the
# docs/api tree), refusing to package anything incomplete or mis-stamped.
#
# This is the one place the generation lives. release-build.yml's "Generate API Docs"
# job runs it and publishes docs/api.zip as the `api-docs` artifact; every build job
# of that run downloads the artifact before it deploys, and release-publish.yml
# deploys the same artifact to objeck.org. docs/api.zip is not committed.
#
# <deploy dir> is a POSIX deploy tree (default core/release/deploy) holding bin/obc,
# bin/obr and lib/*.obl at <version>: run it after deploy_posix.sh and
# core/compiler/update_version.sh, as the release job does. Run from anywhere.
set -euo pipefail

VERSION="${1:?usage: gen_api_docs.sh <version> [<deploy dir>]}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPLOY="${2:-$ROOT/core/release/deploy}"
DEPLOY="$(cd "$DEPLOY" && pwd)"

cd "$DEPLOY/bin"
export OBJECK_LIB_PATH="$DEPLOY/lib"
SRC="$ROOT/core/compiler/lib_src"
LIBDOC="$ROOT/core/lib/code_doc"

# code_doc writes every page to a HARDCODED '../html/' -- relative to
# obr's cwd, so <deploy>/html -- and it does NOT create that
# directory. doc_html.obs builds its paths as GetClassLink(.., "../html/")
# and opens them with FileWriter, which fails silently when the directory
# is absent: the run prints "Writing API files...", spins through every
# class, exits 0, and writes nothing. (That is why POSIX generated zero pages
# on every release until 2026-09-02.) code_doc64.cmd has always done this
# mkdir-then-copy; mirror it.
rm -rf ../html
mkdir -p ../html
rm -rf "$ROOT/docs/api"
mkdir -p "$ROOT/docs/api"

./obc -src "$LIBDOC/doc_html.obs,$LIBDOC/doc_parser.obs" \
  -lib cipher,json,xml,misc,net -dest code_doc.obe

set -o pipefail
./obr code_doc.obe \
  "$LIBDOC/templates" \
  "$VERSION" \
  "$SRC/lang.obs" \
  "$SRC/regex.obs" \
  "$SRC/json_stream.obs" \
  "$SRC/json.obs" \
  "$SRC/xml.obs" \
  "$SRC/cipher.obs" \
  "$SRC/odbc.obs" \
  "$SRC/csv.obs" \
  "$SRC/math.obs" \
  "$SRC/query.obs" \
  "$SRC/query_server.obs" \
  "$SRC/query_ml.obs" \
  "$SRC/query_odbc.obs" \
  "$SRC/query_vision.obs" \
  "$SRC/sdl2.obs" \
  "$SRC/sdl_game.obs" \
  "$SRC/sdl_gl.obs" \
  "$SRC/gen_collect.obs" \
  "$SRC/concurrent.obs" \
  "$SRC/actor.obs" \
  "$SRC/net_common.obs" \
  "$SRC/net.obs" \
  "$SRC/net_secure.obs" \
  "$SRC/net_server.obs" \
  "$SRC/net_h2.obs" \
  "$SRC/net_quic.obs" \
  "$SRC/web_server.obs" \
  "$SRC/rss.obs" \
  "$SRC/misc.obs" \
  "$SRC/diags.obs" \
  "$SRC/ml_core.obs" \
  "$SRC/ml_linear.obs" \
  "$SRC/ml_tree.obs" \
  "$SRC/ml_bayes.obs" \
  "$SRC/ml_neighbors.obs" \
  "$SRC/ml_cluster.obs" \
  "$SRC/ml_data.obs" \
  "$SRC/ml_svm.obs" \
  "$SRC/ml_outlier.obs" \
  "$SRC/ml_embed.obs" \
  "$SRC/ml_lab.obs" \
  "$SRC/ml_boost.obs" \
  "$SRC/ml_vector.obs" \
  "$SRC/ml_report.obs" \
  "$SRC/ml_verify.obs" \
  "$SRC/ml_timeseries.obs" \
  "$SRC/ml_timeseries_models.obs" \
  "$SRC/ml_timeseries_arima.obs" \
  "$SRC/ml_timeseries_eval.obs" \
  "$SRC/ml_cnn.obs" \
  "$SRC/nlp_topics_text.obs" \
  "$SRC/nlp_topics_lda.obs" \
  "$SRC/nlp_topics_metrics.obs" \
  "$SRC/nlp_topics_viz.obs" \
  "$SRC/plot.obs" \
  "$SRC/plot_sdl.obs" \
  "$SRC/stats.obs" \
  "$SRC/telemetry.obs" \
  "$SRC/ai_search.obs" \
  "$SRC/ai_game.obs" \
  "$SRC/ai_optimize.obs" \
  "$SRC/ai_rl.obs" \
  "$SRC/openai.obs" \
  "$SRC/gemini.obs" \
  "$SRC/ollama.obs" \
  "$SRC/anthropic.obs" \
  "$SRC/llm_util.obs" \
  "$SRC/agent.obs" \
  "$SRC/actor_agents.obs" \
  "$SRC/mcp.obs" \
  "$SRC/mcp_agent.obs" \
  "$SRC/agent_json.obs" \
  "$SRC/rag.obs" \
  "$SRC/rag_adapters.obs" \
  "$SRC/json_rpc.obs" \
  "$SRC/models.obs" \
  "$SRC/opencv.obs" \
  "$SRC/onnx.obs" \
  "$SRC/inference.obs" \
  "$SRC/nlp.obs" \
  "$SRC/nlp_onnx.obs" \
  "$SRC/ml_clip.obs" \
  "$SRC/ai_hub.obs" \
  "$SRC/lame.obs" \
  2>&1 | tee code_doc.log

# A doc comment the parser rejects drops its member from the API reference, and
# code_doc still exits 0 (issue #729). check_code_doc_log.py knows every failure
# output ('ERROR:', '*** Unable to parse ...', '--- Unable ...'). Refuse to package.
python3 "$ROOT/tools/cicd/check_code_doc_log.py" code_doc.log

# Move the generated tree to where the packaging step looks, and bring the
# stylesheet and logos across as code_doc64.cmd does -- without style/ every
# page renders unstyled.
cp -r ../html/. "$ROOT/docs/api/"
mkdir -p "$ROOT/docs/api/style"
cp "$LIBDOC/templates/resources/"* "$ROOT/docs/api/style/"

echo "generated $(find "$ROOT/docs/api" -name '*.html' | wc -l) HTML pages"

cd "$ROOT/docs"

# Count what code_doc ACTUALLY generated, before any zip exists. An old gate counted
# entries inside api.zip -- but then api.zip was tracked, so a copy was always there
# from the checkout and `zip -r` UPDATED it in place; when generation produced
# nothing the gate counted the stale entries and passed. That shipped v2026.8.0's
# docs as the docs for v2026.8.1 and v2026.8.2.
GEN_COUNT=$(find api -name '*.html' 2>/dev/null | wc -l)
if [ "$GEN_COUNT" -lt 50 ]; then
  echo "::error::code_doc generated only $GEN_COUNT HTML files (expected 50+)"
  exit 1
fi

# Build the archive FRESH, never update an existing one.
rm -f api.zip
zip -r api.zip api/ > /dev/null

HTML_COUNT=$(unzip -l api.zip | grep -c "\.html$" || true)
if [ "$HTML_COUNT" -ne "$GEN_COUNT" ]; then
  echo "::error::packaged $HTML_COUNT HTML files but generated $GEN_COUNT -- archive does not match the generated tree"
  exit 1
fi

# Content, not just shape: assert the pages carry THIS release's version.
# A well-formed archive built against the wrong version passes every
# structural check, which is precisely how the stale docs went unnoticed.
PAGE=$(find api -name '*.html' | awk 'NR==1')
STAMP=$(grep -o 'class="version">v[0-9.]*' "$PAGE" | awk 'NR==1' | sed 's/.*>//')
if [ -z "$STAMP" ]; then
  echo "::error::no version stamp found in $PAGE -- treat as failure, not a pass"
  exit 1
fi
if [ "$STAMP" != "v$VERSION" ]; then
  echo "::error::api docs stamp $STAMP but this release is v$VERSION"
  exit 1
fi

echo "API documentation packaged: $HTML_COUNT HTML files, stamped $STAMP -> $ROOT/docs/api.zip"
