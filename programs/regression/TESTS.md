# Regression Test Manifest

This document inventories every test in `programs/regression/`. The suite runs via
`run_regression.sh` (Linux/macOS) or `run_regression.cmd` (Windows) and in CI on
every push; each test compiles with `obc` and runs with `obr`. Tests marked
**(neg)** expect a compile or runtime error (`# EXPECT_COMPILE_ERROR: <message>` /
`# EXPECT_RUNTIME_ERROR` at the start of a line) and pass when that error is
produced; a compile-error test also needs `<message>` in the compiler output.

This file is generated — regenerate it after adding or removing tests with:

```
python gen_manifest.py
```


**Total runtime tests: 748** (plus 14 debugger tests, see below).


## Tests by Category

| Category | Count |
|----------|-------|
| Other | 304 |
| Negative | 97 |
| System.ML | 57 |
| Core Language | 53 |
| Query.Structured | 49 |
| AMD64/JIT | 44 |
| Collections | 14 |
| Bug Fix | 13 |
| Strings | 12 |
| Math | 9 |
| Debugger | 8 |
| Exceptions | 8 |
| JSON | 8 |
| System.NLP | 8 |
| AMD64/JIT (neg) | 7 |
| ARM64 JIT | 5 |
| Functional | 5 |
| Generics (neg) | 5 |
| I/O | 5 |
| MCP Server | 5 |
| Generics | 4 |
| System.AI | 4 |
| XML | 4 |
| API (network) | 3 |
| Date/Time | 3 |
| ODBC | 3 |
| Regex | 3 |
| Networking | 2 |
| Other (neg) | 2 |
| Concurrency/GC | 1 |
| Control Flow | 1 |
| Debugger (neg) | 1 |
| LSP | 1 |

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
| 10 | `arm64_imm64_ops.obs` | ARM64 JIT | 64-bit immediate operands in the ARM64 JIT. Windows is LLP64, so 'long' is 32 bits there while Li... | ✅ |
| 11 | `arm64_large_immediates.obs` | ARM64 JIT | ARM64 Large Immediate Test Tests add_imm_reg/sub_imm_reg with values > 4095 | ✅ |
| 12 | `arm64_multiply_constants.obs` | ARM64 JIT | ARM64 Multiply-by-Constant Test Tests multiply-by-constant optimization (especially multiply-by-6) | ✅ |
| 13 | `array_literal_dims.obs` | Other | Multi-dimensional array literals have the shape they are written in. A String literal's dimension... | ✅ |
| 14 | `array_literal_expressions.obs` | Other | Array literals with expressions (#1245; docs/ARRAY_LITERALS_DESIGN.md). A literal used to accept... | ✅ |
| 15 | `array_receiver_order.obs` | Other | A call written on a whole array, 'a->Fill(7)', is a static function on the element type's hidden... | ✅ |
| 16 | `bad_ambiguous_bundle_class_enum.obs` | Negative | Two classes of one bundle each declare 'enum Mood'; a third class of the bundle writes 'Mood'. It... | ✅ |
| 17 | `bad_ambiguous_use_alias.obs` | Negative | Two bundles in scope both declare 'alias Funcs'. An alias was named without its bundle, so the tw... | ✅ |
| 18 | `bad_ambiguous_use_alias_member.obs` | Negative | An alias's member type names a class two used bundles both declare. The name was copied to each p... | ✅ |
| 19 | `bad_ambiguous_use_class.obs` | Negative | Two bundles in scope both declare 'class Shape'. The unqualified name silently resolved to whiche... | ✅ |
| 20 | `bad_ambiguous_use_enum.obs` | Negative | Two bundles in scope both declare 'enum Tint'. The unqualified name silently resolved to whicheve... | ✅ |
| 21 | `bad_ambiguous_use_lib_program.obs` | Negative | A program bundle and a library bundle, both 'use'd, each declare 'JsonElement'. The program's cla... | ✅ |
| 22 | `bad_ambiguous_use_parent.obs` | Negative | A class inherits 'from Root' while two bundles in scope both declare 'class Root'. The parent sil... | ✅ |
| 23 | `bad_ambiguous_use_row_type.obs` | Negative | A jagged array's row type (#1246) on a class two used bundles both declare: 'rows : Shape[][]' to... | ✅ |
| 24 | `bad_array_literal_2d_expressions.obs` | Negative | Array literals with expressions (#1245): expressions are one-dimensional for now; ragged rows are... | ✅ |
| 25 | `bad_array_literal_enum_mix.obs` | Negative | An enum value is an Int at run time. Mixed with a primitive in an array literal, it was taken for... | ✅ |
| 26 | `bad_array_literal_mixed_primitives.obs` | Negative | Array literals with expressions (#1245): an Int and a Float in one literal: there is no implicit... | ✅ |
| 27 | `bad_array_literal_nil_element.obs` | Negative | Array literals with expressions (#1245): Nil takes its type from the literal's other elements (co... | ✅ |
| 28 | `bad_array_narrowing.obs` | Negative | An array of a class serves as an array of its parent or an interface it implements (core_array_in... | ✅ |
| 29 | `bad_array_too_many_dimensions.obs` | Negative | rejects array too many dimensions | ✅ |
| 30 | `bad_call_undefined.obs` | Negative | rejects call undefined | ✅ |
| 31 | `bad_class_enum_other_bundle.obs` | Negative | A class's enum is in scope in its class, the class's children and the other classes of its bundle... | ✅ |
| 32 | `bad_cond_expr_dimension_mismatch.obs` | Negative | Guards #867's companion check. Once a conditional keeps its branches' dimension, an array branch... | ✅ |
| 33 | `bad_constructor_missing.obs` | Negative | 'Shapeless->New()' on a class with no constructor at all resolved to System.Base's New: it built... | ✅ |
| 34 | `bad_constructor_missing_library.obs` | Negative | A library class with no constructor, System.Runtime, resolved 'Runtime->New()' to System.Base's N... | ✅ |
| 35 | `bad_constructor_not_inherited.obs` | Negative | A constructor is not inherited. 'Kid->New()', on a class that declares no 'New' of its own, walke... | ✅ |
| 36 | `bad_curried_call_not_function.obs` | Negative | `Foo(2)(5)` is a curried call (#1184): it calls the value Foo(2) returns. Foo returns an Int, so... | ✅ |
| 37 | `bad_div_zero_folded.obs` | Negative | A constant division by zero must still trap at runtime, at every optimization level. The constant... | ✅ |
| 38 | `bad_duplicate_class.obs` | Negative | rejects duplicate class | ✅ |
| 39 | `bad_error_raise_unhandled.obs` | Negative | Runtime->Raise with no ?-> region to recover from (#1247): the program reports "Unhandled <class>... | ✅ |
| 40 | `bad_field_typo_compound_assign.obs` | Negative | A misspelt field in a compound assignment is an error (#1184). Any unknown name used to become a... | ✅ |
| 41 | `bad_float_div_zero_dead_store.obs` | Negative | A Float division by zero must raise even when its result is never read (#1178). Dead-store elimin... | ✅ |
| 42 | `bad_float_div_zero_folded.obs` | Negative | A constant Float division by zero must raise at every optimization level (#1178). The VM raises "... | ✅ |
| 43 | `bad_float_literal_negative_overflow.obs` | Negative | A negated float literal just past the largest double is rejected too; it compiled silently as neg... | ✅ |
| 44 | `bad_float_literal_overflow.obs` | Negative | A float literal past the largest double must be rejected. It was parsed with wcstod, whose range... | ✅ |
| 45 | `bad_float_literal_underflow.obs` | Negative | A nonzero float literal below the smallest subnormal double rounds to zero and must be rejected r... | ✅ |
| 46 | `bad_float_literal_underflow_edge.obs` | Negative | 2.4e-324 is below half the smallest subnormal double (4.9e-324), so it rounds to zero and is reje... | ✅ |
| 47 | `bad_generic_arg_mismatch.obs` | Generics (neg) | Guards the generic type-argument compatibility check (and its readable diagnostic). Assigning a V... | ✅ |
| 48 | `bad_generic_array_type_argument.obs` | Generics (neg) | 'Vector->New()<String[]>' was accepted at the declaration, and then every AddBack(String[]) faile... | ✅ |
| 49 | `bad_generic_compound_bound.obs` | Generics (neg) | A concrete type argument that satisfies only one of a compound bound (T : A & B) must be rejected... | ✅ |
| 50 | `bad_generic_fbound.obs` | Generics (neg) | A concrete argument that does not satisfy an F-bounded constraint (T : Compare<T>) must be reject... | ✅ |
| 51 | `bad_generic_variance.obs` | Generics (neg) | Variance must stay sound: a covariant 'out T' does NOT allow the reverse direction. Producer<Anim... | ✅ |
| 52 | `bad_inherit_unknown.obs` | Negative | rejects inherit unknown | ✅ |
| 53 | `bad_inherited_library_instance_call_lambda.obs` | Negative | A lambda does not capture self (bad_lambda_implicit_self.obs), so inside one an unqualified call... | ✅ |
| 54 | `bad_inherited_library_instance_call_static.obs` | Negative | A function has no instance, so an unqualified call of an instance method the class inherits from... | ✅ |
| 55 | `bad_int64_min_unnegated.obs` | Negative | 9223372036854775808 is Int's minimum only when negated (int64_min_literal). On its own, or as the... | ✅ |
| 56 | `bad_int_literal_negative_range.obs` | Negative | A negated integer literal past Int's range names the problem, with its sign. The negation path ha... | ✅ |
| 57 | `bad_int_literal_overflow.obs` | Negative | An integer literal past the signed range must be rejected. It used to be parsed with a saturating... | ✅ |
| 58 | `bad_int_literal_range.obs` | Negative | An integer literal too large for Int names the problem (#1184); it was reported only as "unexpect... | ✅ |
| 59 | `bad_jagged_mixed_rows.obs` | Negative | Jagged arrays (#1246): an Int row and a String row in one literal. | ✅ |
| 60 | `bad_jagged_row_type.obs` | Negative | Jagged arrays (#1246): a Float[] row in an Int[][]. | ✅ |
| 61 | `bad_jagged_second_subscript.obs` | Negative | Jagged arrays (#1246): a second subscript on an Int[]. | ✅ |
| 62 | `bad_jagged_too_many_subscripts.obs` | Negative | Jagged arrays (#1246): a third subscript on an Int[][]. | ✅ |
| 63 | `bad_keyword_as_var.obs` | Negative | rejects keyword as var | ✅ |
| 64 | `bad_lambda_implicit_self.obs` | Negative | Guards the boundary of captured method calls (v2026.10.0 C3). A lambda body may call an instance... | ✅ |
| 65 | `bad_lambda_in_interpolation.obs` | Negative | A lambda inside a {$ } interpolation used to be reported as "Invalid escaped string literal" (the... | ✅ |
| 66 | `bad_lambda_multi_arg.obs` | Negative | A bare lambda is only inferred when it is a call's sole argument. As one of several arguments the... | ✅ |
| 67 | `bad_lambda_ternary.obs` | Negative | A bare lambda `\() => body` gets its type from a FuncRef<R> assignment target, return type or sin... | ✅ |
| 68 | `bad_leading_zero_nine.obs` | Negative | '09' has no octal reading. It was an unknown token, reported as an unexpected token with follow-o... | ✅ |
| 69 | `bad_leading_zero_octal.obs` | Negative | A decimal-looking literal with a leading zero was read as octal, so '010' compiled silently as 8.... | ✅ |
| 70 | `bad_local_typo_compound_assign.obs` | Negative | Only `name := value` declares a local (#1184). The target of +=, -= and the like must already exi... | ✅ |
| 71 | `bad_method_generic_bound.obs` | Negative | Method generics (#1242): an argument that does not meet the bound. | ✅ |
| 72 | `bad_method_generic_call.obs` | Negative | A method's own type parameter (#1242) is not a class. 'T->New()' in 'Id<T>' named whatever class... | ✅ |
| 73 | `bad_method_generic_conflict.obs` | Negative | Method generics (#1242): two arguments that bind T to different classes. | ✅ |
| 74 | `bad_method_generic_library_bound.obs` | Negative | Method-level generics in libraries (#1242, phase 2): CompareList's Map<R : Compare> is checked ag... | ✅ |
| 75 | `bad_method_generic_primitive.obs` | Negative | Method generics (#1242): a primitive argument: a type parameter takes an object, as class generic... | ✅ |
| 76 | `bad_mod_zero.obs` | Negative | Modulus by zero must trap like division by zero does. The interpreter's ModInt had no zero check... | ✅ |
| 77 | `bad_named_argument_function_value.obs` | Negative | Named arguments (#1244): a function value: a function type has no parameter names. | ✅ |
| 78 | `bad_named_argument_library.obs` | Negative | Named arguments (#1244): a name a library method does not have. The error lists the parameter nam... | ✅ |
| 79 | `bad_named_argument_missing_required.obs` | Negative | Named arguments (#1244): skipping a parameter that has no default. | ✅ |
| 80 | `bad_named_argument_positional_after.obs` | Negative | Named arguments (#1244): a positional argument after a named one. | ✅ |
| 81 | `bad_named_argument_primitive.obs` | Negative | Named arguments (#1244): a call on a primitive value is a static call with the value as a hidden... | ✅ |
| 82 | `bad_named_argument_twice.obs` | Negative | Named arguments (#1244): one parameter given by position and again by name. | ✅ |
| 83 | `bad_named_argument_unknown.obs` | Negative | Named arguments (#1244): a parameter name the method does not have; the error lists the ones it has. | ✅ |
| 84 | `bad_new_negative_array.obs` | Negative | A negative array size must stop the VM in every mode (#1181). The JIT wrapped the count to 0 and... | ✅ |
| 85 | `bad_new_negative_dim.obs` | Negative | A negative dimension beside a zero one must stop the VM too (#1181): the product is 0, so the siz... | ✅ |
| 86 | `bad_nil_array_argument_ambiguous.obs` | Negative | A literal Nil matches an array parameter as it matches an object one (core_nil_array_argument.obs... | ✅ |
| 87 | `bad_numeric_literal_in_array.obs` | Negative | An out-of-range literal inside an array literal is the only error. Recovery used to skip to the n... | ✅ |
| 88 | `bad_numeric_literal_mid_expression.obs` | Negative | An out-of-range literal in the middle of an expression is the only error. Recovery used to skip t... | ✅ |
| 89 | `bad_primitive_receiver_arity.obs` | Negative | A call on a primitive passes the receiver as parameter 0 of a function on its $ class, so `255->T... | ✅ |
| 90 | `bad_readonly_record_assignment.obs` | Negative | rejects readonly record assignment | ✅ |
| 91 | `bad_readonly_record_op_assignment.obs` | Negative | rejects readonly record op assignment | ✅ |
| 92 | `bad_ref_argument_expression.obs` | Negative | Reference parameters (#1243): an array element passed as 'ref'; the copy back would evaluate its... | ✅ |
| 93 | `bad_ref_array_param.obs` | Negative | Reference parameters (#1243): a 'ref' parameter of an array type; an array is already shared. | ✅ |
| 94 | `bad_ref_default.obs` | Negative | Reference parameters (#1243): a 'ref' parameter has a default; the caller's cell is its value. | ✅ |
| 95 | `bad_ref_in_condition.obs` | Negative | Reference parameters (#1243): a call with a 'ref' argument in an 'if' condition; its copy back wo... | ✅ |
| 96 | `bad_ref_in_return.obs` | Negative | Reference parameters (#1243): a call with a 'ref' argument in a return; its copy back would never... | ✅ |
| 97 | `bad_runtime_bounds.obs` | Negative | VM must not crash (segfault) on array out-of-bounds access — exit 1 with message | ✅ |
| 98 | `bad_runtime_divzero.obs` | Negative | VM must not crash (segfault) on integer division by zero — exit 1 with message | ✅ |
| 99 | `bad_runtime_null.obs` | Negative | VM must not crash (segfault) when dereferencing a nil object — exit 1 with message | ✅ |
| 100 | `bad_runtime_stack.obs` | Negative | VM must not crash (segfault) on call stack overflow — exit 1 with message. NOTE: the recursion MU... | ✅ |
| 101 | `bad_statements_same_line_assign.obs` | Negative | `x := 1 Foo(3);` compiled as an assignment and a call (#1184). Two statements on one line need a... | ✅ |
| 102 | `bad_statements_same_line_call.obs` | Negative | Semicolons between statements are optional, so `Foo(2) Foo(3);` compiled as two calls without a w... | ✅ |
| 103 | `bad_string_interp_format.obs` | Negative | An unrecognized interpolation format specifier must be rejected at compile time. | ✅ |
| 104 | `bad_string_interp_format_range.obs` | Negative | An interpolation width or precision too large for an int is an invalid format specifier, reported... | ✅ |
| 105 | `bad_syntax_unclosed.obs` | Negative | rejects syntax unclosed | ✅ |
| 106 | `bad_try_empty.obs` | Negative | try (#1247): a try with neither handles nothing. | ✅ |
| 107 | `bad_try_finally_break.obs` | Negative | try (#1247): a break out of a finally block would drop the error it runs for. | ✅ |
| 108 | `bad_try_finally_return.obs` | Negative | try (#1247): a return out of a finally block would drop the error it runs for. | ✅ |
| 109 | `bad_try_on_not_error.obs` | Negative | try (#1247): an 'on' clause names a class that is not an Error. | ✅ |
| 110 | `bad_try_on_unreachable.obs` | Negative | try (#1247): a clause for a subclass after one for its parent can never run. | ✅ |
| 111 | `bad_unassigned_function_value.obs` | Negative | Calling a function variable that was never assigned is a Nil dereference (#1183). It used to run... | ✅ |
| 112 | `bad_undefined_type.obs` | Negative | rejects undefined type | ✅ |
| 113 | `bad_undefined_var.obs` | Negative | A read of a variable that was never declared must be rejected with the undefined-variable diagnos... | ✅ |
| 114 | `bad_undefined_variable_read.obs` | Negative | Reading a name that was never declared (#1184). Inside a method an unknown name silently became a... | ✅ |
| 115 | `bad_unsigned_literal_suffix.obs` | Negative | The 'u' suffix reads a literal across the unsigned range, so a value past 2^64-1 has no represent... | ✅ |
| 116 | `bad_unsigned_suffix_on_float.obs` | Negative | The unsigned suffix is meaningless on a floating-point literal and must not be accepted silently. | ✅ |
| 117 | `bad_wrong_return.obs` | Negative | rejects wrong return | ✅ |
| 118 | `base_tostring.obs` | Other | System.Base->ToString (#1248, from #1186). Every object has a ToString now. A Stringify instance... | ✅ |
| 119 | `bool_array_call_index.obs` | Other | Subscripting a Bool[] returned from a call (#988). A Bool[] is allocated as NEW_BYTE_ARY and a lo... | ✅ |
| 120 | `bool_array_nil_comparison.obs` | Other | Comparing a call that returns Bool[] against Nil. `if(obj->Predict(x) = Nil)` used to fail to com... | ✅ |
| 121 | `byte_array_header_test.obs` | Other | A Byte[] reports one size to Objeck and another to native code. Size() reads header word [2]; nat... | ✅ |
| 122 | `byte_array_hex_string.obs` | Other | Byte[]->ToHexString is a hex digest It returned a bracketed list, "[0x76,0x4f,...]", while the ex... | ✅ |
| 123 | `byte_narrowing.obs` | Other | Regression: a scalar Byte wraps to 8 bits (#1176). A value stored into a Byte local, field or par... | ✅ |
| 124 | `calculated_receiver_call.obs` | Other | A method call attached to a calculated expression, used inside another calculated expression. `(1... | ✅ |
| 125 | `call_indices_nested_link.obs` | Other | A subscript on the result of a LATER link of a call chain: 'cells->Get(r)->Get()[c]'. 'GetItems()... | ✅ |
| 126 | `cast_and_typeof_chains.obs` | Other | Regression: a call chain after As(T), and TypeOf(..) at the end of a chain. Found by the Rosetta... | ✅ |
| 127 | `client_socket_errors.obs` | Other | A client socket that fails says why, and keeps saying it TCPSocket->GetLastError() read the live... | ✅ |
| 128 | `closure_array_param_capture.obs` | Other | Guards #849: a lambda capturing an object array crashed obr (0xC0000005). Inside a lambda a captu... | ✅ |
| 129 | `closure_array_var_index.obs` | Other | A captured array indexed by a variable inside a lambda crashed obc (0xC0000005). Inside a lambda... | ✅ |
| 130 | `closure_bare_lambda.obs` | Other | Regression for bare lambdas `\(...) => body` with the return type inferred from context, auto-wra... | ✅ |
| 131 | `closure_block_body.obs` | Other | Regression for lambda block bodies: `\(...) ~ R : () => { ...; return e; }`. Previously a `{ }` l... | ✅ |
| 132 | `closure_body_call_result.obs` | Other | Regression: a call used as a statement inside a lambda body must discard its result. The context... | ✅ |
| 133 | `closure_capture_calls.obs` | Other | Calls through captured variables inside lambda bodies (v2026.10.0 C3 + C6). C3: an instance metho... | ✅ |
| 134 | `closure_capture_old_holder_g12.obs` | Other | G12: a closure capturing a young object, stored in an object that is already old, must keep the c... | ✅ |
| 135 | `closure_capture_resolution.obs` | Other | Guards #865 and #866: two sites resolved a name inside a lambda body without the lambda's capture... | ✅ |
| 136 | `closure_direct_call.obs` | Other | Regression for direct FuncRef callability `v()` and functional-call result chaining `v()->Method(... | ✅ |
| 137 | `closure_each_captured_array.obs` | Other | Guards the 'each' loop over an array captured by a lambda, which failed to compile with "Cannot r... | ✅ |
| 138 | `closure_enum_array_capture.obs` | Other | Guards enum-typed arrays against being freed while still live (G11). Storing an enum item into an... | ✅ |
| 139 | `closure_lambda_fn_param.obs` | Other | Guards a compiler defect present through v2026.9.4: a typed lambda with a FUNCTION-TYPED paramete... | ✅ |
| 140 | `closure_lambda_many_params.obs` | Other | Regression: lambdas whose parameter list is longer than the scanner's look-ahead buffer. ParseLam... | ✅ |
| 141 | `closure_lambda_params.obs` | Other | Guards two obc crashes (exit 0xC0000005, no diagnostic) present from v2026.6.3 through v2026.9.4:... | ✅ |
| 142 | `closure_multi_capture.obs` | Other | Regression for multiple capturing lambdas in one class. Each lambda's captured variables live in... | ✅ |
| 143 | `closure_nested_capture.obs` | Other | A lambda nested in a lambda could not use a variable its enclosing lambda captured: obc reported... | ✅ |
| 144 | `closure_param_lambda.obs` | Other | Regression: typed lambdas that take parameters, with and without captures. ParseLambda looks past... | ✅ |
| 145 | `collect_compare_vector.obs` | Collections | collect compare vector | ✅ |
| 146 | `collect_each_patterns.obs` | Collections | collect each patterns | ✅ |
| 147 | `collect_hash_heap_sort.obs` | Collections | Collection additions from #1186: Hash->Copy, Merge, Each and Filter; the hash-backed MultiHash; H... | ✅ |
| 148 | `collect_hash_open.obs` | Collections | Collection.Hash on open addressing: every public method, growth past one million entries (the old... | ✅ |
| 149 | `collect_hash_ops.obs` | Collections | collect hash ops | ✅ |
| 150 | `collect_map_ops.obs` | Collections | collect map ops | ✅ |
| 151 | `collect_nested_ops.obs` | Collections | collect nested ops | ✅ |
| 152 | `collect_pair_ops.obs` | Collections | collect pair ops | ✅ |
| 153 | `collect_primitive.obs` | Collections | Primitive collections in Collection: IntVector, FloatVector, IntMap, IntSet and BitSet. Covers em... | ✅ |
| 154 | `collect_queue_ops.obs` | Collections | collect queue ops | ✅ |
| 155 | `collect_set_algebra.obs` | Collections | Set algebra on Collection.Set, HashSet and SetHash, and the method names the three now share (#11... | ✅ |
| 156 | `collect_set_ops.obs` | Collections | collect set ops | ✅ |
| 157 | `collect_stack_ops.obs` | Collections | collect stack ops | ✅ |
| 158 | `collect_vector_ops.obs` | Collections | collect vector ops | ✅ |
| 159 | `compile_alias_bundle_scope.obs` | Other | Regression: a bundle's alias is named with its bundle (bad_ambiguous_use_alias.obs). Checks two b... | ✅ |
| 160 | `compile_ambiguous_use_callee_scope.obs` | Other | Regression: a class's declarations resolve in the class's own bundle, wherever they are checked f... | ✅ |
| 161 | `compile_ambiguous_use_names.obs` | Other | Regression: an unqualified name that two 'use'd bundles both declare is a compile error (bad_ambi... | ✅ |
| 162 | `compile_as_call_compare.obs` | Other | Regression: a cast in a call chain used as a comparison operand. `x->As(Kid)->Only() <> "only"`,... | ✅ |
| 163 | `compile_bundle_class_enum_scope.obs` | Other | Regression: a class's enum is in scope in its class, its children and the other classes of its bu... | ✅ |
| 164 | `compile_bundle_enum_same_name.obs` | Other | Regression: bundle-level enums are named with their bundle, as classes are. A bundle-level enum w... | ✅ |
| 165 | `compile_bundle_qualified_enum.obs` | Other | Regression: bundle-qualified enum names. A bundle-level enum is named without its bundle (a class... | ✅ |
| 166 | `compile_class_scoped_enum_names.obs` | Other | Regression: class-scoped enums with the same name in two classes. The parser adds 'ScopedAa#' as... | ✅ |
| 167 | `compile_constructor_own.obs` | Other | Regression: a constructor is not inherited (bad_constructor_not_inherited.obs), so each class ins... | ✅ |
| 168 | `compile_inherited_library_instance_call.obs` | Other | Regression: an instance method calls an instance method its class inherits from a library class w... | ✅ |
| 169 | `compile_method_generic_shadow.obs` | Other | Regression: a method's own type parameter (#1242) shadows a class of the same name that the file'... | ✅ |
| 170 | `compiler_long_add_chain.obs` | Other | A forty-term addition chain. The compiler's AnalyzeCalculation recursed into each calculated oper... | ✅ |
| 171 | `compress_round_trip.obs` | I/O | Byte[] compression round trips: zlib, gzip and raw deflate on empty, one-byte, short and large in... | ✅ |
| 172 | `concurrent_critical_alloc.obs` | Other | Allocating inside a contended critical section (#1186). Eight threads share one ThreadMutex and a... | ✅ |
| 173 | `concurrent_sync_primitives.obs` | Other | System.Concurrency's blocking primitives (#1186): Condition (lang.obl, new COND_NEW/COND_WAIT/CON... | ✅ |
| 174 | `cond_expr_array_type.obs` | Other | Guards #867: a conditional expression whose branches are primitive arrays was typed as a scalar.... | ✅ |
| 175 | `cond_expr_receiver.obs` | Other | A conditional expression used as a method receiver -- '(t ? "abcdef" : "xy")-> SubString(1, 2)' -... | ✅ |
| 176 | `core_abstract_virtual.obs` | Core Language | core abstract virtual | ✅ |
| 177 | `core_arithmetic.obs` | Core Language | Core Arithmetic Operations Test Tests basic arithmetic operations, type conversions, and operator... | ✅ |
| 178 | `core_array_element_interface_return.obs` | Core Language | Regression: a call on an array element whose result needs a class cast. 'c : Compare := rows[i]->... | ✅ |
| 179 | `core_array_interface_widening.obs` | Core Language | Regression: an array of a class where an array of an interface it implements is wanted. A String[... | ✅ |
| 180 | `core_array_literal_mixed_strings.obs` | Core Language | Regression: an array literal mixing string literals with other expressions. 'v := "xyz"; texts :=... | ✅ |
| 181 | `core_array_literal_nil_element.obs` | Core Language | Regression: Nil as an element of an array literal. 'row := ["a", Nil, "c"];' was refused, "An arr... | ✅ |
| 182 | `core_array_operations.obs` | Core Language | core array operations | ✅ |
| 183 | `core_arrays_simple.obs` | Core Language | Core Array Operations Test (Simplified) Tests basic array creation, access, and modification | ✅ |
| 184 | `core_basic_type_call_statement.obs` | Core Language | A function of a basic type's class called as a statement: 'Int->Clear(a);'. 'Runtime->Copy(..);'... | ✅ |
| 185 | `core_bitwise_ops.obs` | Core Language | core bitwise ops | ✅ |
| 186 | `core_bool_ops.obs` | Core Language | core bool ops | ✅ |
| 187 | `core_bool_short_circuit.obs` | Core Language | Evaluation order and skipping of `&` and `\|`: left operand first, right only when the left did no... | ✅ |
| 188 | `core_break_continue.obs` | Core Language | core break continue | ✅ |
| 189 | `core_call_depth.obs` | Core Language | Ordinary deep recursion runs: thousands of frames, not 256. The call stack was a fixed array of 2... | ✅ |
| 190 | `core_call_depth_max.obs` | Core Language | The maximum call depth is enforced: a runaway recursion reaches it and stops with "call stack bou... | ✅ |
| 191 | `core_char_methods.obs` | Core Language | core char methods | ✅ |
| 192 | `core_classes.obs` | Core Language | Core Classes Test Tests class instantiation, inheritance, method calls, and select statements Bas... | ✅ |
| 193 | `core_collections_perf.obs` | Core Language | core collections perf | ✅ |
| 194 | `core_control_flow.obs` | Core Language | Core Control Flow Test Tests if/else, loops, and select statements | ✅ |
| 195 | `core_do_while.obs` | Core Language | core do while | ✅ |
| 196 | `core_do_while_post_op.obs` | Core Language | A post-operation in a `do`/`while` condition must run once per iteration. EmitDoWhile chose betwe... | ✅ |
| 197 | `core_each_loop.obs` | Core Language | core each loop | ✅ |
| 198 | `core_enum.obs` | Core Language | core enum | ✅ |
| 199 | `core_enum_interp.obs` | Core Language | Enum and consts values in strings (v2026.10.0 hardening item C4, decision D4). Through v2026.9.4... | ✅ |
| 200 | `core_function_refs.obs` | Core Language | core function refs | ✅ |
| 201 | `core_generic_compound_bounds.obs` | Generics | Compound generic bounds (T : A & B): a concrete type argument must satisfy ALL bounds. Person imp... | ✅ |
| 202 | `core_generic_fbound.obs` | Generics | F-bounded type-parameter constraint (T : Compare<T>): the bound may be generic and self-referenti... | ✅ |
| 203 | `core_generic_structural.obs` | Generics | Exercises the structural generic type comparison: deeply nested generic type arguments must round... | ✅ |
| 204 | `core_generic_variance.obs` | Generics | Declaration-site variance: 'out T' (covariant) lets Producer<Dog> be used where Producer<Animal>... | ✅ |
| 205 | `core_http_server.obs` | Core Language | HTTP Client/Server Loopback Test Tests HTTP GET and POST using raw TCP server + HttpClient. Verif... | ✅ |
| 206 | `core_inheritance_chain.obs` | Core Language | core inheritance chain | ✅ |
| 207 | `core_int_methods.obs` | Core Language | core int methods | ✅ |
| 208 | `core_interfaces.obs` | Core Language | core interfaces | ✅ |
| 209 | `core_interpolate_nil.obs` | Core Language | An interpolated String or object that is Nil reads as "Nil" instead of stopping the program: "x={... | ✅ |
| 210 | `core_json_escape.obs` | Core Language | core json escape | ✅ |
| 211 | `core_method_overload.obs` | Core Language | core method overload | ✅ |
| 212 | `core_multi_dim_array.obs` | Core Language | core multi dim array | ✅ |
| 213 | `core_net_buffer.obs` | Core Language | Network Buffer Read Test Tests that TCP socket ReadBuffer correctly handles partial reads by veri... | ✅ |
| 214 | `core_nil_array_argument.obs` | Core Language | Regression: a literal Nil passed for an array parameter. With 'function : F(a : String[]) ~ Int',... | ✅ |
| 215 | `core_odbc.obs` | Core Language | Core ODBC Value Classes Test Date, Timestamp and ColumnInfo are pure Objeck. Only Connection, Par... | ✅ |
| 216 | `core_opencv.obs` | Core Language | Core OpenCV Bindings Test Tests helper classes, constants, and VideoWriter FourCC without requiri... | ✅ |
| 217 | `core_paren_method_chain.obs` | Core Language | Verifies a method call on a parenthesized method-call expression chains onto the parenthesized re... | ✅ |
| 218 | `core_records.obs` | Core Language | Core Records Test Exercises record-generated constructors, accessors, mutators, generics, readonl... | ✅ |
| 219 | `core_recursion.obs` | Core Language | Core Recursion Test Tests recursive function calls and tail recursion | ✅ |
| 220 | `core_select_ops.obs` | Core Language | core select ops | ✅ |
| 221 | `core_static_array_literals.obs` | Core Language | Regression test for the static-array literal pool (compiler bug, 2026-06): the bool literal-pool... | ✅ |
| 222 | `core_static_fields.obs` | Core Language | core static fields | ✅ |
| 223 | `core_string_escape_esc.obs` | Core Language | String Escape \e Test "\e" in a string literal is ESC (27) on every platform; a Windows-built obc... | ✅ |
| 224 | `core_string_format.obs` | Core Language | core string format | ✅ |
| 225 | `core_string_interp_expr.obs` | Core Language | Verifies operator expressions inside "{$...}" string interpolation. | ✅ |
| 226 | `core_string_interp_format.obs` | Core Language | Verifies inline format specifiers "{$expr:spec}" in string interpolation. | ✅ |
| 227 | `core_string_methods.obs` | Core Language | core string methods | ✅ |
| 228 | `core_strings_simple.obs` | Core Language | Core String Operations Test (Simplified) Tests basic string operations without complex method cha... | ✅ |
| 229 | `core_ternary_call_chain_type.obs` | Core Language | Regression: a '?:' whose first branch is a call chain takes the chain's type. 'return c ? cols->G... | ✅ |
| 230 | `core_thread_gc_stress.obs` | Concurrency/GC | Multithreaded GC stop-the-world stress test. Guards the GC bugs fixed on branch fix/gc-stop-the-w... | ✅ |
| 231 | `core_type_checking.obs` | Core Language | core type checking | ✅ |
| 232 | `critical_exit_unlocks.obs` | Other | Regression: leaving a critical() block by return, break or continue never released its mutex (#11... | ✅ |
| 233 | `csv_median.obs` | Other | CsvRow->Median and CsvColumn->Median return the exact median. Both used to box every value into a... | ✅ |
| 234 | `csv_nil_data.obs` | Other | A CSV file that is not there reports, it does not crash the VM. FileReader->ReadFile returns Nil... | ✅ |
| 235 | `csv_quoting.obs` | Other | Data.CSV reads RFC 4180 quoting and reports malformed rows. CsvTable used to split the text into... | ✅ |
| 236 | `csv_unparsed_table.obs` | Other | An unparsed CsvTable reports, it does not crash the VM. CsvTable->Init only built @header_names w... | ✅ |
| 237 | `curried_calls.obs` | Other | Regression: curried calls (#1184). `F(a)(b)` calls the function value F(a) returns with (b); chai... | ✅ |
| 238 | `dap_databreak_test.obs` | Debugger | Fixture for dap_databreak_test.py. Stops once with everything initialized, then mutates two local... | ✅ |
| 239 | `dap_drilldown_test.obs` | Debugger | dap drilldown test | ✅ |
| 240 | `dap_exception_test.obs` | Debugger (neg) | Triggers an uncaught runtime error (Nil dereference) so the DAP test suite can verify exception b... | ✅ |
| 241 | `dap_frame_eval_test.obs` | Debugger | Fixture for frame-scoped DAP evaluation and hit-count breakpoints. The point of the nesting is th... | ✅ |
| 242 | `dap_setvar_test.obs` | Debugger | Fixture for DAP setVariable through a drill-down handle. A local named 'count' and an object whos... | ✅ |
| 243 | `date_arithmetic.obs` | Date/Time | date arithmetic | ✅ |
| 244 | `date_basic_ops.obs` | Date/Time | date basic ops | ✅ |
| 245 | `date_gmt_calendar.obs` | Date/Time | Regression: System.Time GMT dates were computed as local time (#1179). - Date->New(day, month, ye... | ✅ |
| 246 | `debugger_coll_test.obs` | Debugger | debugger coll test | ✅ |
| 247 | `debugger_eval_test.obs` | Debugger | Fixture for the debugger's expression evaluator and breakpoint bookkeeping. Separate from debugge... | ✅ |
| 248 | `debugger_test.obs` | Debugger | debugger test | ✅ |
| 249 | `debugger_thread_test.obs` | Debugger | Fixture for debugging a program with several live threads. Each worker carries its own id and a l... | ✅ |
| 250 | `diag_concurrent_analysis.obs` | Other | Concurrency guard for the diagnostics/LSP analysis path (#659). The LSP server spawns a worker th... | ✅ |
| 251 | `dir_list_no_dot_entries.obs` | Other | Directory->List returns the same entries on every platform: the files and directories in the dire... | ✅ |
| 252 | `discarded_call_result_pop.obs` | Other | GC_STRESS_SKIP reason: millions of iterations to overflow the operand stack; with --gc-threshold=... | ✅ |
| 253 | `dtls_bind_address.obs` | Other | A DTLS server can listen on one address, and keeps to it DTLSSocketServer->New(cert, key, passwd,... | ✅ |
| 254 | `dtls_socket_test.obs` | Other | DTLSSocket and DTLSSocketServer, end to end Until this test, nothing had ever run a DTLS call. Th... | ✅ |
| 255 | `dual_stack_servers.obs` | Other | Servers are reachable over IPv4 and IPv6 alike A wildcard server should take clients of either fa... | ✅ |
| 256 | `error_raise.obs` | Other | Raising an Error and reading the one a ?-> region recovered from (#1247, phase 1; docs/ERROR_HAND... | ✅ |
| 257 | `exit_with_live_threads.obs` | Other | Ending the process while other threads are still running must exit cleanly. Guards #877. A runtim... | ✅ |
| 258 | `file_io_edge_cases.obs` | Other | Regression: the smaller File IO defects of #1180. - FileReader->ReadFile returned Nil for an empt... | ✅ |
| 259 | `file_io_flush_eof_delete.obs` | Other | Regression: three file defects (#1180). 1. File->Flush() cleared the file handle before flushing... | ✅ |
| 260 | `fix524_array_cast_chain.obs` | Bug Fix | Fix #524: Cannot chain method calls on array-indexed elements after cast Tests that Get(index)->A... | ✅ |
| 261 | `fix534_substring_crash.obs` | Bug Fix | Fix #534: String->SubString crash on negative or zero length argument Tests that a negative lengt... | ✅ |
| 262 | `fix_array_bounds.obs` | Bug Fix | fix array bounds | ✅ |
| 263 | `fix_chained_calls.obs` | Bug Fix | fix chained calls | ✅ |
| 264 | `fix_deep_recursion.obs` | Bug Fix | fix deep recursion | ✅ |
| 265 | `fix_float_precision.obs` | Bug Fix | fix float precision | ✅ |
| 266 | `fix_int_boundary.obs` | Bug Fix | fix int boundary | ✅ |
| 267 | `fix_large_arrays.obs` | Bug Fix | fix large arrays | ✅ |
| 268 | `fix_nested_generics.obs` | Bug Fix | fix nested generics | ✅ |
| 269 | `fix_nil_chain_ops.obs` | Bug Fix | fix nil chain ops | ✅ |
| 270 | `fix_polymorphic_calls.obs` | Bug Fix | fix polymorphic calls | ✅ |
| 271 | `fix_scope_shadowing.obs` | Bug Fix | fix scope shadowing | ✅ |
| 272 | `fix_string_concat.obs` | Bug Fix | fix string concat | ✅ |
| 273 | `float_constants_abs_binary.obs` | Other | Regression: Float constants, Float->Abs, ToBinaryString and hoisted Float division (#1176, #1178)... | ✅ |
| 274 | `float_literal_range.obs` | Other | Float literals at the edges of the double range compile to exactly the double strtod gives. Out-o... | ✅ |
| 275 | `float_precision_long_lines.obs` | Other | Regression: Float->ToString(v, precision) above 2^63 (#1173) and FileReader->ReadLine on lines lo... | ✅ |
| 276 | `float_tostring_large.obs` | Other | Regression: converting a Float of about 1e57 or more to a String killed the VM. Float->ToString,... | ✅ |
| 277 | `func_closure_field.obs` | Functional | func closure field | ✅ |
| 278 | `func_filter_ops.obs` | Functional | func filter ops | ✅ |
| 279 | `func_higher_order.obs` | Functional | func higher order | ✅ |
| 280 | `func_reduce_ops.obs` | Functional | func reduce ops | ✅ |
| 281 | `func_sort_custom.obs` | Functional | func sort custom | ✅ |
| 282 | `function_types.obs` | Other | Regression: function types (#1183). - An array parameter: `r := Plain(Int[]) ~ Int; r(a)` was "Un... | ✅ |
| 283 | `gc_bool_array_declaration.obs` | Other | Bool[] references must be declared as byte arrays. Bool->New[n] allocates a byte array (NEW_BYTE_... | ✅ |
| 284 | `gc_closure_capture_nursery_end.obs` | Other | A closure's captured young object must survive a collection that runs while the closure is being... | ✅ |
| 285 | `gc_closure_ids_stress.obs` | Other | Many distinct closure ids going live on several threads at once, while minor collections run. Thi... | ✅ |
| 286 | `gc_cond_value_root.obs` | Other | A conditional's object value must survive a minor collection that runs before the value is consum... | ✅ |
| 287 | `gc_cond_value_threads.obs` | Other | A conditional's object value must survive another thread's collection that runs while this thread... | ✅ |
| 288 | `gc_conservative_bad_class.obs` | Other | Guards G5: a conservative root that points INTO a young object must not have its "class pointer"... | ✅ |
| 289 | `gc_fixed_threshold_scales.obs` | Other | A heap threshold set with --gc-threshold is a trigger, not a promise to collect on every allocati... | ✅ |
| 290 | `gc_heap_sizing.obs` | Other | The threshold for a major collection follows the live heap. After each major collection the next... | ✅ |
| 291 | `gc_minor_closure_capture.obs` | Other | Guards G12: a minor GC must trace the captures of a closure stored in an OLD object. A closure's... | ✅ |
| 292 | `gc_minor_str_array_literal.obs` | Other | Guards G13: the elements of a String[] literal must survive a minor GC. A string-array literal is... | ✅ |
| 293 | `gc_mt_small_nursery_stress.obs` | Other | Multithreaded GC stress with thread exits overlapping collections. Guards the crash the stress pr... | ✅ |
| 294 | `gc_nursery_knob.obs` | Other | Nursery knob (--nursery / OBJECK_NURSERY) and the GC statistics it is read with. Guards: 1. runti... | ✅ |
| 295 | `gc_scoped_local_slot_types.obs` | Other | Frame slot types must match what the frame's slots actually hold. The collector (and OBJECK_GC_VE... | ✅ |
| 296 | `gc_verify_reflection.obs` | Other | Reflection objects the VM builds while the nursery is full. The VM allocates the Class, Method an... | ✅ |
| 297 | `gc_zero_field_nursery_end.obs` | Other | A zero-field object allocated just before a collection must survive it. An object's address is th... | ✅ |
| 298 | `gemini_error_message.obs` | Other | gemini error message | ✅ |
| 299 | `gemini_parse_test.obs` | Other | API.Google.Gemini response parsing, tested offline from recorded bodies that follow the public AP... | ✅ |
| 300 | `gemini_query_results.obs` | Other | gemini query results | ✅ |
| 301 | `generics_chained_calls.obs` | Other | Regression: generic type arguments through chained calls, generic returns and self-referential ge... | ✅ |
| 302 | `gl_context_test.obs` | Other | Regression test for OpenGL 3.3 core support: proves a real context can be created AND that someth... | ✅ |
| 303 | `gl_quaternion_test.obs` | Other | Game.OpenGL Quaternion -- arithmetic only, so it needs no window. Every other GL test in this sui... | ✅ |
| 304 | `http_error_body.obs` | Other | HTTP error-response body test A client that reports a status code but no body for a server error... | ✅ |
| 305 | `http_header_flatten_test.obs` | Other | Request-header flattening (Web.HTTP.HeaderCheck->Flatten). HTTP/2 and HTTP/3 hand the request to... | ✅ |
| 306 | `http_header_validation_test.obs` | Other | Request-header validation (Web.HTTP.HeaderCheck). HttpClient->AddHeader was injectable: HTTP/1.1... | ✅ |
| 307 | `http_persistence_test.obs` | Other | http persistence test | ✅ |
| 308 | `http_query_params.obs` | Other | http query params | ✅ |
| 309 | `http_status_codes.obs` | Other | http status codes | ✅ |
| 310 | `https_persistence_test.obs` | Other | The exit 139 this test reported on linux-arm64 in nightly run 35027973357 is recorded in docs/PLA... | ✅ |
| 311 | `ieee_float_semantics.obs` | Other | IEEE 754 semantics for NaN and infinity, across the interpreter AND the JIT. Windows had two defe... | ✅ |
| 312 | `indexed_call_result.obs` | Other | Subscripting the result of a method call: 'GetItems()[0]->Name()'. This was never implemented, an... | ✅ |
| 313 | `inference_client_test.obs` | Other | API.Inference: tensor bookkeeping, error reporting, and the two wire formats. EXTRA_LIBS: models,... | ✅ |
| 314 | `inline_funcref_param.obs` | Other | A method that takes a func-ref parameter and is small enough for the compiler to inline: the inli... | ✅ |
| 315 | `int64_min_literal.obs` | Other | Int's minimum is writable as a literal. 9223372036854775808 is out of range on its own, so '-9223... | ✅ |
| 316 | `int_semantics.obs` | Other | Int arithmetic has one meaning in the interpreter, both JITs and at every -opt level (docs/FEATUR... | ✅ |
| 317 | `integer_literal_prefixes.obs` | Other | Integer literal prefixes. Octal is written with a '0o' prefix, as hex takes '0x' and binary '0b'.... | ✅ |
| 318 | `interp_chained_call.obs` | Other | A chained method call inside string interpolation. "{$s->ToUpper()->Size()}" segfaulted the VM: n... | ✅ |
| 319 | `interp_float_fastpath.obs` | Other | reason: this test exercises the INTERPRETER's float fast path; compiled, it would test the JIT in... | ✅ |
| 320 | `io_create_new_process.obs` | I/O | File->CreateNew creates a file only when none of that name exists, as one step (O_EXCL on POSIX,... | ✅ |
| 321 | `io_file_basic.obs` | I/O | io file basic | ✅ |
| 322 | `io_rename_replaces.obs` | I/O | File->Rename replaces an existing target on every platform, as POSIX rename() does. On Windows th... | ✅ |
| 323 | `io_unicode_paths.obs` | I/O | File and directory names with non-ASCII characters work on every platform. Objeck strings are UTF... | ✅ |
| 324 | `jagged_arrays.obs` | Other | Jagged arrays (#1246; docs/JAGGED_ARRAYS_DESIGN.md). 'Int[][]' is an array of Int[] rows of any l... | ✅ |
| 325 | `jit_array_index_registers.obs` | AMD64/JIT | Multi-dimensional array accesses in JIT-compiled loops: the register that holds a later index is... | ✅ |
| 326 | `jit_array_native.obs` | AMD64/JIT | jit array native | ✅ |
| 327 | `jit_autojit_race.obs` | AMD64/JIT | Auto-JIT concurrency guard. Many threads call the same hot method, crossing the auto-JIT threshol... | ✅ |
| 328 | `jit_bitwise_mem_operand.obs` | AMD64/JIT | Regression for the AMD64 JIT and/or/xor memory-operand encoding (fuzzer F4, seeds 140/48/18/236).... | ✅ |
| 329 | `jit_branch_shapes.obs` | AMD64/JIT | Every branch shape the compiler emits for `&`, `\|` and `<>` in conditions, in loops that are JIT-... | ✅ |
| 330 | `jit_bridge_exception.obs` | AMD64/JIT (neg) | A C++ exception thrown inside a call the JIT's bridge made ends the program with the VM's interna... | ✅ |
| 331 | `jit_call_overhead.obs` | AMD64/JIT | F7 guard: a call from compiled code into compiled code must stay cheap. The calling convention (d... | ✅ |
| 332 | `jit_closure_call_in_creating_frame.obs` | AMD64/JIT | JIT: calling a capturing lambda while its creating frame is still on the stack. JitAmd64::Process... | ✅ |
| 333 | `jit_closure_gc_fixup.obs` | AMD64/JIT | Regression for the generational-GC fixup of closure captures (bug B1). The GC mark phase descends... | ✅ |
| 334 | `jit_closure_local_funcvar.obs` | AMD64/JIT | Guards a JIT crash present through v2026.9.4: a capturing lambda stored in a local function varia... | ✅ |
| 335 | `jit_concurrent_compile.obs` | AMD64/JIT | Concurrency guard for the JIT code-page allocator (PageManager::GetPage). Several threads JIT-com... | ✅ |
| 336 | `jit_conditional_native.obs` | AMD64/JIT | jit conditional native | ✅ |
| 337 | `jit_conditional_value_types.obs` | AMD64/JIT | A conditional expression ('c ? a : b') whose value is a Float or a function reference, interprete... | ✅ |
| 338 | `jit_const_char_store.obs` | AMD64/JIT | ARM64 JIT: a constant character stored into a Char[] element was written with a full 8-byte store... | ✅ |
| 339 | `jit_dispatch_native.obs` | AMD64/JIT | jit dispatch native | ✅ |
| 340 | `jit_entry_compiled.obs` | AMD64/JIT | DIFF_REQUIRES_JIT reason: asserts Main and Run are compiled; with --jit=off they are interpreted... | ✅ |
| 341 | `jit_entry_shapes.obs` | AMD64/JIT | The entry and exit of a compiled method, in the shapes the ARM64 callee work touches (the AMD64 b... | ✅ |
| 342 | `jit_float_branch_nan.obs` | AMD64/JIT | Regression: under the x64 JIT, `if(nan = x)` was taken and `if(nan <> x)` was not. The fused comp... | ✅ |
| 343 | `jit_float_compare_store.obs` | AMD64/JIT | GC_STRESS_SKIP reason: a long JIT float loop; with --gc-threshold=64k that took 637 s measured wi... | ✅ |
| 344 | `jit_float_divide_nan.obs` | AMD64/JIT | Float division by NaN, and division guarded by a Float compare against 0.0, interpreted and JIT-c... | ✅ |
| 345 | `jit_float_equality.obs` | AMD64/JIT | Regression test for float equality compares on array elements (2026-06). The front-end chose EQL_... | ✅ |
| 346 | `jit_float_intensive.obs` | AMD64/JIT | jit float intensive | ✅ |
| 347 | `jit_float_limits.obs` | AMD64/JIT | Float->NaN(), Float->Inf() and Float->NegInf() in JIT-compiled methods. The three are the opcodes... | ✅ |
| 348 | `jit_float_mem_ops.obs` | AMD64/JIT | Float arithmetic and comparison against MEMORY operands, under the JIT. IMPORTANT: must run with... | ✅ |
| 349 | `jit_float_round_trig.obs` | AMD64/JIT | Exercises two JIT float-codegen bugs that only surface once a method using them is auto-JIT'd (de... | ✅ |
| 350 | `jit_frame_trap_test.obs` | AMD64/JIT | Regression test for the JIT frame-dependent trap crash (2026-06). Traps such as SERL_INT/SERL_FLO... | ✅ |
| 351 | `jit_frame_unreferenced_local.obs` | AMD64/JIT | A compiled method declares a local that no instruction references, ahead of an object-array local... | ✅ |
| 352 | `jit_func_ref_hot.obs` | AMD64/JIT | jit func ref hot | ✅ |
| 353 | `jit_funcref_store_basic_lambda.obs` | AMD64/JIT | JIT: a func-ref store whose two words are different operand kinds. A func-ref variable is two wor... | ✅ |
| 354 | `jit_funcref_store_mixed_words.obs` | AMD64/JIT | JIT: storing a func-ref whose two words have different working-stack shapes. A capturing lambda b... | ✅ |
| 355 | `jit_gc_safepoint.obs` | AMD64/JIT | jit gc safepoint | ✅ |
| 356 | `jit_gc_stress.obs` | AMD64/JIT | JIT + GC interaction stress (2026-06). One CI run on linux-x64 failed with a JIT-to-JIT runtime e... | ✅ |
| 357 | `jit_loop_native.obs` | AMD64/JIT | jit loop native | ✅ |
| 358 | `jit_native_call_depth.obs` | AMD64/JIT (neg) | Recursion deeper than the call stack allows, through compiled-to-compiled native calls (the calli... | ✅ |
| 359 | `jit_native_call_error.obs` | AMD64/JIT (neg) | A compiled callee, called straight from compiled code (the calling convention's phase 3), derefer... | ✅ |
| 360 | `jit_native_cls_fields.obs` | AMD64/JIT | JIT Native Class Fields Test Tests object reference storage in class instance fields with GC pres... | ✅ |
| 361 | `jit_native_float_array.obs` | AMD64/JIT | JIT Native Float Array Test Tests native function with float array creation and math operations R... | ✅ |
| 362 | `jit_native_func_ref.obs` | AMD64/JIT | JIT Native Function Reference Test Tests native functions with function reference storage in clas... | ✅ |
| 363 | `jit_native_inline.obs` | AMD64/JIT | jit native inline | ✅ |
| 364 | `jit_native_math.obs` | AMD64/JIT | JIT Native Math Builtins Test Tests native math functions: Factorial, Sinh/Cosh/Tanh/Log2/Cbrt, P... | ✅ |
| 365 | `jit_nil_inlined_field.obs` | AMD64/JIT (neg) | An inlined getter called on a Nil object must raise the same Nil-dereference error compiled as in... | ✅ |
| 366 | `jit_nil_inlined_field_float.obs` | AMD64/JIT (neg) | Float sibling of jit_nil_inlined_field.obs: an inlined Float getter on a Nil object. At -opt s3 c... | ✅ |
| 367 | `jit_nil_inlined_field_funcref.obs` | AMD64/JIT (neg) | Func-ref sibling of jit_nil_inlined_field.obs: an inlined getter returning a function-reference f... | ✅ |
| 368 | `jit_nil_inlined_field_object.obs` | AMD64/JIT (neg) | Object sibling of jit_nil_inlined_field.obs: an inlined getter returning an object field, on a Ni... | ✅ |
| 369 | `jit_recovery_through_bridge.obs` | AMD64/JIT | Regression: `?->` recovery from a fault several frames below a compiled method (#1171, JIT half).... | ✅ |
| 370 | `jit_safepoint_pending_values.obs` | AMD64/JIT | A loop's GC safepoint keeps the values an expression still has pending. The JIT polls for a stop-... | ✅ |
| 371 | `jit_stack_value_across_branch.obs` | AMD64/JIT | JIT: a value left on the working stack across a branch (#1256). `f(a <> Nil & a->Kind() = 6, b <>... | ✅ |
| 372 | `jit_string_ops.obs` | AMD64/JIT | jit string ops | ✅ |
| 373 | `jit_tailcall_loop_recovery.obs` | AMD64/JIT | Regression: `?->` recovery from an error inside a method whose tail call the optimizer turned int... | ✅ |
| 374 | `jit_tco_bare_local.obs` | AMD64/JIT | Regression for the TCO deferred-local-load miscompile (both arches). A self-recursive tail call t... | ✅ |
| 375 | `jit_virtual_equals.obs` | AMD64/JIT | Issue #722: on ARM64 the JIT miscompiled String->Equals inside a virtual request-handler callback... | ✅ |
| 376 | `json_build_ops.obs` | JSON | json build ops | ✅ |
| 377 | `json_codec_escapes.obs` | JSON | JsonElement->Decode and Encode agree on every JSON escape. \b and \f were mapped to 0x10 and 0x14... | ✅ |
| 378 | `json_escape_test.obs` | JSON | JsonElement must escape on serialization. Format's STRING branch appended the raw value between t... | ✅ |
| 379 | `json_hand_built_escapes.obs` | JSON | JSON that a library builds by hand is valid JSON whatever the text holds. CsvRow->ToJson pasted e... | ✅ |
| 380 | `json_number_grammar.obs` | JSON | Data.JSON and Data.JSON.Stream number grammar (RFC 8259): -? (0 \| [1-9][0-9]*) (. [0-9]+)? ([eE]... | ✅ |
| 381 | `json_parse_ops.obs` | JSON | json parse ops | ✅ |
| 382 | `json_rpc_escapes.obs` | JSON | Data.JSON.RPC over a loopback socket: strings are escaped once on the way out and decoded once on... | ✅ |
| 383 | `json_string_roundtrip.obs` | JSON | Data.JSON strings: the parser decodes, the serializer escapes, and a round trip is the identity (... | ✅ |
| 384 | `lambda_andor_entry_space.obs` | Other | Interpreter frames were one to two words too small for a method at the compiler's local limit. Th... | ✅ |
| 385 | `lambda_capture_after_func.obs` | Other | Regression: a lambda capture that follows a captured function value. Found by the Rosetta batch-3... | ✅ |
| 386 | `lame_encode_test.obs` | Other | Audio.Lame->PcmToMp3 encodes PCM to MP3. EXTRA_LIBS: lame This library had no runtime test at all... | ✅ |
| 387 | `lang_array_bytes.obs` | Other | Raw conversion between numeric arrays and bytes, one memcpy each: Int->ToBytes / Byte->ToInts and... | ✅ |
| 388 | `lang_array_copy.obs` | Other | Bulk primitive array moves: Int/Float/Byte/Char->Copy(dest, dest_offset, src, src_offset, count)... | ✅ |
| 389 | `lang_bytebuffer_written_bytes.obs` | Other | System.ByteBuffer covers the bytes written, not its capacity. ToString hex-dumped the whole backi... | ✅ |
| 390 | `lang_compress_deflate.obs` | Other | Byte->CompressDeflate/UncompressDeflate are raw deflate (RFC 1951), and the web server's response... | ✅ |
| 391 | `lang_compress_level.obs` | Other | Byte->CompressZlib(input, level), and Serializer->Serialize compressing at level 1. Serialize use... | ✅ |
| 392 | `lang_float_exact.obs` | Other | Float->ToShortString: the shortest text that reads back as the same Float. Float->ToString prints... | ✅ |
| 393 | `lang_inflate_limit.obs` | Other | Byte->UncompressDeflate(input, limit) and Byte->UncompressGzip(input, limit) inflate at most 'lim... | ✅ |
| 394 | `lang_tofloat_range.obs` | Other | String->ToFloat on out-of-range text, and String->ToInt's prefixes under the JIT. ToFloat went th... | ✅ |
| 395 | `lang_trap_method_returns.obs` | Other | Library methods with a return type return a value. lang.obs is compiled by the bootstrap compiler... | ✅ |
| 396 | `lib_alias_vision.obs` | Other | The -lib alias @vision links: it is closed under its members' dependencies. EXTRA_LIBS: @vision A... | ✅ |
| 397 | `literal_and_call_indexing.obs` | Other | Regression: subscripting a literal or a call result (#1184). - `["a", "b"][1]` did not parse: the... | ✅ |
| 398 | `loop_literal_condition.obs` | Other | Loops whose condition is a Bool literal, a Bool variable or a call. A 'for' whose condition was a... | ✅ |
| 399 | `lsp_features.obs` | LSP | lsp features | ✅ |
| 400 | `math_bigint.obs` | Math | System.Math.BigInt: exact integer arithmetic at any size. EXTRA_LIBS: math Objeck's Int is 64-bit... | ✅ |
| 401 | `math_complex.obs` | Math | System.Math.Complex: complex numbers with Float parts. EXTRA_LIBS: math What is pinned here are t... | ✅ |
| 402 | `math_float_ops.obs` | Math | math float ops | ✅ |
| 403 | `math_log_exp.obs` | Math | math log exp | ✅ |
| 404 | `math_random_ops.obs` | Math | math random ops | ✅ |
| 405 | `math_rational.obs` | Math | System.Math.Rational: exact fractions, backed by BigInt. EXTRA_LIBS: math Backed by BigInt rather... | ✅ |
| 406 | `math_rounding.obs` | Math | math rounding | ✅ |
| 407 | `math_sqrt_ops.obs` | Math | math sqrt ops | ✅ |
| 408 | `math_trig_funcs.obs` | Math | math trig funcs | ✅ |
| 409 | `mcp_debug_test.obs` | MCP Server | DEBUG VERSION of mcp_server_test.obs Identical to programs/regression/mcp_server_test.obs except:... | ✅ |
| 410 | `mcp_server_bad_config.obs` | MCP Server | McpServer->Serve with a configuration it cannot use. Serve used to read config->Get("http_port"),... | ✅ |
| 411 | `mcp_server_bound_port_test.obs` | MCP Server | McpServer->Serve with an "http_port" of 0: the system chooses the port and McpServer->GetBoundPor... | ✅ |
| 412 | `mcp_server_malformed_requests.obs` | MCP Server | McpRequestHandler->ProcessPost read client-supplied JSON without checks: root_json->Get("method")... | ✅ |
| 413 | `mcp_server_test.obs` | MCP Server | mcp server test | ✅ |
| 414 | `method_generics.obs` | Other | Method-level generics, phase 1 (#1242; docs/METHOD_GENERICS_DESIGN.md). A function or method decl... | ✅ |
| 415 | `method_generics_library.obs` | Other | Method-level generics in libraries (#1242, phase 2; docs/METHOD_GENERICS_DESIGN.md). A library's... | ✅ |
| 416 | `minor_gc_stress.obs` | Other | Regression for generational MINOR GC: old objects holding young references. 'keep' is an object a... | ✅ |
| 417 | `ml_activations.obs` | System.ML | Numerical correctness of the System.ML activation functions and scalers. Softmax normalized over... | ✅ |
| 418 | `ml_adaboost_test.obs` | System.ML | Regression tests for System.ML AdaBoost (overhaul phase 3): boosting over boolean decision stumps... | ✅ |
| 419 | `ml_api_test.obs` | System.ML | Regression tests for the System.ML estimator API consistency sweep (item 11): RandomForest Fit (r... | ✅ |
| 420 | `ml_base_rate_groups.obs` | System.ML | Metrics->PrecisionAtBaseRate, ShuffleLabels, RecallByGroup and UnseenGroups. PrecisionAtBaseRate... | ✅ |
| 421 | `ml_column_sum_fractions.obs` | System.ML | Matrix2D->SumColumn and Matrix2D->AverageColumn started their accumulator as an Int (`sum := 0;`)... | ✅ |
| 422 | `ml_cross_val_oof.obs` | System.ML | CrossValidation: out-of-fold scores, the fold row mapping, stratification by a key other than the... | ✅ |
| 423 | `ml_dbscan_test.obs` | System.ML | Regression tests for System.ML DBSCAN (overhaul phase 3): two dense blobs plus far-away outliers... | ✅ |
| 424 | `ml_decision_tree_classifier.obs` | System.ML | DecisionTreeClassifier: continuous splits chosen by Gini. DecisionTree takes Bool[,], so continuo... | ✅ |
| 425 | `ml_degenerate_ratios.obs` | System.ML | Degenerate inputs must not take the VM down. Float division by zero RAISES in Objeck rather than... | ✅ |
| 426 | `ml_embedding_classifier.obs` | System.ML | EmbeddingClassifier: a logistic or multi-layer head over fixed-size embeddings, trained by miniba... | ✅ |
| 427 | `ml_experiment_runner.obs` | System.ML | CrossValidation's experiment runner: ScoreModel, Split and Evaluate. Two seams made every experim... | ✅ |
| 428 | `ml_feature_scaler_stateful.obs` | System.ML | FeatureScaler's stateful Fit/Transform pair. The static StandardScaler standardizes whatever matr... | ✅ |
| 429 | `ml_forest_lab_params.obs` | System.ML | The random-forest parameters a lab sweeps, on DecisionTreeClassifier and RandomForestClassifier:... | ✅ |
| 430 | `ml_gbt_classifier.obs` | System.ML | GradientBoostedClassifier: boosting under logistic loss. The regression GradientBoostedTrees fits... | ✅ |
| 431 | `ml_gbt_test.obs` | System.ML | Regression tests for System.ML gradient boosting (overhaul phase 3 leftover): a RegressionTree le... | ✅ |
| 432 | `ml_gmm_test.obs` | System.ML | Regression tests for System.ML GaussianMixture (overhaul phase 3): EM on two well-separated blobs... | ✅ |
| 433 | `ml_kdtree_test.obs` | System.ML | Regression tests for System.ML KDTree (overhaul phase 3): for several queries and k values over a... | ✅ |
| 434 | `ml_kernel_svm_edges.obs` | System.ML | KernelSVM edge cases, parameter semantics and determinism. Bad input is refused with a reason (on... | ✅ |
| 435 | `ml_kernel_svm_geometry.obs` | System.ML | KernelSVM against known geometry, with the optimality (KKT) conditions checked after every fit fr... | ✅ |
| 436 | `ml_kernel_svm_reference.obs` | System.ML | KernelSVM against an independent reference, without Python. Generated by tools/ml/svm_reference/g... | ✅ |
| 437 | `ml_kmeans_group_order.obs` | System.ML | KMeansGrouping lists its groups in a defined order: KMeans->Group keeps the order of its labels (... | ✅ |
| 438 | `ml_kmeans_groups.obs` | System.ML | KMeans->Group returned empty groups, and GetDunnIndex then divided by zero. Group filed each reco... | ✅ |
| 439 | `ml_lab_chart_svg.obs` | System.ML | System.ML.Lab.Chart: standalone SVG line, bar and loss-curve charts. Three kinds of check. Struct... | ✅ |
| 440 | `ml_lab_experiment.obs` | System.ML | System.ML.Lab.Experiment: a sweep over one parameter, with repeats. The Trial here returns predic... | ✅ |
| 441 | `ml_lab_labels_encoding.obs` | System.ML | System.ML.Lab.Labels, and the NSL-KDD encoding path through TableEncoder and FeatureScaler. The l... | ✅ |
| 442 | `ml_lab_resample.obs` | System.ML | System.ML.Lab.Resample: a training set at a requested positive ratio, with the test set left exac... | ✅ |
| 443 | `ml_lab_result_table.obs` | System.ML | System.ML.Lab.ResultTable: the table a sweep hands in, rendered four ways and read back from two... | ✅ |
| 444 | `ml_lab_split.obs` | System.ML | System.ML.Lab.Partition: a deterministic, stratified hold-out split. The counts are exact, not ap... | ✅ |
| 445 | `ml_lab_stopwatch.obs` | System.ML | System.ML.Lab.Stopwatch: wall-clock milliseconds that accumulate across Start/Stop pairs until Re... | ✅ |
| 446 | `ml_library_test.obs` | System.ML | ml library test | ✅ |
| 447 | `ml_linearclf_test.obs` | System.ML | Regression tests for the System.ML linear classifiers (overhaul phase 2): Perceptron (mistake-dri... | ✅ |
| 448 | `ml_matrix_reader_columns.obs` | System.ML | MatrixReader's target_offset is the number of target columns, taken from the end of each row. Its... | ✅ |
| 449 | `ml_matrix_reader_line_endings.obs` | System.ML | MatrixReader reads rows through CsvTable, which used to split on CRLF unless it was handed anothe... | ✅ |
| 450 | `ml_matrix_shape_mismatch.obs` | System.ML | Matrix2D operations on operands they cannot combine must return Nil, not fault. The native matrix... | ✅ |
| 451 | `ml_nn_test.obs` | System.ML | Regression tests for the System.ML NeuralNetwork with hidden/output bias vectors (ML overhaul ite... | ✅ |
| 452 | `ml_one_class_svm.obs` | System.ML | OneClassSVM: the nu property, the optimality conditions, sign conventions and novelty detection.... | ✅ |
| 453 | `ml_one_class_svm_reference.obs` | System.ML | OneClassSVM against an independent reference, without Python. Generated by tools/ml/svm_reference... | ✅ |
| 454 | `ml_one_class_svm_tolerance.obs` | System.ML | OneClassSVM: why the flagged training fraction can exceed nu at the default tolerance, and that i... | ✅ |
| 455 | `ml_outlier_autoencoder.obs` | System.ML | System.ML DenseNetwork and AutoencoderDetector against an independent numpy reference. The refere... | ✅ |
| 456 | `ml_outlier_dbscan.obs` | System.ML | System.ML DbscanDetector: DBSCAN's noise as an OutlierDetector. The outliers must be exactly the... | ✅ |
| 457 | `ml_outlier_envelope.obs` | System.ML | System.ML EllipticEnvelope (FastMCD) against independent references. * Chi-square quantiles again... | ✅ |
| 458 | `ml_outlier_iforest.obs` | System.ML | System.ML IsolationForest. The average path length c(n) is checked exactly against the formula. A... | ✅ |
| 459 | `ml_outlier_lof.obs` | System.ML | System.ML LocalOutlierFactor against an independent brute-force reference. LOF is deterministic,... | ✅ |
| 460 | `ml_outlier_mcd.obs` | System.ML | System.ML EllipticEnvelope's MCD search against an independent reference, on 30 contaminated near... | ✅ |
| 461 | `ml_outlier_ocsvm.obs` | System.ML | System.ML OneClassSvmDetector: OneClassSVM through the OutlierDetector interface. The adapter mus... | ✅ |
| 462 | `ml_outlier_sweep.obs` | System.ML | System.ML ConfusionMatrix, Metrics->FalsePositiveRate and SensitivitySweep. ConfusionMatrix is ch... | ✅ |
| 463 | `ml_outlier_zscore.obs` | System.ML | System.ML ZScoreDetector against an independent reference. The expected scores are numpy's (scipy... | ✅ |
| 464 | `ml_pca_gnb_test.obs` | System.ML | Regression tests for System.ML PCA (power-iteration decomposition: dominant diagonal direction re... | ✅ |
| 465 | `ml_phase1_test.obs` | System.ML | Regression tests for the System.ML correctness fixes (phase 1): seedable PRNG, DotSigmoid dimensi... | ✅ |
| 466 | `ml_random_forest_classifier.obs` | System.ML | RandomForestClassifier: bootstrapped, feature-sampled continuous trees, averaged. Two properties... | ✅ |
| 467 | `ml_regularized_test.obs` | System.ML | Regression tests for the System.ML regularized linear models (overhaul phase 2): RidgeRegression... | ✅ |
| 468 | `ml_score_metrics.obs` | System.ML | Score-based metrics: RecallAtFpr, ThresholdAtFpr, AucRoc, AveragePrecision and the two curves. Me... | ✅ |
| 469 | `ml_sort_depth.obs` | System.ML | System.ML's two hand-written quicksorts: KDTree's SortByDim (sorts row indexes by one coordinate)... | ✅ |
| 470 | `ml_stratified_kfold.obs` | System.ML | CrossValidation->StratifiedKFold: folds that keep the class ratio, from a seed. The existing KFol... | ✅ |
| 471 | `ml_table_encoder.obs` | System.ML | TableEncoder: CsvTable to Float[,] with a vocabulary fixed at Fit. Two properties carry this test... | ✅ |
| 472 | `ml_threshold_bootstrap.obs` | System.ML | Metrics->AtThreshold and Metrics->Bootstrap, plus Percentile underneath them. AtThreshold is chec... | ✅ |
| 473 | `ml_trees_test.obs` | System.ML | Regression tests for the System.ML tree models: the real recursive DecisionTree (left/right child... | ✅ |
| 474 | `named_arguments.obs` | Other | Named arguments, phase 1 (#1244; docs/NAMED_ARGUMENTS_DESIGN.md). `name: value` binds an argument... | ✅ |
| 475 | `named_arguments_library.obs` | Other | Named arguments to library methods (#1244, phase 2; docs/NAMED_ARGUMENTS_DESIGN.md). A library's... | ✅ |
| 476 | `native_gc_barrier_test.obs` | Other | A value returned by a native library must survive a collection. A C++ shared library returns a va... | ✅ |
| 477 | `native_gc_leak_test.obs` | Other | Objects a native library returns must be RECLAIMED, not merely reachable. native_gc_barrier_test.... | ✅ |
| 478 | `native_lib_gc_park.obs` | Other | Blocking calls in native libraries must park for garbage collection A stop-the-world collection w... | ✅ |
| 479 | `net_io_gc_park.obs` | Other | Blocking I/O must park for garbage collection: DTLS, HTTP/3, UDP, named pipes and Runtime->Comman... | ✅ |
| 480 | `net_resolve_failure.obs` | Other | TCPSocket->Resolve failure-path test Resolve() freed its addrinfo result on the FAILURE path, whe... | ✅ |
| 481 | `new_array_indexed_size.obs` | Other | Regression: `T->New[a[i]]` was compiled as a COPY of the array a, not as an array of a[i] element... | ✅ |
| 482 | `nil_coalesce_values.obs` | Other | Regression: `??` after a guarded call that yields a value type (#1172). `??` tested its left side... | ✅ |
| 483 | `nil_safe_ops.obs` | Core Language | Nil-safe operators: '??' (nil-coalesce) and '?->' (nil-safe call). Both desugar onto existing int... | ✅ |
| 484 | `nil_safe_statement.obs` | Core Language | DIFF_CONFIGS: s3 reason: the million-deep guarded Down() needs the s3 tail-call rewrite; at s0 it... | ✅ |
| 485 | `nlp_api_sequence.obs` | System.NLP | ApiSequenceTokenizer: a vocabulary of API names built from training traces, and the encodings of... | ✅ |
| 486 | `nlp_onnx_embedder.obs` | System.NLP | BertEmbedder: sentence embeddings from a BERT-shaped ONNX model, checked against the model's clos... | ✅ |
| 487 | `nlp_porter.obs` | System.NLP | System.NLP Porter stemmer (docs/NLP_DESIGN.md, section 4): the worked examples from Porter's 1980... | ✅ |
| 488 | `nlp_sentiment.obs` | System.NLP | System.NLP sentiment (docs/NLP_DESIGN.md, section 5): each VADER-style rule on its own (negation,... | ✅ |
| 489 | `nlp_tfidf.obs` | System.NLP | System.NLP TF-IDF and similarity (docs/NLP_DESIGN.md, sections 6 and 7): a refit starts from an e... | ✅ |
| 490 | `nlp_tokenize.obs` | System.NLP | System.NLP tokenization (docs/NLP_DESIGN.md, sections 2 and 3): contractions, numbers, hyphenated... | ✅ |
| 491 | `nlp_wordpiece.obs` | System.NLP | WordPieceTokenizer: BERT's BasicTokenizer and WordPiece, and the encodings a BERT-family model ta... | ✅ |
| 492 | `nlp_wordpiece_parity.obs` | System.NLP | WordPieceTokenizer gives the same ids as BERT's own tokenizer on a corpus: 234 texts, each in thr... | ✅ |
| 493 | `oauth_test.obs` | Networking | oauth test | ✅ |
| 494 | `obj_size_layout.obs` | Other | Object field layout must fit the object allocation exactly. The compiler records a class's instan... | ✅ |
| 495 | `odbc_prepared_reuse.obs` | ODBC | A Database.ODBC ParameterStatement runs as many times as its caller likes. The statement's column... | ✅ |
| 496 | `odbc_sqlite_test.obs` | ODBC | ODBC SQLite Integration Test Tests live database operations against an in-memory SQLite database:... | ✅ |
| 497 | `odbc_whole_values.obs` | ODBC | Database.ODBC reads and writes whole values, and says why a SELECT failed. Four defects in the OD... | ✅ |
| 498 | `ollama_api_test.obs` | Other | API.Ollama against recorded payloads, no server needed: chat tool calling (the tool_calls reply,... | ✅ |
| 499 | `ollama_parse_test.obs` | Other | Completion->ParseGenerateResponse: the response handling behind every Completion->Generate overlo... | ✅ |
| 500 | `onnx_failed_session.obs` | Other | A failed ONNX session is detectable, and inference refuses instead of aborting. EXTRA_LIBS: model... | ✅ |
| 501 | `onnx_generic_run.obs` | Other | The generic ONNX run entry point: its refusals, and the tensor bookkeeping around it. EXTRA_LIBS:... | ✅ |
| 502 | `onnx_runtime_test.obs` | Other | API.Onnx.OnnxRuntime->GetProviders() reaches the native ONNX Runtime. EXTRA_LIBS: models,onnx,ope... | ✅ |
| 503 | `onnx_tiny_bert_model.obs` | Other | The tiny BERT-shaped test models under data/bert run on this platform's ONNX Runtime and compute... | ✅ |
| 504 | `openai_parse_test.obs` | Other | API.OpenAI response parsing, tested offline from recorded bodies that follow the public API refer... | ✅ |
| 505 | `opt_dead_store_first_instr.obs` | Other | reason: the leaked operand only corrupts the caller on the interpreter's shared operand stack; JI... | ✅ |
| 506 | `opt_dead_store_side_effects.obs` | Other | Regression: dead-store elimination dropped side effects. Found by the differential fuzzer against... | ✅ |
| 507 | `opt_dead_store_stack_balance.obs` | Other | reason: the leaked operands only corrupt the caller on the interpreter's shared operand stack; JI... | ✅ |
| 508 | `opt_funcref_local_slots.obs` | Other | Optimizer slot numbering with a function-reference local. A func-ref local takes two slots, but t... | ✅ |
| 509 | `opt_inline_and_or_slots.obs` | Other | An object local of a method inlined at -opt s3 must stay a traced root. The inliner appends the c... | ✅ |
| 510 | `opt_inline_label_collision.obs` | Other | Two inlined bodies must not share a label space (#1037). A method's labels are numbered from its... | ✅ |
| 511 | `opt_int_division.obs` | Other | Integer division must give the same answer at every optimization level. Two -opt rewrites produce... | ✅ |
| 512 | `parser_gaps.obs` | Other | Regression: parse gaps found by the Rosetta batch-3 tasks (#1184). - An array literal ate the com... | ✅ |
| 513 | `pipe_closed_peer.obs` | Other | A write to a pipe whose other end has closed fails; it does not end the VM On POSIX a Pipe is a U... | ✅ |
| 514 | `primitive_receiver_call_result.obs` | Other | Regression: a primitive method called on a CALL RESULT got its arguments reversed. Found by the R... | ✅ |
| 515 | `primitive_receiver_order.obs` | Other | Argument order for instance-style calls on primitives. Writing `v->Pow(10)` on a primitive does n... | ✅ |
| 516 | `primitive_receiver_resolution.obs` | Other | Calls on primitive receivers resolve to the $-class function that takes the receiver as parameter... | ✅ |
| 517 | `query_aggregates.obs` | Query.Structured | Regression tests for Query.Structured (query.obl), which had none: - Average, Sum, Min, Max and C... | ✅ |
| 518 | `query_archives_in_sql.obs` | Query.Structured | Query.Structured: archives and workbooks named in SQL (docs/QUERY_DESIGN.md, "Archives and workbo... | ✅ |
| 519 | `query_bridges.obs` | Query.Structured | Query.Structured's bridges (docs/QUERY_DESIGN.md section 7, phase 4c): a table or a view out to C... | ✅ |
| 520 | `query_catalog_concurrent.obs` | Query.Structured | Query.Structured: one Catalog shared by several threads (docs/QUERY_DESIGN.md, "Concurrent reader... | ✅ |
| 521 | `query_differential.obs` | Query.Structured | Query.Structured differential test (docs/QUERY_DESIGN.md, "Testing"). Random tables (every column... | ✅ |
| 522 | `query_expr_concurrent.obs` | Query.Structured | Query.Structured: expressions under one Catalog shared by several threads (docs/QUERY_DESIGN.md,... | ✅ |
| 523 | `query_expr_differential.obs` | Query.Structured | Query.Structured expressions, differentially (docs/QUERY_DESIGN.md, "Expressions in every clause"... | ✅ |
| 524 | `query_expr_grouped.obs` | Query.Structured | Query.Structured: expressions in grouped queries (docs/QUERY_DESIGN.md, "Expressions in every cla... | ✅ |
| 525 | `query_expr_join.obs` | Query.Structured | Query.Structured: expressions in JOIN ... ON (docs/QUERY_DESIGN.md, "Expressions in every clause"... | ✅ |
| 526 | `query_expr_where.obs` | Query.Structured | Query.Structured: expressions in WHERE and ORDER BY (docs/QUERY_DESIGN.md, "Expressions in every... | ✅ |
| 527 | `query_expr_zone_maps.obs` | Query.Structured | Query.Structured expressions across row-group and batch boundaries (docs/QUERY_DESIGN.md, "Reduct... | ✅ |
| 528 | `query_file_rewritten.obs` | Query.Structured | Query.Structured: a file rewritten within the second it was read, to the same size, is read again... | ✅ |
| 529 | `query_index_basic.obs` | Query.Structured | Query.Structured indexes (docs/QUERY_DESIGN.md, section 5, phase 6): hash and sorted indexes on e... | ✅ |
| 530 | `query_index_concurrent.obs` | Query.Structured | Query.Structured: one table queried from several threads right after a write (docs/QUERY_DESIGN.m... | ✅ |
| 531 | `query_index_differential.obs` | Query.Structured | Query.Structured indexes, differentially (docs/QUERY_DESIGN.md, sections 5 and "Testing"). For a... | ✅ |
| 532 | `query_index_plan.obs` | Query.Structured | Query.Structured's rule-based planner (docs/QUERY_DESIGN.md, section 5, phase 6): the plan a view... | ✅ |
| 533 | `query_nan_compare.obs` | Query.Structured | NaN in Query.Structured conditions and sorts (found by query_index_differential). The comparison... | ✅ |
| 534 | `query_net_range.obs` | Query.Structured | Query.Structured reading an .otab file over HTTP (docs/QUERY_DESIGN.md, section 10, phase 5), aga... | ✅ |
| 535 | `query_net_read.obs` | Query.Structured | Query.Structured reading files over HTTP (docs/QUERY_DESIGN.md, section 10, phase 5), against an... | ✅ |
| 536 | `query_net_serve.obs` | Query.Structured | Query.Structured serving tables (docs/QUERY_DESIGN.md, section 10, phase 5), against an in-proces... | ✅ |
| 537 | `query_persist_crash.obs` | Query.Structured | Query.Structured's .otab saves are crash safe (docs/OTAB_FORMAT.md, "Writing a file"): Save and A... | ✅ |
| 538 | `query_persist_indexes.obs` | Query.Structured | Query.Structured: index definitions stored in .otab files (docs/OTAB_FORMAT.md, format 2; docs/QU... | ✅ |
| 539 | `query_persist_lock.obs` | Query.Structured | Query.Structured's writer lock (QUERY_DESIGN.md, phase 3b): opening a table for writing takes 'pa... | ✅ |
| 540 | `query_persist_log.obs` | Query.Structured | Query.Structured's durable tables (docs/OTAB_FORMAT.md, "The change log"; QUERY_DESIGN.md, phase... | ✅ |
| 541 | `query_persist_roundtrip.obs` | Query.Structured | Query.Structured's .otab file (docs/QUERY_DESIGN.md, section 9, phase 3): a table with every colu... | ✅ |
| 542 | `query_select_expressions.obs` | Query.Structured | Query.Structured: arithmetic in the select list (docs/QUERY_DESIGN.md, section 4, "Expressions in... | ✅ |
| 543 | `query_serialize_generic.obs` | Query.Structured | Query.Structured tables through the language's own Serializer and Deserializer (docs/QUERY_DESIGN... | ✅ |
| 544 | `query_server_concurrent.obs` | Query.Structured | Query.Structured's TableServer answering requests at once (docs/QUERY_DESIGN.md, "A server that a... | ✅ |
| 545 | `query_source_archive.obs` | Query.Structured | Query.Structured ArchiveSource (docs/QUERY_DESIGN.md, section 6, phase 7): a zip, a tar and a gzi... | ✅ |
| 546 | `query_source_csv.obs` | Query.Structured | Query.Structured CsvSource (docs/QUERY_DESIGN.md, section 6): whole-column typing, empty cells, d... | ✅ |
| 547 | `query_source_files_in_sql.obs` | Query.Structured | Query.Structured files named in SQL (docs/QUERY_DESIGN.md, section 6, phase 4): a quoted path in... | ✅ |
| 548 | `query_source_filesystem.obs` | Query.Structured | Query.Structured FilesystemSource and Finder (docs/QUERY_DESIGN.md, section 6): a flat and a recu... | ✅ |
| 549 | `query_source_gzip.obs` | Query.Structured | Query.Structured reads gzipped sources (docs/QUERY_DESIGN.md, section 6, phase 2): a '.gz' CSV, J... | ✅ |
| 550 | `query_source_http.obs` | Query.Structured | Query.Structured HttpJsonSource (docs/QUERY_DESIGN.md, sections 6 and 10, phase 5), against an in... | ✅ |
| 551 | `query_source_json.obs` | Query.Structured | Query.Structured JsonSource (docs/QUERY_DESIGN.md, section 6, phase 4): an array of objects as a... | ✅ |
| 552 | `query_source_ml.obs` | Query.Structured | Query.Structured's System.ML source, MlResultSource (docs/QUERY_DESIGN.md section 6, tier 2, quer... | ✅ |
| 553 | `query_source_objects.obs` | Query.Structured | Query.Structured ObjectSource (docs/QUERY_DESIGN.md, section 6): the program's own objects as a t... | ✅ |
| 554 | `query_source_odbc.obs` | Query.Structured | Query.Structured's ODBC source, OdbcSource (docs/QUERY_DESIGN.md section 6, tier 2, query_odbc.ob... | ✅ |
| 555 | `query_source_text.obs` | Query.Structured | Query.Structured TextSource and JsonLinesSource (docs/QUERY_DESIGN.md, section 6, phase 2): a log... | ✅ |
| 556 | `query_source_tree.obs` | Query.Structured | Query.Structured over a directory tree (docs/QUERY_DESIGN.md, section 6, phase 4): the recursive... | ✅ |
| 557 | `query_source_vision.obs` | Query.Structured | Query.Structured's vision source, VisionSource (docs/QUERY_DESIGN.md section 6, tier 2, query_vis... | ✅ |
| 558 | `query_source_xlsx.obs` | Query.Structured | Query.Structured XlsxSource (docs/QUERY_DESIGN.md, section 6, phase 7): worksheets of an .xlsx wo... | ✅ |
| 559 | `query_source_xml.obs` | Query.Structured | Query.Structured XmlSource (docs/QUERY_DESIGN.md, section 6, phase 4): row elements found at any... | ✅ |
| 560 | `query_sql_relational.obs` | Query.Structured | Query.Structured's relational SQL (docs/QUERY_DESIGN.md, sections 4 and 6, phase 4): GROUP BY wit... | ✅ |
| 561 | `query_storage.obs` | Query.Structured | Query.Structured column storage (docs/QUERY_DESIGN.md, section 1, phase 1): typing and promotion,... | ✅ |
| 562 | `query_thread_errors.obs` | Query.Structured | Query.Structured: GetError reports the calling thread's last error (docs/QUERY_DESIGN.md, "Concur... | ✅ |
| 563 | `query_url_cache_processes.obs` | Query.Structured | Query.Structured's URL download cache shared by several processes (docs/QUERY_DESIGN.md, "One cac... | ✅ |
| 564 | `query_views.obs` | Query.Structured | Query.Structured predicates, views and prepared statements (docs/QUERY_DESIGN.md, sections 2 and... | ✅ |
| 565 | `query_zone_map_wide_ints.obs` | Query.Structured | Zone maps keep each row group's minimum and maximum as Floats. An Int above 2^53 rounds when conv... | ✅ |
| 566 | `recovered_errors.obs` | Other | Reading the error a ?-> region recovered from (#1247; docs/RECOVERED_ERRORS_DESIGN.md). ?-> turns... | ✅ |
| 567 | `ref_parameters.obs` | Other | Reference parameters (#1243; docs/REFERENCE_PARAMETERS_DESIGN.md). function : Swap(ref a : Int, r... | ✅ |
| 568 | `reflection_signatures.obs` | Other | Regression: reflection over method signatures (#1175). - DataType->GetType() returned -1000..-994... | ✅ |
| 569 | `regex_bench.obs` | Regex | regex bench | ✅ |
| 570 | `regex_classes.obs` | Regex | Query.RegEx: character classes (several ranges, negation, punctuation, escapes, literal '-' and '... | ✅ |
| 571 | `regex_dfa_test.obs` | Regex | regex dfa test | ✅ |
| 572 | `rosetta_additive_primes.obs` | Other | Rosetta Code: Additive primes https://rosettacode.org/wiki/Additive_primes A prime whose digit su... | ✅ |
| 573 | `rosetta_align_columns.obs` | Other | Rosetta Code: Align columns https://rosettacode.org/wiki/Align_columns Claimed in #1137 Splits th... | ✅ |
| 574 | `rosetta_anonymous_recursion.obs` | Other | Rosetta Code: Anonymous recursion https://rosettacode.org/wiki/Anonymous_recursion Claimed in #11... | ✅ |
| 575 | `rosetta_append_a_record_to_the_end_of_a_text_file.obs` | Other | Rosetta Code: Append a record to the end of a text file https://rosettacode.org/wiki/Append_a_rec... | ✅ |
| 576 | `rosetta_arithmetic_complex.obs` | Other | Rosetta Code: Arithmetic/Complex https://rosettacode.org/wiki/Arithmetic/Complex Claimed in #1164... | ✅ |
| 577 | `rosetta_associative_array_merging.obs` | Other | Rosetta Code: Associative array/Merging https://rosettacode.org/wiki/Associative_array/Merging Cl... | ✅ |
| 578 | `rosetta_atomic_updates.obs` | Other | Rosetta Code: Atomic updates https://rosettacode.org/wiki/Atomic_updates EXTRA_LIBS: gen_collect... | ✅ |
| 579 | `rosetta_balanced_ternary.obs` | Other | Rosetta Code: Balanced ternary https://rosettacode.org/wiki/Balanced_ternary Digits are -1, 0 and... | ✅ |
| 580 | `rosetta_bell_numbers.obs` | Other | Rosetta Code: Bell numbers https://rosettacode.org/wiki/Bell_numbers The number of ways to partit... | ✅ |
| 581 | `rosetta_bernoulli_numbers.obs` | Other | Rosetta Code: Bernoulli numbers https://rosettacode.org/wiki/Bernoulli_numbers Claimed in #1166 E... | ✅ |
| 582 | `rosetta_brace_expansion.obs` | Other | Rosetta Code: Brace expansion https://rosettacode.org/wiki/Brace_expansion Claimed in #1143 Shell... | ✅ |
| 583 | `rosetta_camel_case_and_snake_case.obs` | Other | Rosetta Code: Camel case and snake case https://rosettacode.org/wiki/Camel_case_and_snake_case Cl... | ✅ |
| 584 | `rosetta_cartesian_product.obs` | Other | Rosetta Code: Cartesian product of two or more lists https://rosettacode.org/wiki/Cartesian_produ... | ✅ |
| 585 | `rosetta_catalan_numbers.obs` | Other | Rosetta Code: Catalan numbers https://rosettacode.org/wiki/Catalan_numbers C(n) = C(2n,n)/(n+1).... | ✅ |
| 586 | `rosetta_checkpoint_synchronization.obs` | Other | Rosetta Code: Checkpoint synchronization https://rosettacode.org/wiki/Checkpoint_synchronization... | ✅ |
| 587 | `rosetta_church_numerals.obs` | Other | Rosetta Code: Church numerals https://rosettacode.org/wiki/Church_numerals Claimed in #1122 A Chu... | ✅ |
| 588 | `rosetta_circular_primes.obs` | Other | Rosetta Code: Circular primes https://rosettacode.org/wiki/Circular_primes Every rotation of the... | ✅ |
| 589 | `rosetta_collapsible.obs` | Other | Rosetta Code: Determine if a string is collapsible https://rosettacode.org/wiki/Determine_if_a_st... | ✅ |
| 590 | `rosetta_combinations.obs` | Other | Rosetta Code: Combinations https://rosettacode.org/wiki/Combinations The k-subsets of 0..n-1 in a... | ✅ |
| 591 | `rosetta_convert_seconds_to_compound_duration.obs` | Other | Rosetta Code: Convert seconds to compound duration https://rosettacode.org/wiki/Convert_seconds_t... | ✅ |
| 592 | `rosetta_count_substring.obs` | Other | Rosetta Code: Count occurrences of a substring https://rosettacode.org/wiki/Count_occurrences_of_... | ✅ |
| 593 | `rosetta_cuban_primes.obs` | Other | Rosetta Code: Cuban primes https://rosettacode.org/wiki/Cuban_primes Primes of the form (n+1)^3 -... | ✅ |
| 594 | `rosetta_currying.obs` | Other | Rosetta Code: Currying https://rosettacode.org/wiki/Currying Claimed in #1118 A three-argument ad... | ✅ |
| 595 | `rosetta_date_manipulation.obs` | Other | Rosetta Code: Date manipulation https://rosettacode.org/wiki/Date_manipulation Claimed in #1157 "... | ✅ |
| 596 | `rosetta_day_of_the_week.obs` | Other | Rosetta Code: Day of the week https://rosettacode.org/wiki/Day_of_the_week Claimed in #1154 The y... | ✅ |
| 597 | `rosetta_deepcopy.obs` | Other | Rosetta Code: Deepcopy https://rosettacode.org/wiki/Deepcopy Claimed in #1134 EXTRA_LIBS: gen_col... | ✅ |
| 598 | `rosetta_detect_division_by_zero.obs` | Other | Rosetta Code: Detect division by zero https://rosettacode.org/wiki/Detect_division_by_zero Claime... | ✅ |
| 599 | `rosetta_digital_root.obs` | Other | Rosetta Code: Digital root https://rosettacode.org/wiki/Digital_root The additive persistence is... | ✅ |
| 600 | `rosetta_emirp_primes.obs` | Other | Rosetta Code: Emirp primes https://rosettacode.org/wiki/Emirp_primes A prime whose digit reversal... | ✅ |
| 601 | `rosetta_enforced_immutability.obs` | Other | Rosetta Code: Enforced immutability https://rosettacode.org/wiki/Enforced_immutability Claimed in... | ✅ |
| 602 | `rosetta_esthetic.obs` | Other | Rosetta Code: Esthetic numbers https://rosettacode.org/wiki/Esthetic_numbers Adjacent digits of a... | ✅ |
| 603 | `rosetta_exceptions.obs` | Other | Rosetta Code: Exceptions https://rosettacode.org/wiki/Exceptions Claimed in #1123 Define an excep... | ✅ |
| 604 | `rosetta_exceptions_catch_an_exception_thrown_in_a_nested_call.obs` | Other | Rosetta Code: Exceptions/Catch an exception thrown in a nested call https://rosettacode.org/wiki/... | ✅ |
| 605 | `rosetta_extreme_floating_point_values.obs` | Other | Rosetta Code: Extreme floating point values https://rosettacode.org/wiki/Extreme_floating_point_v... | ✅ |
| 606 | `rosetta_fast_fourier_transform.obs` | Other | Rosetta Code: Fast Fourier transform https://rosettacode.org/wiki/Fast_Fourier_transform Claimed... | ✅ |
| 607 | `rosetta_flatten_a_list.obs` | Other | Rosetta Code: Flatten a list https://rosettacode.org/wiki/Flatten_a_list Arbitrarily nested lists... | ✅ |
| 608 | `rosetta_formatted_numeric_output.obs` | Other | Rosetta Code: Formatted numeric output https://rosettacode.org/wiki/Formatted_numeric_output Clai... | ✅ |
| 609 | `rosetta_fusc_sequence.obs` | Other | Rosetta Code: Fusc sequence https://rosettacode.org/wiki/Fusc_sequence fusc(2n) = fusc(n) and fus... | ✅ |
| 610 | `rosetta_gapful.obs` | Other | Rosetta Code: Gapful numbers https://rosettacode.org/wiki/Gapful_numbers A gapful number is divis... | ✅ |
| 611 | `rosetta_gaussian_elimination.obs` | Other | Rosetta Code: Gaussian elimination https://rosettacode.org/wiki/Gaussian_elimination Claimed in #... | ✅ |
| 612 | `rosetta_generic_swap.obs` | Other | Rosetta Code: Generic swap https://rosettacode.org/wiki/Generic_swap Claimed in #1131 Objeck pass... | ✅ |
| 613 | `rosetta_gray_code.obs` | Other | Rosetta Code: Gray code https://rosettacode.org/wiki/Gray_code Encode with n XOR (n >> 1) and dec... | ✅ |
| 614 | `rosetta_hailstone.obs` | Other | Rosetta Code: Hailstone sequence https://rosettacode.org/wiki/Hailstone_sequence The sequence for... | ✅ |
| 615 | `rosetta_hash_join.obs` | Other | Rosetta Code: Hash join https://rosettacode.org/wiki/Hash_join Claimed in #1151 EXTRA_LIBS: gen_c... | ✅ |
| 616 | `rosetta_hex_dump.obs` | Other | Rosetta Code: Hex dump https://rosettacode.org/wiki/Hex_dump Claimed in #1158 hexdump -C's canoni... | ✅ |
| 617 | `rosetta_huffman_coding.obs` | Other | Rosetta Code: Huffman coding https://rosettacode.org/wiki/Huffman_coding Claimed in #1152 EXTRA_L... | ✅ |
| 618 | `rosetta_infinity.obs` | Other | Rosetta Code: Infinity https://rosettacode.org/wiki/Infinity Claimed in #1129 Float is an IEEE do... | ✅ |
| 619 | `rosetta_integer_overflow.obs` | Other | Rosetta Code: Integer overflow https://rosettacode.org/wiki/Integer_overflow Claimed in #1125 Obj... | ✅ |
| 620 | `rosetta_isqrt.obs` | Other | Rosetta Code: Isqrt (integer square root) of X https://rosettacode.org/wiki/Isqrt_(integer_square... | ✅ |
| 621 | `rosetta_jacobsthal_numbers.obs` | Other | Rosetta Code: Jacobsthal numbers https://rosettacode.org/wiki/Jacobsthal_numbers J(n) = J(n-1) +... | ✅ |
| 622 | `rosetta_kaprekar.obs` | Other | Rosetta Code: Kaprekar numbers https://rosettacode.org/wiki/Kaprekar_numbers A Kaprekar number's... | ✅ |
| 623 | `rosetta_largest_proper_divisor.obs` | Other | Rosetta Code: Largest proper divisor of n https://rosettacode.org/wiki/Largest_proper_divisor_of_... | ✅ |
| 624 | `rosetta_last_friday_of_each_month.obs` | Other | Rosetta Code: Last Friday of each month https://rosettacode.org/wiki/Last_Friday_of_each_month Cl... | ✅ |
| 625 | `rosetta_leonardo_numbers.obs` | Other | Rosetta Code: Leonardo numbers https://rosettacode.org/wiki/Leonardo_numbers L(n) = L(n-1) + L(n-... | ✅ |
| 626 | `rosetta_longest_common_subsequence.obs` | Other | Rosetta Code: Longest common subsequence https://rosettacode.org/wiki/Longest_common_subsequence... | ✅ |
| 627 | `rosetta_look_and_say.obs` | Other | Rosetta Code: Look-and-say sequence https://rosettacode.org/wiki/Look-and-say_sequence Each term... | ✅ |
| 628 | `rosetta_mertens.obs` | Other | Rosetta Code: Mertens function https://rosettacode.org/wiki/Mertens_function The running sum of t... | ✅ |
| 629 | `rosetta_metered_concurrency.obs` | Other | Rosetta Code: Metered concurrency https://rosettacode.org/wiki/Metered_concurrency Claimed in #11... | ✅ |
| 630 | `rosetta_mobius.obs` | Other | Rosetta Code: Möbius function https://rosettacode.org/wiki/M%C3%B6bius_function 0 when a square d... | ✅ |
| 631 | `rosetta_modular_exponentiation.obs` | Other | Rosetta Code: Modular exponentiation https://rosettacode.org/wiki/Modular_exponentiation EXTRA_LI... | ✅ |
| 632 | `rosetta_modular_inverse.obs` | Other | Rosetta Code: Modular inverse https://rosettacode.org/wiki/Modular_inverse The extended Euclidean... | ✅ |
| 633 | `rosetta_motzkin_numbers.obs` | Other | Rosetta Code: Motzkin numbers https://rosettacode.org/wiki/Motzkin_numbers M(n) = M(n-1)*(2n+1)/(... | ✅ |
| 634 | `rosetta_munchausen.obs` | Other | Rosetta Code: Munchausen numbers https://rosettacode.org/wiki/Munchausen_numbers A Munchausen num... | ✅ |
| 635 | `rosetta_natural_sorting.obs` | Other | Rosetta Code: Natural sorting https://rosettacode.org/wiki/Natural_sorting Claimed in #1139 The t... | ✅ |
| 636 | `rosetta_negative_base_numbers.obs` | Other | Rosetta Code: Negative base numbers https://rosettacode.org/wiki/Negative_base_numbers A negative... | ✅ |
| 637 | `rosetta_optional_parameters.obs` | Other | Rosetta Code: Optional parameters https://rosettacode.org/wiki/Optional_parameters Claimed in #11... | ✅ |
| 638 | `rosetta_partial_function_application.obs` | Other | Rosetta Code: Partial function application https://rosettacode.org/wiki/Partial_function_applicat... | ✅ |
| 639 | `rosetta_pascals_triangle.obs` | Other | Rosetta Code: Pascal's triangle https://rosettacode.org/wiki/Pascal%27s_triangle Each entry is th... | ✅ |
| 640 | `rosetta_pell_numbers.obs` | Other | Rosetta Code: Pell numbers https://rosettacode.org/wiki/Pell_numbers P(n) = 2*P(n-1) + P(n-2). Th... | ✅ |
| 641 | `rosetta_permutations.obs` | Other | Rosetta Code: Permutations https://rosettacode.org/wiki/Permutations All n! orderings, in lexicog... | ✅ |
| 642 | `rosetta_pernicious.obs` | Other | Rosetta Code: Pernicious numbers https://rosettacode.org/wiki/Pernicious_numbers A pernicious num... | ✅ |
| 643 | `rosetta_population_count.obs` | Other | Rosetta Code: Population count https://rosettacode.org/wiki/Population_count The count of set bit... | ✅ |
| 644 | `rosetta_power_set.obs` | Other | Rosetta Code: Power set https://rosettacode.org/wiki/Power_set All 2^n subsets, enumerated by tre... | ✅ |
| 645 | `rosetta_priority_queue.obs` | Other | Rosetta Code: Priority queue https://rosettacode.org/wiki/Priority_queue Claimed in #1146 EXTRA_L... | ✅ |
| 646 | `rosetta_range_expansion.obs` | Other | Rosetta Code: Range expansion https://rosettacode.org/wiki/Range_expansion Claimed in #1144 Expan... | ✅ |
| 647 | `rosetta_read_a_specific_line_from_a_file.obs` | Other | Rosetta Code: Read a specific line from a file https://rosettacode.org/wiki/Read_a_specific_line_... | ✅ |
| 648 | `rosetta_reflection_list_methods.obs` | Other | Rosetta Code: Reflection/List methods https://rosettacode.org/wiki/Reflection/List_methods Claime... | ✅ |
| 649 | `rosetta_remove_lines_from_a_file.obs` | Other | Rosetta Code: Remove lines from a file https://rosettacode.org/wiki/Remove_lines_from_a_file Clai... | ✅ |
| 650 | `rosetta_same_characters.obs` | Other | Rosetta Code: Determine if a string has all the same characters https://rosettacode.org/wiki/Dete... | ✅ |
| 651 | `rosetta_set.obs` | Other | Rosetta Code: Set https://rosettacode.org/wiki/Set Claimed in #1148 EXTRA_LIBS: gen_collect Creat... | ✅ |
| 652 | `rosetta_sieve.obs` | Other | Rosetta Code: Sieve of Eratosthenes https://rosettacode.org/wiki/Sieve_of_Eratosthenes The sieve... | ✅ |
| 653 | `rosetta_square_but_not_cube.obs` | Other | Rosetta Code: Square but not cube https://rosettacode.org/wiki/Square_but_not_cube The squares th... | ✅ |
| 654 | `rosetta_squeezable.obs` | Other | Rosetta Code: Determine if a string is squeezable https://rosettacode.org/wiki/Determine_if_a_str... | ✅ |
| 655 | `rosetta_stern_brocot.obs` | Other | Rosetta Code: Stern-Brocot sequence https://rosettacode.org/wiki/Stern-Brocot_sequence Built by a... | ✅ |
| 656 | `rosetta_symmetric_difference.obs` | Other | Rosetta Code: Symmetric difference https://rosettacode.org/wiki/Symmetric_difference Claimed in #... | ✅ |
| 657 | `rosetta_tau_function.obs` | Other | Rosetta Code: Tau function https://rosettacode.org/wiki/Tau_function The count of a number's divi... | ✅ |
| 658 | `rosetta_tokenize_a_string_with_escaping.obs` | Other | Rosetta Code: Tokenize a string with escaping https://rosettacode.org/wiki/Tokenize_a_string_with... | ✅ |
| 659 | `rosetta_topological_sort.obs` | Other | Rosetta Code: Topological sort https://rosettacode.org/wiki/Topological_sort Claimed in #1147 EXT... | ✅ |
| 660 | `rosetta_totient.obs` | Other | Rosetta Code: Totient function https://rosettacode.org/wiki/Totient_function Euler's totient, cou... | ✅ |
| 661 | `rosetta_truncatable_primes.obs` | Other | Rosetta Code: Truncatable primes https://rosettacode.org/wiki/Truncatable_primes Left- and right-... | ✅ |
| 662 | `rosetta_twos_complement.obs` | Other | Rosetta Code: Two's complement https://rosettacode.org/wiki/Two%27s_complement Claimed in #1126 T... | ✅ |
| 663 | `rosetta_unique_characters.obs` | Other | Rosetta Code: Determine if a string has all unique characters https://rosettacode.org/wiki/Determ... | ✅ |
| 664 | `rosetta_utf8_encode_and_decode.obs` | Other | Rosetta Code: UTF-8 encode and decode https://rosettacode.org/wiki/UTF-8_encode_and_decode Claime... | ✅ |
| 665 | `rosetta_van_eck.obs` | Other | Rosetta Code: Van Eck sequence https://rosettacode.org/wiki/Van_Eck_sequence Each term is the gap... | ✅ |
| 666 | `rosetta_variadic_function.obs` | Other | Rosetta Code: Variadic function https://rosettacode.org/wiki/Variadic_function Claimed in #1132 O... | ✅ |
| 667 | `rosetta_word_wrap.obs` | Other | Rosetta Code: Word wrap https://rosettacode.org/wiki/Word_wrap Claimed in #1138 The minimum-lengt... | ✅ |
| 668 | `rosetta_y_combinator.obs` | Other | Rosetta Code: Y combinator https://rosettacode.org/wiki/Y_combinator Claimed in #1120 The applica... | ✅ |
| 669 | `rosetta_zeckendorf.obs` | Other | Rosetta Code: Zeckendorf number representation https://rosettacode.org/wiki/Zeckendorf_number_rep... | ✅ |
| 670 | `rosetta_zero_to_the_zero_power.obs` | Other | Rosetta Code: Zero to the zero power https://rosettacode.org/wiki/Zero_to_the_zero_power Claimed... | ✅ |
| 671 | `runtime_feature_test.obs` | Other | Regression tests for the "runtime.feature.*" properties, which report which optional protocol eng... | ✅ |
| 672 | `runtime_gc_stats.obs` | Other | The runtime.* GC statistics must stay inside their own stated ranges. runtime.gc.nursery.occupanc... | ✅ |
| 673 | `sdl2_bindings.obs` | Other | Regression test for the Game.SDL2 bindings added to close the 2026-10 gap survey: proves each new... | ✅ |
| 674 | `select_dispatch_test.obs` | Control Flow | Single-case, linear (2-5 cases), jump-table (dense >=6), and binary-tree (sparse) paths | ✅ |
| 675 | `self_typeof_as_chain.obs` | Other | `@self->TypeOf(T)` and `@self->As(T)->M()` (found adding Base->ToString, #1248). EmitVariable ret... | ✅ |
| 676 | `serial_nil_array_element.obs` | Other | A Nil element inside a serialized object array must come back as Nil in that array, and must not... | ✅ |
| 677 | `serialize_float_array_type.obs` | Other | A Float[] field read back by the Deserializer is a Float[] to the VM. ObjectDeserializer::Deseria... | ✅ |
| 678 | `serializer_bool_array.obs` | Other | Serializer->Write(Bool[]) and Deserializer->ReadBoolArray(). A Bool[] is a byte array, one byte p... | ✅ |
| 679 | `serializer_multidim_arrays.obs` | Other | Regression: the Serializer dropped the tail of multi-dimensional arrays (#1174). An array's heade... | ✅ |
| 680 | `server_bind_address.obs` | Other | A server can listen on one address instead of every interface TCPSocketServer->New(port) binds th... | ✅ |
| 681 | `server_listen_error.obs` | Other | A server that cannot listen reports why When a server's port was already taken, Listen failed but... | ✅ |
| 682 | `server_port_readback.obs` | Other | A server bound to port 0 reports the port the system chose The regression tests used to listen on... | ✅ |
| 683 | `socket_accept_class.obs` | Other | An accepted socket is the kind of socket its server makes The VM caches the class ids it allocate... | ✅ |
| 684 | `socket_graceful_close_test.obs` | Other | TCPSocket->CloseGracefully() must not lose the data it just wrote (#669). The shape this guards i... | ✅ |
| 685 | `sort_primitive_arrays.obs` | Other | Primitive array sorting: Int, Float, Char and Byte. Int->Sort and its Byte, Char and Float counte... | ✅ |
| 686 | `string_concat_chained_call.obs` | Strings | Regression: '+' concatenation with a chained method call appended the wrong overload. Found by th... | ✅ |
| 687 | `string_concat_nesting.obs` | Strings | Nested string concatenation. The compiler lowers a concatenation to "allocate a System.String, st... | ✅ |
| 688 | `string_concat_receiver_and_interp_index.obs` | Strings | Regression: a call on a parenthesised concatenation, and an indexed array in {$...}. Found by the... | ✅ |
| 689 | `string_edge_cases.obs` | Strings | Regression: String library edge cases (#1177), checked against Python. - Split(String) completed... | ✅ |
| 690 | `string_find_ops.obs` | Strings | string find ops | ✅ |
| 691 | `string_format_ops.obs` | Strings | Verifies String->Format() positional substitution. | ✅ |
| 692 | `string_interp_concat.obs` | Strings | An interpolated string as the LEFT operand of a concatenation. "{$a}" + "{$b}" printed AAB. The c... | ✅ |
| 693 | `string_literal_receiver_nested_call.obs` | Strings | A method call whose receiver is already on the stack when its arguments are emitted -- a string l... | ✅ |
| 694 | `string_number_conv.obs` | Strings | string number conv | ✅ |
| 695 | `string_replace_ops.obs` | Strings | string replace ops | ✅ |
| 696 | `string_split_ops.obs` | Strings | string split ops | ✅ |
| 697 | `string_wrap_center_split.obs` | Strings | String additions from #1186: SplitWhitespace, Split(delim, escape), Center, Wrap and WrapLines, a... | ✅ |
| 698 | `sys_env_command_vars.obs` | Other | Runtime->CommandOutput(command, variables) runs the command with the variables set and puts the e... | ✅ |
| 699 | `task_scope.obs` | Other | Regression for a structured-concurrency nursery (TaskScope) built purely on the existing System.C... | ✅ |
| 700 | `tco_receiver.obs` | Other | DIFF_CONFIGS: s3 reason: Deep->Down(50000) needs the s3 tail-call rewrite; at s0 it overflows the... | ✅ |
| 701 | `thread_accept_exit_test.obs` | Other | A thread parked in accept() must not take the VM down when Main returns (#681). The shape: one th... | ✅ |
| 702 | `time_strings_code_points.obs` | Other | Regression: Date strings (#1179, library half) and code points above U+FFFF (#1185). - Date->GetT... | ✅ |
| 703 | `time_thread_safe.obs` | Other | Dates and file times read from several threads at once. Eight threads each move the same date (1... | ✅ |
| 704 | `time_timer_wall_clock.obs` | Other | System.Time.Timer measures wall-clock time on every platform. The VM implemented the timer traps... | ✅ |
| 705 | `tls_connect_gc_park.obs` | Other | TLS and HTTP/2 connects must park for garbage collection A stop-the-world collection waits until... | ✅ |
| 706 | `tls_errors_and_verify.obs` | Other | TLS failures are reported, and verification cannot be switched off per socket TCPSecureSocket->Ge... | ✅ |
| 707 | `tls_ip_san.obs` | Other | A client connecting by IP address verifies a certificate that names it A certificate names an IP... | ✅ |
| 708 | `tls_server_cert_names.obs` | Other | TCPSecureSocketServer->GetIssuer and GetSubject report the server's own certificate The socket an... | ✅ |
| 709 | `tls_verify_test.obs` | Other | TLS certificate verification must REFUSE. https_persistence_test proves the happy path (a pinned... | ✅ |
| 710 | `tls_write_byte_result.obs` | Other | TCPSecureSocket->WriteByte and DTLSSocket->WriteByte say whether they wrote Both are documented "... | ✅ |
| 711 | `trap_array_barrier_test.obs` | Other | A String[] returned by a VM trap must survive a collection. Every trap that returns an array of o... | ✅ |
| 712 | `trap_array_mt_barrier_test.obs` | Other | A trap-returned array must survive ANOTHER THREAD's allocation. trap_array_barrier_test.obs cover... | ✅ |
| 713 | `try_blocks.obs` | Exceptions | try / on / finally blocks (#1247, phases 2 and 3; docs/ERROR_HANDLING_DESIGN.md). try { ... } on(... | ✅ |
| 714 | `try_chain_inline_compare.obs` | Exceptions | A recovered '?->' value compares the same way inline as it does through a local. 1067. The chain... | ✅ |
| 715 | `try_otherwise.obs` | Exceptions | Try/Otherwise Error Handling Test Tests the Try() and Otherwise() intrinsic methods for error han... | ✅ |
| 716 | `try_recovers_bounds_mid_args.obs` | Exceptions | Regression: a recovered array-bounds error corrupted the operand stack when the caller had alread... | ✅ |
| 717 | `try_recovers_cast.obs` | Exceptions | Try() -- and '?->', which is Try() on the chain -- recovers from an invalid object cast, whether... | ✅ |
| 718 | `try_recovers_depth.obs` | Exceptions | Try() -- and '?->', which is Try() on the chain -- recovers from a full call stack, as it does fr... | ✅ |
| 719 | `try_recovers_jit_call.obs` | Exceptions | Try() -- and '?->', which is Try() on the chain -- recovers when a COMPILED method calls a COMPIL... | ✅ |
| 720 | `try_recovery_stack_balance.obs` | Exceptions | Regression: `?->` recovery left the operand stack unbalanced, and `?->` after a call result or a... | ✅ |
| 721 | `udp_departed_client.obs` | Other | A UDP server outlives a departed client; a client hears its peer's refusal A reply to a client th... | ✅ |
| 722 | `udp_failover.obs` | Other | A UDP client tries each address its name resolves to UDPSocket->New used the first address a name... | ✅ |
| 723 | `udp_host_names.obs` | Other | A UDP socket takes a host name or an IPv6 address, and a UDP server hears both UDPSocket->New(add... | ✅ |
| 724 | `unary_minus_parenthesised.obs` | Other | Regression: unary minus on a parenthesised operand (#1099). `-(x)`, `-(F())` and `-(3 + 4)` repor... | ✅ |
| 725 | `unsigned_literals.obs` | Other | Unsigned integer literals: the 'u'/'U' suffix, and hex/binary read as bit patterns. The suffix ch... | ✅ |
| 726 | `unsigned_ops.obs` | Other | The '>>>' operator and the unsigned helpers on Int. Objeck stores every integer in a signed 64-bi... | ✅ |
| 727 | `url_encode_utf8.obs` | Other | url encode utf8 | ✅ |
| 728 | `vm_call_depth_limit.obs` | Other | Fixture for run_vm_flag_tests.py section 16 (--call-depth and OBJECK_CALL_DEPTH). With arguments,... | ✅ |
| 729 | `vm_callback_depth.obs` | Other | A call back into Objeck from inside the VM's C++ runs on the thread's own call stack, bounded lik... | ✅ |
| 730 | `vm_error_exit.obs` | Other (neg) | A program that dies inside the VM must leave obr with a non-zero exit status. Execute (core/vm/vm... | ✅ |
| 731 | `vm_gc_verify_inject.obs` | Other | Fixture for the heap verifier (OBJECK_GC_VERIFY, core/vm/arch/memory_verify.cpp). On its own it i... | ✅ |
| 732 | `vm_jit_equiv.obs` | Other | The interpreter and the JIT must agree. This program is run twice by run_vm_flag_tests.py -- once... | ✅ |
| 733 | `vm_lib_path_native.obs` | Other | Fixture for run_vm_flag_tests.py: --lib-path must reach the VM's native-library loader. SHA256 is... | ✅ |
| 734 | `vm_locale_wide.obs` | Other | Fixture for run_vm_flag_tests.py: obr must run, and write wide characters as UTF-8, under a local... | ✅ |
| 735 | `vm_parse_ranges.obs` | Other | A float-formatting precision that is not an int is refused where it is set, with an error a ?-> h... | ✅ |
| 736 | `vm_set_locale_refused.obs` | Other | Runtime->SetLocale with a name the system cannot supply. The VM switched the C library's locale a... | ✅ |
| 737 | `vm_set_property_first.obs` | Other | A program whose first property access is a set still gets the runtime's own properties. The runti... | ✅ |
| 738 | `vm_set_property_overwrite.obs` | Other | A runtime property set twice reads back the second value. StackProgram::SetProperty stored with s... | ✅ |
| 739 | `vm_thread_stack_pages.obs` | Other | Fixture for run_vm_flag_tests.py section 19: transparent huge pages stay off the stacks of the th... | ✅ |
| 740 | `vm_trace_fn_param_format.obs` | Other (neg) | The stack trace names each method the way its source declares it, including function-typed parame... | ✅ |
| 741 | `vm_unicode_args_env.obs` | Other | Fixture for run_vm_flag_tests.py (section 14): arguments, environment variables and command outpu... | ✅ |
| 742 | `vm_write_char_buffer.obs` | Other | Console->WriteBuffer(Char[]) wrote the buffer twice-encoded, and ignored num. The trap (STD_OUT_C... | ✅ |
| 743 | `web_server_test.obs` | Other | Web.Server end-to-end coverage. Every method on Web.Server.Request and Response used to call a na... | ✅ |
| 744 | `websocket_test.obs` | Networking | websocket test | ✅ |
| 745 | `xml_attribute_order.obs` | XML | XmlElement serializes its attributes in the order they were added, and a parsed element keeps doc... | ✅ |
| 746 | `xml_build_ops.obs` | XML | xml build ops | ✅ |
| 747 | `xml_encoding_ops.obs` | XML | Unit tests for the 2026-06 Data.XML improvements: truncated/garbage input is rejected (previously... | ✅ |
| 748 | `xml_parse_ops.obs` | XML | xml parse ops | ✅ |

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
