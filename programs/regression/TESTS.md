# Regression Test Manifest

This document inventories every test in `programs/regression/`. The suite runs via
`run_regression.sh` (Linux/macOS) or `run_regression.cmd` (Windows) and in CI on
every push; each test compiles with `obc` and runs with `obr`. Tests marked
**(neg)** expect a compile or runtime error (`# EXPECT_COMPILE_ERROR` /
`# EXPECT_RUNTIME_ERROR`) and pass when that error is produced.

This file is generated — regenerate it after adding or removing tests with:

```
python gen_manifest.py
```


**Total runtime tests: 152** (plus 14 debugger tests, see below).


## Tests by Category

| Category | Count |
|----------|-------|
| Core Language | 34 |
| Negative | 15 |
| AMD64/JIT | 13 |
| Bug Fix | 13 |
| System.ML | 13 |
| Collections | 10 |
| Math | 6 |
| Functional | 5 |
| ARM64 JIT | 4 |
| Generics | 4 |
| Generics (neg) | 4 |
| Strings | 4 |
| System.AI | 4 |
| API (network) | 3 |
| XML | 3 |
| Date/Time | 2 |
| JSON | 2 |
| MCP Server | 2 |
| Networking | 2 |
| Regex | 2 |
| Concurrency/GC | 1 |
| Control Flow | 1 |
| Debugger | 1 |
| Exceptions | 1 |
| I/O | 1 |
| LSP | 1 |
| ODBC | 1 |

## Test Inventory

