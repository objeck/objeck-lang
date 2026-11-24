#!/bin/sh
# The Objeck library set, in dependency order. This is the ONE copy: update_version.sh
# runs it after building the compiler on Linux/macOS, and ci-build.yml runs it on the
# Windows runners with the MSBuild-built obc. It used to be duplicated in both places
# (and once in update_version_arm.sh), and the copies had to be diffed by hand.
#
# usage: build_libs.sh <path-to-obc>      (run from core/compiler)
#
# lang.obl is not here: it needs the _SYSTEM bootstrap compiler, which only the
# POSIX flow builds (update_version.sh) and Windows takes from the tree.
set -e
OBC=${1:?usage: build_libs.sh <path-to-obc>}

$OBC -src lib_src/gen_collect.obs -lib ../lib/lang -tar lib -opt s3 -dest ../lib/gen_collect.obl -strict
$OBC -src lib_src/concurrent.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/concurrent.obl
$OBC -src lib_src/json_stream.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/json_stream.obl
$OBC -src lib_src/cipher.obs -tar lib -opt s3 -dest ../lib/cipher.obl
# Exact arithmetic. Depends on nothing but lang.
$OBC -src lib_src/math.obs -tar lib -opt s3 -dest ../lib/math.obl
$OBC -src lib_src/json.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/json.obl
# The types API.Onnx and API.Inference share. Built before both, because
# both link it; needs nothing but gen_collect itself.
$OBC -src lib_src/models.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/models.obl
$OBC -src lib_src/opencv.obs -lib cipher,json -tar lib -opt s3 -dest ../lib/opencv.obl
$OBC -src lib_src/onnx.obs -lib models,opencv,cipher,json -tar lib -opt s3 -dest ../lib/onnx.obl
$OBC -src lib_src/lame.obs -tar lib -opt s3 -dest ../lib/lame.obl
$OBC -src lib_src/diags.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/diags.obl
$OBC -src lib_src/xml.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/xml.obl
$OBC -src lib_src/regex.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/regex.obl
$OBC -src lib_src/csv.obs -tar lib -lib gen_collect -opt s3 -dest ../lib/csv.obl
$OBC -src lib_src/ml_core.obs,lib_src/ml_linear.obs,lib_src/ml_tree.obs,lib_src/ml_bayes.obs,lib_src/ml_neighbors.obs,lib_src/ml_cluster.obs,lib_src/ml_data.obs,lib_src/ml_svm.obs,lib_src/ml_outlier.obs,lib_src/ml_embed.obs -lib gen_collect,csv -tar lib -opt s3 -dest ../lib/ml.obl
$OBC -src lib_src/ml_lab.obs -lib gen_collect,csv,json,ml -tar lib -opt s3 -dest ../lib/ml_lab.obl
# Gradient boosting (System.ML.Boost): regressor and classifier, XGBoost-style trees. Pure Objeck over ml, so ml.obl is untouched
$OBC -src lib_src/ml_boost.obs -lib gen_collect,csv,ml -tar lib -opt s3 -dest ../lib/ml_boost.obl
# Local vector index (exact cosine / dot / L2 search). Needs only gen_collect, so it is built apart from ml.obl
$OBC -src lib_src/ml_vector.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/ml_vector.obl
# Verification metrics (FAR, FRR, EER, ROC/DET points) and cosine similarity. Needs only gen_collect, so it links no vision library
$OBC -src lib_src/ml_verify.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/ml_verify.obl
# Time series forecasting (System.ML.TimeSeries): a TimeSeries container, transforms, baselines, exponential smoothing, ARIMA, automatic selection, accuracy metrics and rolling-origin cross-validation. Pure Objeck, needs only gen_collect
$OBC -src lib_src/ml_timeseries.obs,lib_src/ml_timeseries_models.obs,lib_src/ml_timeseries_arima.obs,lib_src/ml_timeseries_eval.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/ml_timeseries.obl
# Convolutional networks (the layers are native, in libobjk_ml). Reads images through opencv, scores with ml, writes history tables with ml_lab
$OBC -src lib_src/ml_cnn.obs -lib gen_collect,csv,json,cipher,ml,ml_lab,opencv -tar lib -opt s3 -dest ../lib/ml_cnn.obl
# Plotting: scatter, histogram, line and bar charts as SVG. Needs only gen_collect, no display, no SDL
$OBC -src lib_src/plot.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/plot.obl
# Statistics: t-tests, correlation, OLS summary. Needs only gen_collect, so it is built apart from ml.obl
$OBC -src lib_src/stats.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/stats.obl
# ml_report needs ml_lab, stats and plot, so it is built AFTER them (it used to come first, and a version bump failed: the committed stats/plot were still the previous toolchain version)
# Replicate statistics and the HTML/Markdown report builder. Needs ml_lab (ResultTable), stats and plot
$OBC -src lib_src/ml_report.obs -lib gen_collect,csv,json,ml,ml_lab,stats,plot -tar lib -opt s3 -dest ../lib/ml_report.obl
# Topic modeling: BSON reader, dictionary, LDA, coherence and an SVG topic report. After json and plot, which it links
$OBC -src lib_src/nlp_topics_text.obs,lib_src/nlp_topics_lda.obs,lib_src/nlp_topics_metrics.obs,lib_src/nlp_topics_viz.obs -lib gen_collect,json,plot -tar lib -opt s3 -dest ../lib/nlp_topics.obl
$OBC -src lib_src/ai_search.obs,lib_src/ai_game.obs,lib_src/ai_optimize.obs,lib_src/ai_rl.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/ai.obl
$OBC -src lib_src/nlp.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/nlp.obl
# Text embeddings from BERT-family ONNX models. A library of its own, after both nlp
# and onnx, so neither of them has to link the other.
$OBC -src lib_src/nlp_onnx.obs -lib gen_collect,models,opencv,cipher,json,onnx,nlp -tar lib -opt s3 -dest ../lib/nlp_onnx.obl
# CLIP image embeddings and LFW pair scoring from an exported ONNX image tower. After onnx, opencv and ml_verify
$OBC -src lib_src/ml_clip.obs -lib gen_collect,models,opencv,cipher,json,onnx,ml_verify -tar lib -opt s3 -dest ../lib/ml_clip.obl
$OBC -src lib_src/net_common.obs,lib_src/net.obs,lib_src/net_secure.obs -tar lib -lib gen_collect,cipher -opt s3 -dest ../lib/net.obl
$OBC -src lib_src/net_h2.obs -tar lib -lib net,gen_collect,cipher -opt s3 -dest ../lib/net_h2.obl
$OBC -src lib_src/net_quic.obs -tar lib -lib net,gen_collect,cipher -opt s3 -dest ../lib/net_quic.obl
$OBC -src lib_src/net_server.obs -tar lib -lib net,json,gen_collect,cipher -opt s3 -dest ../lib/net_server.obl
$OBC -src lib_src/web_server.obs -lib gen_collect,net,net_server,json,cipher -tar lib -opt s3 -dest ../lib/web_server.obl
$OBC -src lib_src/json_rpc.obs -tar lib -lib json,net -opt s3 -dest ../lib/json_rpc.obl
$OBC -src lib_src/misc.obs -lib gen_collect,net,json -tar lib -opt s3 -dest ../lib/misc.obl
$OBC -src lib_src/rss.obs -tar lib -lib xml,gen_collect,net,cipher -opt s3 -dest ../lib/rss.obl
$OBC -src lib_src/query.obs -tar lib -lib net,regex,csv,xml,json,misc -opt s3 -dest ../lib/query.obl
$OBC -src lib_src/query_server.obs -tar lib -lib net,net_server,regex,csv,xml,json,misc,query -opt s3 -dest ../lib/query_server.obl
$OBC -src lib_src/query_ml.obs -tar lib -lib gen_collect,csv,ml,net,regex,xml,json,misc,query -opt s3 -dest ../lib/query_ml.obl
$OBC -src lib_src/query_vision.obs -tar lib -lib gen_collect,models,opencv,onnx,cipher,json,net,regex,csv,xml,misc,query -opt s3 -dest ../lib/query_vision.obl
$OBC -src lib_src/odbc.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/odbc.obl
$OBC -src lib_src/query_odbc.obs -tar lib -lib gen_collect,odbc,net,regex,csv,xml,json,misc,query -opt s3 -dest ../lib/query_odbc.obl
$OBC -src lib_src/openai.obs -lib json,net,net_server,cipher,misc -tar lib -opt s3 -dest ../lib/openai.obl
$OBC -src lib_src/gemini.obs -lib misc,json,net,net_server,cipher -tar lib -opt s3 -dest ../lib/gemini.obl
$OBC -src lib_src/ollama.obs -lib net,json,cipher,misc -tar lib -opt s3 -dest ../lib/ollama.obl
$OBC -src lib_src/anthropic.obs -lib net,json,cipher,misc -tar lib -opt s3 -dest ../lib/anthropic.obl
# LLM utilities: prompt templates, token estimation, JSON Schema validation, structured calls with retry. Pure Objeck; needs only gen_collect, json and regex, and links no client
$OBC -src lib_src/llm_util.obs -lib gen_collect,json,regex -tar lib -opt s3 -dest ../lib/llm_util.obl
# Adapters from llm_util's ChatFn and TokenCounter to the Anthropic, OpenAI, Gemini and Ollama clients. A library of its own so llm_util links no client
$OBC -src lib_src/llm_util_clients.obs -lib gen_collect,json,regex,llm_util,net,net_server,cipher,misc,anthropic,openai,gemini,ollama -tar lib -opt s3 -dest ../lib/llm_util_clients.obl
$OBC -src lib_src/agent.obs -lib net,json,cipher,misc -tar lib -opt s3 -dest ../lib/agent.obl
$OBC -src lib_src/inference.obs -lib models,net,json,cipher,gen_collect -tar lib -opt s3 -dest ../lib/inference.obl
# Hugging Face Hub downloads and the model cache (docs/AI_FOUNDATION_DESIGN.md sec. 7.3)
$OBC -src lib_src/ai_hub.obs -lib gen_collect,cipher,json,net -tar lib -opt s3 -dest ../lib/ai_hub.obl
$OBC -src lib_src/sdl2.obs -tar lib -dest ../lib/sdl2.obl
$OBC -src lib_src/sdl_game.obs -lib gen_collect,json,sdl2 -tar lib -dest ../lib/sdl_game.obl
$OBC -src lib_src/sdl_gl.obs -lib gen_collect,sdl2 -tar lib -dest ../lib/sdl_gl.obl
# The SDL2 backend of System.Plot: an interactive window and PNG export. After plot and sdl2
$OBC -src lib_src/plot_sdl.obs -lib gen_collect,plot,sdl2 -tar lib -opt s3 -dest ../lib/plot_sdl.obl
