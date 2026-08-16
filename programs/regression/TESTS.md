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


**Total runtime tests: 190** (plus 14 debugger tests, see below).


## Tests by Category

| Category | Count |
|----------|-------|
| Core Language | 38 |
| Negative | 21 |
| AMD64/JIT | 20 |
| Other | 16 |
| Bug Fix | 13 |
| System.ML | 13 |
| Collections | 10 |
| Math | 6 |
| Functional | 5 |
| Strings | 5 |
| ARM64 JIT | 4 |
| Debugger | 4 |
| Generics | 4 |
| Generics (neg) | 4 |
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
| Debugger (neg) | 1 |
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
| 14 | `bad_div_zero_folded.obs` | Negative | A constant division by zero must still trap at runtime, at every optimization level. The constant... | ✅ |
| 15 | `bad_duplicate_class.obs` | Negative | rejects duplicate class | ✅ |
| 16 | `bad_generic_arg_mismatch.obs` | Generics (neg) | Guards the generic type-argument compatibility check (and its readable diagnostic). Assigning a V... | ✅ |
| 17 | `bad_generic_compound_bound.obs` | Generics (neg) | A concrete type argument that satisfies only one of a compound bound (T : A & B) must be rejected... | ✅ |
| 18 | `bad_generic_fbound.obs` | Generics (neg) | A concrete argument that does not satisfy an F-bounded constraint (T : Compare<T>) must be reject... | ✅ |
| 19 | `bad_generic_variance.obs` | Generics (neg) | Variance must stay sound: a covariant 'out T' does NOT allow the reverse direction. Producer<Anim... | ✅ |
| 20 | `bad_inherit_unknown.obs` | Negative | rejects inherit unknown | ✅ |
| 21 | `bad_int_literal_overflow.obs` | Negative | An integer literal past the signed range must be rejected. It used to be parsed with a saturating... | ✅ |
| 22 | `bad_keyword_as_var.obs` | Negative | rejects keyword as var | ✅ |
| 23 | `bad_mod_zero.obs` | Negative | Modulus by zero must trap like division by zero does. The interpreter's ModInt had no zero check... | ✅ |
| 24 | `bad_readonly_record_assignment.obs` | Negative | rejects readonly record assignment | ✅ |
| 25 | `bad_readonly_record_op_assignment.obs` | Negative | rejects readonly record op assignment | ✅ |
| 26 | `bad_runtime_bounds.obs` | Negative | VM must not crash (segfault) on array out-of-bounds access — exit 1 with message | ✅ |
| 27 | `bad_runtime_divzero.obs` | Negative | VM must not crash (segfault) on integer division by zero — exit 1 with message | ✅ |
| 28 | `bad_runtime_null.obs` | Negative | VM must not crash (segfault) when dereferencing a nil object — exit 1 with message | ✅ |
| 29 | `bad_runtime_stack.obs` | Negative | VM must not crash (segfault) on call stack overflow — exit 1 with message. NOTE 1: the recursion... | ✅ |
| 30 | `bad_string_interp_format.obs` | Negative | An unrecognized interpolation format specifier must be rejected at compile time. | ✅ |
| 31 | `bad_syntax_unclosed.obs` | Negative | rejects syntax unclosed | ✅ |
| 32 | `bad_undefined_type.obs` | Negative | rejects undefined type | ✅ |
| 33 | `bad_undefined_var.obs` | Negative | rejects undefined var | ✅ |
| 34 | `bad_unsigned_literal_suffix.obs` | Negative | The 'u' suffix reads a literal across the unsigned range, so a value past 2^64-1 has no represent... | ✅ |
| 35 | `bad_unsigned_suffix_on_float.obs` | Negative | The unsigned suffix is meaningless on a floating-point literal and must not be accepted silently. | ✅ |
| 36 | `bad_wrong_return.obs` | Negative | rejects wrong return | ✅ |
| 37 | `calculated_receiver_call.obs` | Other | A method call attached to a calculated expression, used inside another calculated expression. `(1... | ✅ |
| 38 | `closure_bare_lambda.obs` | Other | Regression for bare lambdas `\(...) => body` with the return type inferred from context, auto-wra... | ✅ |
| 39 | `closure_block_body.obs` | Other | Regression for lambda block bodies: `\(...) ~ R : () => { ...; return e; }`. Previously a `{ }` l... | ✅ |
| 40 | `closure_direct_call.obs` | Other | Regression for direct FuncRef callability `v()` and functional-call result chaining `v()->Method(... | ✅ |
| 41 | `closure_multi_capture.obs` | Other | Regression for multiple capturing lambdas in one class. Each lambda's captured variables live in... | ✅ |
| 42 | `collect_compare_vector.obs` | Collections | collect compare vector | ✅ |
| 43 | `collect_each_patterns.obs` | Collections | collect each patterns | ✅ |
| 44 | `collect_hash_ops.obs` | Collections | collect hash ops | ✅ |
| 45 | `collect_map_ops.obs` | Collections | collect map ops | ✅ |
| 46 | `collect_nested_ops.obs` | Collections | collect nested ops | ✅ |
| 47 | `collect_pair_ops.obs` | Collections | collect pair ops | ✅ |
| 48 | `collect_queue_ops.obs` | Collections | collect queue ops | ✅ |
| 49 | `collect_set_ops.obs` | Collections | collect set ops | ✅ |
| 50 | `collect_stack_ops.obs` | Collections | collect stack ops | ✅ |
| 51 | `collect_vector_ops.obs` | Collections | collect vector ops | ✅ |
| 52 | `core_abstract_virtual.obs` | Core Language | core abstract virtual | ✅ |
| 53 | `core_arithmetic.obs` | Core Language | Core Arithmetic Operations Test Tests basic arithmetic operations, type conversions, and operator... | ✅ |
| 54 | `core_array_operations.obs` | Core Language | core array operations | ✅ |
| 55 | `core_arrays_simple.obs` | Core Language | Core Array Operations Test (Simplified) Tests basic array creation, access, and modification | ✅ |
| 56 | `core_bitwise_ops.obs` | Core Language | core bitwise ops | ✅ |
| 57 | `core_bool_ops.obs` | Core Language | core bool ops | ✅ |
| 58 | `core_break_continue.obs` | Core Language | core break continue | ✅ |
| 59 | `core_char_methods.obs` | Core Language | core char methods | ✅ |
| 60 | `core_classes.obs` | Core Language | Core Classes Test Tests class instantiation, inheritance, method calls, and select statements Bas... | ✅ |
| 61 | `core_collections_perf.obs` | Core Language | core collections perf | ✅ |
| 62 | `core_control_flow.obs` | Core Language | Core Control Flow Test Tests if/else, loops, and select statements | ✅ |
| 63 | `core_do_while.obs` | Core Language | core do while | ✅ |
| 64 | `core_each_loop.obs` | Core Language | core each loop | ✅ |
| 65 | `core_enum.obs` | Core Language | core enum | ✅ |
| 66 | `core_function_refs.obs` | Core Language | core function refs | ✅ |
| 67 | `core_generic_compound_bounds.obs` | Generics | Compound generic bounds (T : A & B): a concrete type argument must satisfy ALL bounds. Person imp... | ✅ |
| 68 | `core_generic_fbound.obs` | Generics | F-bounded type-parameter constraint (T : Compare<T>): the bound may be generic and self-referenti... | ✅ |
| 69 | `core_generic_structural.obs` | Generics | Exercises the structural generic type comparison: deeply nested generic type arguments must round... | ✅ |
| 70 | `core_generic_variance.obs` | Generics | Declaration-site variance: 'out T' (covariant) lets Producer<Dog> be used where Producer<Animal>... | ✅ |
| 71 | `core_http_server.obs` | Core Language | HTTP Client/Server Loopback Test Tests HTTP GET and POST using raw TCP server + HttpClient. Verif... | ✅ |
| 72 | `core_inheritance_chain.obs` | Core Language | core inheritance chain | ✅ |
| 73 | `core_int_methods.obs` | Core Language | core int methods | ✅ |
| 74 | `core_interfaces.obs` | Core Language | core interfaces | ✅ |
| 75 | `core_json_escape.obs` | Core Language | core json escape | ✅ |
| 76 | `core_method_overload.obs` | Core Language | core method overload | ✅ |
| 77 | `core_multi_dim_array.obs` | Core Language | core multi dim array | ✅ |
| 78 | `core_net_buffer.obs` | Core Language | Network Buffer Read Test Tests that TCP socket ReadBuffer correctly handles partial reads by veri... | ✅ |
| 79 | `core_odbc.obs` | Core Language | Core ODBC Bindings Test Tests Date, Timestamp, and ColumnInfo classes without requiring a databas... | ✅ |
| 80 | `core_opencv.obs` | Core Language | Core OpenCV Bindings Test Tests helper classes, constants, and VideoWriter FourCC without requiri... | ✅ |
| 81 | `core_paren_method_chain.obs` | Core Language | Verifies a method call on a parenthesized method-call expression chains onto the parenthesized re... | ✅ |
| 82 | `core_records.obs` | Core Language | Core Records Test Exercises record-generated constructors, accessors, mutators, generics, readonl... | ✅ |
| 83 | `core_recursion.obs` | Core Language | Core Recursion Test Tests recursive function calls and tail recursion | ✅ |
| 84 | `core_select_ops.obs` | Core Language | core select ops | ✅ |
| 85 | `core_static_array_literals.obs` | Core Language | Regression test for the static-array literal pool (compiler bug, 2026-06): the bool literal-pool... | ✅ |
| 86 | `core_static_fields.obs` | Core Language | core static fields | ✅ |
| 87 | `core_string_format.obs` | Core Language | core string format | ✅ |
| 88 | `core_string_interp_expr.obs` | Core Language | Verifies operator expressions inside "{$...}" string interpolation. | ✅ |
| 89 | `core_string_interp_format.obs` | Core Language | Verifies inline format specifiers "{$expr:spec}" in string interpolation. | ✅ |
| 90 | `core_string_methods.obs` | Core Language | core string methods | ✅ |
| 91 | `core_strings_simple.obs` | Core Language | Core String Operations Test (Simplified) Tests basic string operations without complex method cha... | ✅ |
| 92 | `core_thread_gc_stress.obs` | Concurrency/GC | Multithreaded GC stop-the-world stress test. Guards the GC bugs fixed on branch fix/gc-stop-the-w... | ✅ |
| 93 | `core_type_checking.obs` | Core Language | core type checking | ✅ |
| 94 | `dap_databreak_test.obs` | Debugger | Fixture for dap_databreak_test.py. Stops once with everything initialized, then mutates two local... | ✅ |
| 95 | `dap_drilldown_test.obs` | Debugger | dap drilldown test | ✅ |
| 96 | `dap_exception_test.obs` | Debugger (neg) | Triggers an uncaught runtime error (Nil dereference) so the DAP test suite can verify exception b... | ✅ |
| 97 | `date_arithmetic.obs` | Date/Time | date arithmetic | ✅ |
| 98 | `date_basic_ops.obs` | Date/Time | date basic ops | ✅ |
| 99 | `debugger_coll_test.obs` | Debugger | debugger coll test | ✅ |
| 100 | `debugger_test.obs` | Debugger | debugger test | ✅ |
| 101 | `fix524_array_cast_chain.obs` | Bug Fix | Fix #524: Cannot chain method calls on array-indexed elements after cast Tests that Get(index)->A... | ✅ |
| 102 | `fix534_substring_crash.obs` | Bug Fix | Fix #534: String->SubString crash on negative or zero length argument Tests that negative or zero... | ✅ |
| 103 | `fix_array_bounds.obs` | Bug Fix | fix array bounds | ✅ |
| 104 | `fix_chained_calls.obs` | Bug Fix | fix chained calls | ✅ |
| 105 | `fix_deep_recursion.obs` | Bug Fix | fix deep recursion | ✅ |
| 106 | `fix_float_precision.obs` | Bug Fix | fix float precision | ✅ |
| 107 | `fix_int_boundary.obs` | Bug Fix | fix int boundary | ✅ |
| 108 | `fix_large_arrays.obs` | Bug Fix | fix large arrays | ✅ |
| 109 | `fix_nested_generics.obs` | Bug Fix | fix nested generics | ✅ |
| 110 | `fix_nil_chain_ops.obs` | Bug Fix | fix nil chain ops | ✅ |
| 111 | `fix_polymorphic_calls.obs` | Bug Fix | fix polymorphic calls | ✅ |
| 112 | `fix_scope_shadowing.obs` | Bug Fix | fix scope shadowing | ✅ |
| 113 | `fix_string_concat.obs` | Bug Fix | fix string concat | ✅ |
| 114 | `func_closure_field.obs` | Functional | func closure field | ✅ |
| 115 | `func_filter_ops.obs` | Functional | func filter ops | ✅ |
| 116 | `func_higher_order.obs` | Functional | func higher order | ✅ |
| 117 | `func_reduce_ops.obs` | Functional | func reduce ops | ✅ |
| 118 | `func_sort_custom.obs` | Functional | func sort custom | ✅ |
| 119 | `http_header_flatten_test.obs` | Other | Request-header flattening (Web.HTTP.HeaderCheck->Flatten). HTTP/2 and HTTP/3 hand the request to... | ✅ |
| 120 | `http_header_validation_test.obs` | Other | Request-header validation (Web.HTTP.HeaderCheck). HttpClient->AddHeader was injectable: HTTP/1.1... | ✅ |
| 121 | `indexed_call_result.obs` | Other | Subscripting the result of a method call: 'GetItems()[0]->Name()'. This was never implemented, an... | ✅ |
| 122 | `interp_float_fastpath.obs` | Other | Exercises the interpreter's inlined float fast-path (ADD/SUB/MUL_FLOAT and the six float comparis... | ✅ |
| 123 | `io_file_basic.obs` | I/O | io file basic | ✅ |
| 124 | `jit_array_native.obs` | AMD64/JIT | jit array native | ✅ |
| 125 | `jit_autojit_race.obs` | AMD64/JIT | Auto-JIT concurrency guard. Many threads call the same hot method, crossing the auto-JIT threshol... | ✅ |
| 126 | `jit_closure_gc_fixup.obs` | AMD64/JIT | Regression for the generational-GC fixup of closure captures (bug B1). The GC mark phase descends... | ✅ |
| 127 | `jit_concurrent_compile.obs` | AMD64/JIT | Concurrency guard for the JIT code-page allocator (PageManager::GetPage). Several threads JIT-com... | ✅ |
| 128 | `jit_conditional_native.obs` | AMD64/JIT | jit conditional native | ✅ |
| 129 | `jit_dispatch_native.obs` | AMD64/JIT | jit dispatch native | ✅ |
| 130 | `jit_float_equality.obs` | AMD64/JIT | Regression test for float equality compares on array elements (2026-06). The front-end chose EQL_... | ✅ |
| 131 | `jit_float_intensive.obs` | AMD64/JIT | jit float intensive | ✅ |
| 132 | `jit_float_mem_ops.obs` | AMD64/JIT | Float arithmetic and comparison against MEMORY operands, under the JIT. IMPORTANT: must run with... | ✅ |
| 133 | `jit_float_round_trig.obs` | AMD64/JIT | Exercises two JIT float-codegen bugs that only surface once a method using them is auto-JIT'd (de... | ✅ |
| 134 | `jit_frame_trap_test.obs` | AMD64/JIT | Regression test for the JIT frame-dependent trap crash (2026-06). Traps such as SERL_INT/SERL_FLO... | ✅ |
| 135 | `jit_func_ref_hot.obs` | AMD64/JIT | jit func ref hot | ✅ |
| 136 | `jit_gc_stress.obs` | AMD64/JIT | JIT + GC interaction stress (2026-06). One CI run on linux-x64 failed with a JIT-to-JIT runtime e... | ✅ |
| 137 | `jit_loop_native.obs` | AMD64/JIT | jit loop native | ✅ |
| 138 | `jit_native_cls_fields.obs` | AMD64/JIT | JIT Native Class Fields Test Tests object reference storage in class instance fields with GC pres... | ✅ |
| 139 | `jit_native_float_array.obs` | AMD64/JIT | JIT Native Float Array Test Tests native function with float array creation and math operations R... | ✅ |
| 140 | `jit_native_func_ref.obs` | AMD64/JIT | JIT Native Function Reference Test Tests native functions with function reference storage in clas... | ✅ |
| 141 | `jit_native_math.obs` | AMD64/JIT | JIT Native Math Builtins Test Tests native math functions: Factorial, Sinh/Cosh/Tanh/Log2/Cbrt, P... | ✅ |
| 142 | `jit_string_ops.obs` | AMD64/JIT | jit string ops | ✅ |
| 143 | `jit_tco_bare_local.obs` | AMD64/JIT | Regression for the TCO deferred-local-load miscompile (both arches). A self-recursive tail call t... | ✅ |
| 144 | `json_build_ops.obs` | JSON | json build ops | ✅ |
| 145 | `json_parse_ops.obs` | JSON | json parse ops | ✅ |
| 146 | `lsp_features.obs` | LSP | lsp features | ✅ |
| 147 | `math_float_ops.obs` | Math | math float ops | ✅ |
| 148 | `math_log_exp.obs` | Math | math log exp | ✅ |
| 149 | `math_random_ops.obs` | Math | math random ops | ✅ |
| 150 | `math_rounding.obs` | Math | math rounding | ✅ |
| 151 | `math_sqrt_ops.obs` | Math | math sqrt ops | ✅ |
| 152 | `math_trig_funcs.obs` | Math | math trig funcs | ✅ |
| 153 | `mcp_debug_test.obs` | MCP Server | DEBUG VERSION of mcp_server_test.obs Identical to programs/regression/mcp_server_test.obs except:... | ✅ |
| 154 | `mcp_server_test.obs` | MCP Server | mcp server test | ✅ |
| 155 | `minor_gc_stress.obs` | Other | Regression for generational MINOR GC: old objects holding young references. 'keep' is an object a... | ✅ |
| 156 | `ml_adaboost_test.obs` | System.ML | Regression tests for System.ML AdaBoost (overhaul phase 3): boosting over boolean decision stumps... | ✅ |
| 157 | `ml_api_test.obs` | System.ML | Regression tests for the System.ML estimator API consistency sweep (item 11): RandomForest Fit (r... | ✅ |
| 158 | `ml_dbscan_test.obs` | System.ML | Regression tests for System.ML DBSCAN (overhaul phase 3): two dense blobs plus far-away outliers... | ✅ |
| 159 | `ml_gbt_test.obs` | System.ML | Regression tests for System.ML gradient boosting (overhaul phase 3 leftover): a RegressionTree le... | ✅ |
| 160 | `ml_gmm_test.obs` | System.ML | Regression tests for System.ML GaussianMixture (overhaul phase 3): EM on two well-separated blobs... | ✅ |
| 161 | `ml_kdtree_test.obs` | System.ML | Regression tests for System.ML KDTree (overhaul phase 3): for several queries and k values over a... | ✅ |
| 162 | `ml_library_test.obs` | System.ML | ml library test | ✅ |
| 163 | `ml_linearclf_test.obs` | System.ML | Regression tests for the System.ML linear classifiers (overhaul phase 2): Perceptron (mistake-dri... | ✅ |
| 164 | `ml_nn_test.obs` | System.ML | Regression tests for the System.ML NeuralNetwork with hidden/output bias vectors (ML overhaul ite... | ✅ |
| 165 | `ml_pca_gnb_test.obs` | System.ML | Regression tests for System.ML PCA (power-iteration decomposition: dominant diagonal direction re... | ✅ |
| 166 | `ml_phase1_test.obs` | System.ML | Regression tests for the System.ML correctness fixes (phase 1): seedable PRNG, DotSigmoid dimensi... | ✅ |
| 167 | `ml_regularized_test.obs` | System.ML | Regression tests for the System.ML regularized linear models (overhaul phase 2): RidgeRegression... | ✅ |
| 168 | `ml_trees_test.obs` | System.ML | Regression tests for the System.ML tree models: the real recursive DecisionTree (left/right child... | ✅ |
| 169 | `nil_safe_ops.obs` | Core Language | Nil-safe operators: '??' (nil-coalesce) and '?->' (nil-safe call). Both desugar onto existing int... | ✅ |
| 170 | `oauth_test.obs` | Networking | oauth test | ✅ |
| 171 | `odbc_sqlite_test.obs` | ODBC | ODBC SQLite Integration Test Tests live database operations against an in-memory SQLite database.... | ✅ |
| 172 | `primitive_receiver_order.obs` | Other | Argument order for instance-style calls on primitives. Writing `v->Pow(10)` on a primitive does n... | ✅ |
| 173 | `regex_bench.obs` | Regex | regex bench | ✅ |
| 174 | `regex_dfa_test.obs` | Regex | regex dfa test | ✅ |
| 175 | `runtime_feature_test.obs` | Other | Regression tests for the "runtime.feature.*" properties, which report which optional protocol eng... | ✅ |
| 176 | `select_dispatch_test.obs` | Control Flow | Single-case, linear (2-5 cases), jump-table (dense >=6), and binary-tree (sparse) paths | ✅ |
| 177 | `string_find_ops.obs` | Strings | string find ops | ✅ |
| 178 | `string_format_ops.obs` | Strings | Verifies String->Format() positional substitution. | ✅ |
| 179 | `string_number_conv.obs` | Strings | string number conv | ✅ |
| 180 | `string_replace_ops.obs` | Strings | string replace ops | ✅ |
| 181 | `string_split_ops.obs` | Strings | string split ops | ✅ |
| 182 | `task_scope.obs` | Other | Regression for a structured-concurrency nursery (TaskScope) built purely on the existing System.C... | ✅ |
| 183 | `tco_receiver.obs` | Other | Tail-call optimization must respect the receiver. TCO used to fire on matching class-id and metho... | ✅ |
| 184 | `try_otherwise.obs` | Exceptions | Try/Otherwise Error Handling Test Tests the Try() and Otherwise() intrinsic methods for error han... | ✅ |
| 185 | `unsigned_literals.obs` | Other | Unsigned integer literals: the 'u'/'U' suffix, and hex/binary read as bit patterns. The suffix ch... | ✅ |
| 186 | `unsigned_ops.obs` | Other | The '>>>' operator and the unsigned helpers on Int. Objeck stores every integer in a signed 64-bi... | ✅ |
| 187 | `websocket_test.obs` | Networking | websocket test | ✅ |
| 188 | `xml_build_ops.obs` | XML | xml build ops | ✅ |
| 189 | `xml_encoding_ops.obs` | XML | Unit tests for the 2026-06 Data.XML improvements: truncated/garbage input is rejected (previously... | ✅ |
| 190 | `xml_parse_ops.obs` | XML | xml parse ops | ✅ |

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