| # | Test | Category | What it validates | Status |
|---|------|----------|-------------------|--------|
| 1 | `ai_game_test.obs` | System.AI | Regression tests for System.AI adversarial search using tic-tac-toe: Minimax takes a win-in-1, bl... | ✅ |
| 2 | `ai_optimize_test.obs` | System.AI | Regression tests for System.AI optimization: GeneticAlgorithm solves OneMax (all-ones chromosome)... | ✅ |
| 3 | `ai_rl_test.obs` | System.AI | Regression tests for System.AI reinforcement learning using a 6-state chain world (move right to... | ✅ |
| 4 | `ai_search_test.obs` | System.AI | Regression tests for System.AI graph search: Dijkstra finds the minimum-cost path even when a dir... | ✅ |
| 5 | `api_gemini_test.obs` | API (network) | api gemini test | ✅ |
| 6 | `api_ollama_test.obs` | API (network) | api ollama test | ✅ |
| 7 | `api_openai_test.obs` | API (network) | api openai test | ✅ |
| 8 | `arm64_bitwise.obs` | ARM64 JIT | ARM64 Bitwise Operations Test Tests bitwise AND, OR, XOR operations | ✅ |
| 9 | `arm64_char_arrays.obs` | ARM64 JIT | ARM64 Character Array Test Tests STRH/LDRH instruction encoding (16-bit operations) | ✅ |
| 10 | `arm64_large_immediates.obs` | ARM64 JIT | ARM64 Large Immediate Test Tests add_imm_reg/sub_imm_reg with values > 4095 | ✅ |
| 11 | `arm64_multiply_constants.obs` | ARM64 JIT | ARM64 Multiply-by-Constant Test Tests multiply-by-constant optimization (especially multiply-by-6) | ✅ |
| 12 | `bad_array_too_many_dimensions.obs` | Negative | rejects array too many dimensions | ✅ |
| 13 | `bad_call_undefined.obs` | Negative | rejects call undefined | ✅ |
| 14 | `bad_duplicate_class.obs` | Negative | rejects duplicate class | ✅ |
| 15 | `bad_generic_arg_mismatch.obs` | Generics (neg) | Guards the generic type-argument compatibility check (and its readable diagnostic). Assigning a V... | ✅ |
| 16 | `bad_generic_compound_bound.obs` | Generics (neg) | A concrete type argument that satisfies only one of a compound bound (T : A & B) must be rejected... | ✅ |
| 17 | `bad_generic_fbound.obs` | Generics (neg) | A concrete argument that does not satisfy an F-bounded constraint (T : Compare<T>) must be reject... | ✅ |
| 18 | `bad_generic_variance.obs` | Generics (neg) | Variance must stay sound: a covariant 'out T' does NOT allow the reverse direction. Producer<Anim... | ✅ |
| 19 | `bad_inherit_unknown.obs` | Negative | rejects inherit unknown | ✅ |
| 20 | `bad_keyword_as_var.obs` | Negative | rejects keyword as var | ✅ |
| 21 | `bad_readonly_record_assignment.obs` | Negative | rejects readonly record assignment | ✅ |
| 22 | `bad_readonly_record_op_assignment.obs` | Negative | rejects readonly record op assignment | ✅ |
| 23 | `bad_runtime_bounds.obs` | Negative | VM must not crash (segfault) on array out-of-bounds access — exit 1 with message | ✅ |
| 24 | `bad_runtime_divzero.obs` | Negative | VM must not crash (segfault) on integer division by zero — exit 1 with message | ✅ |
| 25 | `bad_runtime_null.obs` | Negative | VM must not crash (segfault) when dereferencing a nil object — exit 1 with message | ✅ |
| 26 | `bad_runtime_stack.obs` | Negative | VM must not crash (segfault) on call stack overflow — exit 1 with message. NOTE 1: the recursion... | ✅ |
| 27 | `bad_syntax_unclosed.obs` | Negative | rejects syntax unclosed | ✅ |
| 28 | `bad_undefined_type.obs` | Negative | rejects undefined type | ✅ |
| 29 | `bad_undefined_var.obs` | Negative | rejects undefined var | ✅ |
| 30 | `bad_wrong_return.obs` | Negative | rejects wrong return | ✅ |
| 31 | `collect_compare_vector.obs` | Collections | collect compare vector | ✅ |
| 32 | `collect_each_patterns.obs` | Collections | collect each patterns | ✅ |
| 33 | `collect_hash_ops.obs` | Collections | collect hash ops | ✅ |
| 34 | `collect_map_ops.obs` | Collections | collect map ops | ✅ |
| 35 | `collect_nested_ops.obs` | Collections | collect nested ops | ✅ |
| 36 | `collect_pair_ops.obs` | Collections | collect pair ops | ✅ |
| 37 | `collect_queue_ops.obs` | Collections | collect queue ops | ✅ |
| 38 | `collect_set_ops.obs` | Collections | collect set ops | ✅ |
| 39 | `collect_stack_ops.obs` | Collections | collect stack ops | ✅ |
| 40 | `collect_vector_ops.obs` | Collections | collect vector ops | ✅ |
| 41 | `core_abstract_virtual.obs` | Core Language | core abstract virtual | ✅ |
| 42 | `core_arithmetic.obs` | Core Language | Core Arithmetic Operations Test Tests basic arithmetic operations, type conversions, and operator... | ✅ |
| 43 | `core_array_operations.obs` | Core Language | core array operations | ✅ |
| 44 | `core_arrays_simple.obs` | Core Language | Core Array Operations Test (Simplified) Tests basic array creation, access, and modification | ✅ |
| 45 | `core_bitwise_ops.obs` | Core Language | core bitwise ops | ✅ |
| 46 | `core_bool_ops.obs` | Core Language | core bool ops | ✅ |
| 47 | `core_break_continue.obs` | Core Language | core break continue | ✅ |
| 48 | `core_char_methods.obs` | Core Language | core char methods | ✅ |
| 49 | `core_classes.obs` | Core Language | Core Classes Test Tests class instantiation, inheritance, method calls, and select statements Bas... | ✅ |
| 50 | `core_collections_perf.obs` | Core Language | core collections perf | ✅ |
| 51 | `core_control_flow.obs` | Core Language | Core Control Flow Test Tests if/else, loops, and select statements | ✅ |
| 52 | `core_do_while.obs` | Core Language | core do while | ✅ |
| 53 | `core_each_loop.obs` | Core Language | core each loop | ✅ |
| 54 | `core_enum.obs` | Core Language | core enum | ✅ |
| 55 | `core_function_refs.obs` | Core Language | core function refs | ✅ |
| 56 | `core_generic_compound_bounds.obs` | Generics | Compound generic bounds (T : A & B): a concrete type argument must satisfy ALL bounds. Person imp... | ✅ |
| 57 | `core_generic_fbound.obs` | Generics | F-bounded type-parameter constraint (T : Compare<T>): the bound may be generic and self-referenti... | ✅ |
| 58 | `core_generic_structural.obs` | Generics | Exercises the structural generic type comparison: deeply nested generic type arguments must round... | ✅ |
| 59 | `core_generic_variance.obs` | Generics | Declaration-site variance: 'out T' (covariant) lets Producer<Dog> be used where Producer<Animal>... | ✅ |
| 60 | `core_http_server.obs` | Core Language | HTTP Client/Server Loopback Test Tests HTTP GET and POST using raw TCP server + HttpClient. Verif... | ✅ |
| 61 | `core_inheritance_chain.obs` | Core Language | core inheritance chain | ✅ |
| 62 | `core_int_methods.obs` | Core Language | core int methods | ✅ |
| 63 | `core_interfaces.obs` | Core Language | core interfaces | ✅ |
| 64 | `core_json_escape.obs` | Core Language | core json escape | ✅ |
| 65 | `core_method_overload.obs` | Core Language | core method overload | ✅ |
| 66 | `core_multi_dim_array.obs` | Core Language | core multi dim array | ✅ |
| 67 | `core_net_buffer.obs` | Core Language | Network Buffer Read Test Tests that TCP socket ReadBuffer correctly handles partial reads by veri... | ✅ |
| 68 | `core_odbc.obs` | Core Language | Core ODBC Bindings Test Tests Date, Timestamp, and ColumnInfo classes without requiring a databas... | ✅ |
| 69 | `core_opencv.obs` | Core Language | Core OpenCV Bindings Test Tests helper classes, constants, and VideoWriter FourCC without requiri... | ✅ |
| 70 | `core_records.obs` | Core Language | Core Records Test Exercises record-generated constructors, accessors, mutators, generics, readonl... | ✅ |
| 71 | `core_recursion.obs` | Core Language | Core Recursion Test Tests recursive function calls and tail recursion | ✅ |
| 72 | `core_select_ops.obs` | Core Language | core select ops | ✅ |
| 73 | `core_static_array_literals.obs` | Core Language | Regression test for the static-array literal pool (compiler bug, 2026-06): the bool literal-pool... | ✅ |
| 74 | `core_static_fields.obs` | Core Language | core static fields | ✅ |
| 75 | `core_string_format.obs` | Core Language | core string format | ✅ |
| 76 | `core_string_methods.obs` | Core Language | core string methods | ✅ |
| 77 | `core_strings_simple.obs` | Core Language | Core String Operations Test (Simplified) Tests basic string operations without complex method cha... | ✅ |
| 78 | `core_thread_gc_stress.obs` | Concurrency/GC | Multithreaded GC stop-the-world stress test. Guards the GC bugs fixed on branch fix/gc-stop-the-w... | ✅ |
| 79 | `core_type_checking.obs` | Core Language | core type checking | ✅ |
| 80 | `date_arithmetic.obs` | Date/Time | date arithmetic | ✅ |
| 81 | `date_basic_ops.obs` | Date/Time | date basic ops | ✅ |
| 82 | `debugger_test.obs` | Debugger | debugger test | ✅ |
| 83 | `fix524_array_cast_chain.obs` | Bug Fix | Fix #524: Cannot chain method calls on array-indexed elements after cast Tests that Get(index)->A... | ✅ |
| 84 | `fix534_substring_crash.obs` | Bug Fix | Fix #534: String->SubString crash on negative or zero length argument Tests that negative or zero... | ✅ |
| 85 | `fix_array_bounds.obs` | Bug Fix | fix array bounds | ✅ |
| 86 | `fix_chained_calls.obs` | Bug Fix | fix chained calls | ✅ |
| 87 | `fix_deep_recursion.obs` | Bug Fix | fix deep recursion | ✅ |
| 88 | `fix_float_precision.obs` | Bug Fix | fix float precision | ✅ |
| 89 | `fix_int_boundary.obs` | Bug Fix | fix int boundary | ✅ |
| 90 | `fix_large_arrays.obs` | Bug Fix | fix large arrays | ✅ |
| 91 | `fix_nested_generics.obs` | Bug Fix | fix nested generics | ✅ |
| 92 | `fix_nil_chain_ops.obs` | Bug Fix | fix nil chain ops | ✅ |
| 93 | `fix_polymorphic_calls.obs` | Bug Fix | fix polymorphic calls | ✅ |
| 94 | `fix_scope_shadowing.obs` | Bug Fix | fix scope shadowing | ✅ |
| 95 | `fix_string_concat.obs` | Bug Fix | fix string concat | ✅ |
| 96 | `func_closure_field.obs` | Functional | func closure field | ✅ |
| 97 | `func_filter_ops.obs` | Functional | func filter ops | ✅ |
| 98 | `func_higher_order.obs` | Functional | func higher order | ✅ |
| 99 | `func_reduce_ops.obs` | Functional | func reduce ops | ✅ |
| 100 | `func_sort_custom.obs` | Functional | func sort custom | ✅ |
| 101 | `io_file_basic.obs` | I/O | io file basic | ✅ |
| 102 | `jit_array_native.obs` | AMD64/JIT | jit array native | ✅ |
| 103 | `jit_conditional_native.obs` | AMD64/JIT | jit conditional native | ✅ |
| 104 | `jit_dispatch_native.obs` | AMD64/JIT | jit dispatch native | ✅ |
| 105 | `jit_float_equality.obs` | AMD64/JIT | Regression test for float equality compares on array elements (2026-06). The front-end chose EQL_... | ✅ |
| 106 | `jit_float_intensive.obs` | AMD64/JIT | jit float intensive | ✅ |
| 107 | `jit_frame_trap_test.obs` | AMD64/JIT | Regression test for the JIT frame-dependent trap crash (2026-06). Traps such as SERL_INT/SERL_FLO... | ✅ |
| 108 | `jit_gc_stress.obs` | AMD64/JIT | JIT + GC interaction stress (2026-06). One CI run on linux-x64 failed with a JIT-to-JIT runtime e... | ✅ |
| 109 | `jit_loop_native.obs` | AMD64/JIT | jit loop native | ✅ |
| 110 | `jit_native_cls_fields.obs` | AMD64/JIT | JIT Native Class Fields Test Tests object reference storage in class instance fields with GC pres... | ✅ |
| 111 | `jit_native_float_array.obs` | AMD64/JIT | JIT Native Float Array Test Tests native function with float array creation and math operations R... | ✅ |
| 112 | `jit_native_func_ref.obs` | AMD64/JIT | JIT Native Function Reference Test Tests native functions with function reference storage in clas... | ✅ |
| 113 | `jit_native_math.obs` | AMD64/JIT | JIT Native Math Builtins Test Tests native math functions: Factorial, Sinh/Cosh/Tanh/Log2/Cbrt, P... | ✅ |
| 114 | `jit_string_ops.obs` | AMD64/JIT | jit string ops | ✅ |
| 115 | `json_build_ops.obs` | JSON | json build ops | ✅ |
| 116 | `json_parse_ops.obs` | JSON | json parse ops | ✅ |
| 117 | `lsp_features.obs` | LSP | lsp features | ✅ |
| 118 | `math_float_ops.obs` | Math | math float ops | ✅ |
| 119 | `math_log_exp.obs` | Math | math log exp | ✅ |
| 120 | `math_random_ops.obs` | Math | math random ops | ✅ |
| 121 | `math_rounding.obs` | Math | math rounding | ✅ |
| 122 | `math_sqrt_ops.obs` | Math | math sqrt ops | ✅ |
| 123 | `math_trig_funcs.obs` | Math | math trig funcs | ✅ |
| 124 | `mcp_debug_test.obs` | MCP Server | DEBUG VERSION of mcp_server_test.obs Identical to programs/regression/mcp_server_test.obs except:... | ✅ |
| 125 | `mcp_server_test.obs` | MCP Server | mcp server test | ✅ |
| 126 | `ml_adaboost_test.obs` | System.ML | Regression tests for System.ML AdaBoost (overhaul phase 3): boosting over boolean decision stumps... | ✅ |
| 127 | `ml_api_test.obs` | System.ML | Regression tests for the System.ML estimator API consistency sweep (item 11): RandomForest Fit (r... | ✅ |
| 128 | `ml_dbscan_test.obs` | System.ML | Regression tests for System.ML DBSCAN (overhaul phase 3): two dense blobs plus far-away outliers... | ✅ |
| 129 | `ml_gbt_test.obs` | System.ML | Regression tests for System.ML gradient boosting (overhaul phase 3 leftover): a RegressionTree le... | ✅ |
| 130 | `ml_gmm_test.obs` | System.ML | Regression tests for System.ML GaussianMixture (overhaul phase 3): EM on two well-separated blobs... | ✅ |
| 131 | `ml_kdtree_test.obs` | System.ML | Regression tests for System.ML KDTree (overhaul phase 3): for several queries and k values over a... | ✅ |
| 132 | `ml_library_test.obs` | System.ML | ml library test | ✅ |
| 133 | `ml_linearclf_test.obs` | System.ML | Regression tests for the System.ML linear classifiers (overhaul phase 2): Perceptron (mistake-dri... | ✅ |
| 134 | `ml_nn_test.obs` | System.ML | Regression tests for the System.ML NeuralNetwork with hidden/output bias vectors (ML overhaul ite... | ✅ |
| 135 | `ml_pca_gnb_test.obs` | System.ML | Regression tests for System.ML PCA (power-iteration decomposition: dominant diagonal direction re... | ✅ |
| 136 | `ml_phase1_test.obs` | System.ML | Regression tests for the System.ML correctness fixes (phase 1): seedable PRNG, DotSigmoid dimensi... | ✅ |
| 137 | `ml_regularized_test.obs` | System.ML | Regression tests for the System.ML regularized linear models (overhaul phase 2): RidgeRegression... | ✅ |
| 138 | `ml_trees_test.obs` | System.ML | Regression tests for the System.ML tree models: the real recursive DecisionTree (left/right child... | ✅ |
| 139 | `oauth_test.obs` | Networking | oauth test | ✅ |
| 140 | `odbc_sqlite_test.obs` | ODBC | ODBC SQLite Integration Test Tests live database operations against an in-memory SQLite database.... | ✅ |
| 141 | `regex_bench.obs` | Regex | regex bench | ✅ |
| 142 | `regex_dfa_test.obs` | Regex | regex dfa test | ✅ |
| 143 | `select_dispatch_test.obs` | Control Flow | Single-case, linear (2-5 cases), jump-table (dense >=6), and binary-tree (sparse) paths | ✅ |
| 144 | `string_find_ops.obs` | Strings | string find ops | ✅ |
| 145 | `string_number_conv.obs` | Strings | string number conv | ✅ |
| 146 | `string_replace_ops.obs` | Strings | string replace ops | ✅ |
| 147 | `string_split_ops.obs` | Strings | string split ops | ✅ |
| 148 | `try_otherwise.obs` | Exceptions | Try/Otherwise Error Handling Test Tests the Try() and Otherwise() intrinsic methods for error han... | ✅ |
| 149 | `websocket_test.obs` | Networking | websocket test | ✅ |
| 150 | `xml_build_ops.obs` | XML | xml build ops | ✅ |
| 151 | `xml_encoding_ops.obs` | XML | Unit tests for the 2026-06 Data.XML improvements: truncated/garbage input is rejected (previously... | ✅ |
| 152 | `xml_parse_ops.obs` | XML | xml parse ops | ✅ |

## Debugger Tests (`run_debugger_tests.sh`)

| # | Test | What it validates |
|---|------|-------------------|
| 1 | `help` | Help command displays all commands |
| 2 | `breakpoints` | Set, list, and delete breakpoints |
| 3 | `run_break` | Run program and hit breakpoint |
| 4 | `print_vars` | Print Int, array, and object variables |
| 5 | `step_into` | Step into method calls |
| 6 | `step_over` | Step over method calls |
| 7 | `stack_trace` | Call stack display |
| 8 | `list_source` | Source code listing |
| 9 | `memory` | Memory allocation stats |
| 10 | `info` | Program/class information |
| 11 | `print_self` | Print `@self` and instance variables |
| 12 | `step_out` | Step out of method (jump) |
| 13 | `full_run` | Full program execution without breakpoints |
| 14 | `clear_breaks` | Clear all breakpoints |

The DAP (Debug Adapter Protocol) tests run separately via `run_dap_tests.sh`.

## Running the Suite

```
cd programs/regression
./run_regression.sh x64            # or arm64; run_regression.cmd on Windows
TEST_TIMEOUT=120 ./run_regression.sh x64   # per-test wall-clock cap (seconds)
```

The runner compiles and runs every `*.obs`, reports a `PASS`/`FAIL` per test, and
prints a final `Results: N passed, M failed` summary (and a GitHub Actions step
summary in CI). Network-dependent tests (`api_*`, `oauth_*`, `mcp_*`) may be
skipped or quarantined in CI when the target service is unavailable.
