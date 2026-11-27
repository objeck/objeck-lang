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


**Total runtime tests: 1219** (plus 14 debugger tests, see below).


## Tests by Category

| Category | Count |
|----------|-------|
| Other | 491 |
| Negative | 239 |
| System.ML | 100 |
| Core Language | 60 |
| AMD64/JIT | 56 |
| Query.Structured | 51 |
| Collections | 27 |
| System.NLP | 21 |
| MCP Server | 16 |
| JSON | 15 |
| Generics (neg) | 14 |
| Strings | 14 |
| Bug Fix | 13 |
| Math | 9 |
| Debugger | 8 |
| Exceptions | 8 |
| AMD64/JIT (neg) | 7 |
| I/O | 7 |
| Regex | 7 |
| API (network) | 6 |
| Generics | 6 |
| XML | 6 |
| ARM64 JIT | 5 |
| Control Flow | 5 |
| Functional | 5 |
| System.AI | 5 |
| Networking | 4 |
| Other (neg) | 4 |
| Date/Time | 3 |
| ODBC | 3 |
| Concurrency/GC | 1 |
| Core Language (neg) | 1 |
| Debugger (neg) | 1 |
| LSP | 1 |

## Test Inventory

| # | Test | Category | What it validates | Status |
|---|------|----------|-------------------|--------|
| 1 | `actor_agents_test.obs` | Other | Agents as actors (actor_agents): an API.Agent agent behind a ScriptedBackend, so no key and no ne... | ✅ |
| 2 | `actor_core_test.obs` | Other | System.Actor core: exit reasons (normal, stopped, failed by a raised error and by each runtime fa... | ✅ |
| 3 | `actor_mailbox_stress_test.obs` | Other | Eight producer threads and one consumer through a Mailbox and through an Actor: every message arr... | ✅ |
| 4 | `actor_mailbox_test.obs` | Other | System.Actor Mailbox: FIFO order, bounded queues with the two overflow policies (BLOCK with a tim... | ✅ |
| 5 | `actor_supervisor_test.obs` | Other | System.Actor Supervisor: ONE_FOR_ONE, ONE_FOR_ALL and REST_FOR_ONE each with a scripted failing c... | ✅ |
| 6 | `agent_backend_test.obs` | Other | The four provider adapters of API.Agent, run against recorded response bodies and the requests th... | ✅ |
| 7 | `agent_cancel_shared_parents_test.obs` | Other | AgentCancel with two parents (API.Agent), against a scripted fake model (no key, no network). A s... | ✅ |
| 8 | `agent_control_test.obs` | Other | Run control for API.Agent, against a scripted fake model (no key, no network): the cancellation t... | ✅ |
| 9 | `agent_decl_test.obs` | Other | Agent declarations (docs/design/AGENT_SYNTAX.md, section 3): 'agent Name(...) { backend : ...; ..... | ✅ |
| 10 | `agent_http_test.obs` | Other | The agent's provider adapters over the wire, against a local HTTPS server that answers in the Ant... | ✅ |
| 11 | `agent_loop_test.obs` | Other | The agent loop (API.Agent) against a scripted fake model, so no key and no network are needed: a... | ✅ |
| 12 | `agent_scope_bounded_worker_test.obs` | Other | An AgentScope reaches the worker thread of a bounded tool or model call (API.Agent), against scri... | ✅ |
| 13 | `agent_scope_outlived_parent_test.obs` | Other | A child scope that outlives the scope above it keeps the limits further up (API.Agent), against a... | ✅ |
| 14 | `agent_scope_test.obs` | Other | AgentScope (API.Agent): limits for every agent run inside a block, against a scripted fake model... | ✅ |
| 15 | `agent_scope_threads_test.obs` | Other | AgentScope and Redactor under threads (API.Agent), against scripted fake models (no key, no netwo... | ✅ |
| 16 | `agent_trace_buffer_threads_test.obs` | Other | AgentTraceBuffer under threads (API.Agent). A scope with SetTrace(true) gives one buffer to every... | ✅ |
| 17 | `agent_worker_release_test.obs` | Other | A bounded worker is not lost when the wait for it ends in an error (API.Agent), against a scripte... | ✅ |
| 18 | `ai_game_test.obs` | System.AI | Regression tests for System.AI adversarial search using tic-tac-toe: Minimax takes a win-in-1, bl... | ✅ |
| 19 | `ai_hub_client.obs` | System.AI | HubClient: Hugging Face Hub downloads into a verified local cache Two in-process servers stand in... | ✅ |
| 20 | `ai_optimize_test.obs` | System.AI | Regression tests for System.AI optimization: GeneticAlgorithm solves OneMax (all-ones chromosome)... | ✅ |
| 21 | `ai_rl_test.obs` | System.AI | Regression tests for System.AI reinforcement learning using a 6-state chain world (move right to... | ✅ |
| 22 | `ai_search_test.obs` | System.AI | Regression tests for System.AI graph search: Dijkstra finds the minimum-cost path even when a dir... | ✅ |
| 23 | `anthropic_http_test.obs` | Other | The Anthropic client over the wire, against a local HTTPS server that speaks the Messages API sha... | ✅ |
| 24 | `anthropic_parse_test.obs` | Other | The Anthropic client's response handling, request building and stream assembly, run against recor... | ✅ |
| 25 | `api_gemini_test.obs` | API (network) | api gemini test | ✅ |
| 26 | `api_key_insecure_tls_anthropic.obs` | API (network) | Anthropic client's API key is not handed out while TLS certificate checking is off. OBJECK_TLS_IN... | ✅ |
| 27 | `api_key_insecure_tls_gemini.obs` | API (network) | Gemini client's API key is not handed out while TLS certificate checking is off. OBJECK_TLS_INSEC... | ✅ |
| 28 | `api_key_insecure_tls_openai.obs` | API (network) | OpenAI client's API key is not handed out while TLS certificate checking is off. OBJECK_TLS_INSEC... | ✅ |
| 29 | `api_ollama_test.obs` | API (network) | api ollama test | ✅ |
| 30 | `api_openai_test.obs` | API (network) | api openai test | ✅ |
| 31 | `arm64_bitwise.obs` | ARM64 JIT | ARM64 Bitwise Operations Test Tests bitwise AND, OR, XOR operations | ✅ |
| 32 | `arm64_char_arrays.obs` | ARM64 JIT | ARM64 Character Array Test Tests STRH/LDRH instruction encoding (16-bit operations) | ✅ |
| 33 | `arm64_imm64_ops.obs` | ARM64 JIT | 64-bit immediate operands in the ARM64 JIT. Windows is LLP64, so 'long' is 32 bits there while Li... | ✅ |
| 34 | `arm64_large_immediates.obs` | ARM64 JIT | ARM64 Large Immediate Test Tests add_imm_reg/sub_imm_reg with values > 4095 | ✅ |
| 35 | `arm64_multiply_constants.obs` | ARM64 JIT | ARM64 Multiply-by-Constant Test Tests multiply-by-constant optimization (especially multiply-by-6) | ✅ |
| 36 | `array_index_nested.obs` | Other | An array index that is itself a subscript. Every index was analyzed twice by the compiler, so 'a[... | ✅ |
| 37 | `array_index_nested_call_chain.obs` | Other | An array index that is both a subscript and a call chain. Two fixes changed how the compiler read... | ✅ |
| 38 | `array_index_scalar_forms.obs` | Other | Every way one value is written as an index still is one, now that a whole array is refused as an... | ✅ |
| 39 | `array_literal_dims.obs` | Other | Multi-dimensional array literals have the shape they are written in. A String literal's dimension... | ✅ |
| 40 | `array_literal_expressions.obs` | Other | Array literals with expressions (#1245; docs/ARRAY_LITERALS_DESIGN.md). A literal used to accept... | ✅ |
| 41 | `array_receiver_order.obs` | Other | A call written on a whole array, 'a->Fill(7)', is a static function on the element type's hidden... | ✅ |
| 42 | `assert_statement.obs` | Other | assert statement: 'assert E : "label";' (docs/design/ASSERT_STATEMENT.md). Every assertion here h... | ✅ |
| 43 | `bad_agent_backend_type.obs` | Negative | A value of an agent declaration is checked where it is written: a backend that is not a ChatBacke... | ✅ |
| 44 | `bad_agent_budget_limit.obs` | Negative | A limit a budget does not have is a compile error at the limit, and the message lists the limits... | ✅ |
| 45 | `bad_agent_no_backend.obs` | Negative | An agent declaration must say which model the agent talks to: one without a backend is a compile... | ✅ |
| 46 | `bad_agent_tool_class.obs` | Negative | A class named in the tools of an agent declaration must have a function marked as a tool. The cla... | ✅ |
| 47 | `bad_agent_unknown_setting.obs` | Negative | A setting an agent declaration does not have is a compile error at the setting, and the message l... | ✅ |
| 48 | `bad_alias_type_incomplete.obs` | Negative | A type that starts with a backslash is an alias, '\Group->Name'. With the '->Name' left out no ty... | ✅ |
| 49 | `bad_ambiguous_bundle_class_enum.obs` | Negative | Two classes of one bundle each declare 'enum Mood'; a third class of the bundle writes 'Mood'. It... | ✅ |
| 50 | `bad_ambiguous_use_alias.obs` | Negative | Two bundles in scope both declare 'alias Funcs'. An alias was named without its bundle, so the tw... | ✅ |
| 51 | `bad_ambiguous_use_alias_member.obs` | Negative | An alias's member type names a class two used bundles both declare. The name was copied to each p... | ✅ |
| 52 | `bad_ambiguous_use_class.obs` | Negative | Two bundles in scope both declare 'class Shape'. The unqualified name silently resolved to whiche... | ✅ |
| 53 | `bad_ambiguous_use_enum.obs` | Negative | Two bundles in scope both declare 'enum Tint'. The unqualified name silently resolved to whicheve... | ✅ |
| 54 | `bad_ambiguous_use_lib_program.obs` | Negative | A program bundle and a library bundle, both 'use'd, each declare 'JsonElement'. The program's cla... | ✅ |
| 55 | `bad_ambiguous_use_parent.obs` | Negative | A class inherits 'from Root' while two bundles in scope both declare 'class Root'. The parent sil... | ✅ |
| 56 | `bad_ambiguous_use_row_type.obs` | Negative | A jagged array's row type (#1246) on a class two used bundles both declare: 'rows : Shape[][]' to... | ✅ |
| 57 | `bad_arith_mul_zero_trap.obs` | Negative | Multiplying by a literal zero must still evaluate the other operand. The optimizer rewrites `x *... | ✅ |
| 58 | `bad_array_literal_2d_expressions.obs` | Negative | Array literals with expressions (#1245): expressions are one-dimensional for now; ragged rows are... | ✅ |
| 59 | `bad_array_literal_enum_mix.obs` | Negative | An enum value is an Int at run time. Mixed with a primitive in an array literal, it was taken for... | ✅ |
| 60 | `bad_array_literal_mixed_primitives.obs` | Negative | Array literals with expressions (#1245): an Int and a Float in one literal: there is no implicit... | ✅ |
| 61 | `bad_array_literal_nil_element.obs` | Negative | Array literals with expressions (#1245): Nil takes its type from the literal's other elements (co... | ✅ |
| 62 | `bad_array_narrowing.obs` | Negative | An array of a class serves as an array of its parent or an interface it implements (core_array_in... | ✅ |
| 63 | `bad_array_too_many_dimensions.obs` | Negative | rejects array too many dimensions | ✅ |
| 64 | `bad_assert_label_type.obs` | Negative | rejects assert label type | ✅ |
| 65 | `bad_assert_missing_expr.obs` | Negative | rejects assert missing expr | ✅ |
| 66 | `bad_assert_not_bool.obs` | Negative | rejects assert not bool | ✅ |
| 67 | `bad_assert_undefined.obs` | Negative | rejects assert undefined | ✅ |
| 68 | `bad_assign_type_detail.obs` | Negative | "Invalid cast with classes: Int and String" said neither that this was an assignment, nor to what... | ✅ |
| 69 | `bad_bang_operator.obs` | Negative | '!' is not an Objeck operator; the message says what to write instead ('<>' for not). The stateme... | ✅ |
| 70 | `bad_boxing_ambiguous_overload.obs` | Negative | Two methods take the call once an argument is boxed, and neither fits every argument at least as... | ✅ |
| 71 | `bad_boxing_array_for_holder.obs` | Negative | An array is not boxed, and one value is not an array of holders. Neither call fits its function,... | ✅ |
| 72 | `bad_boxing_bool_from_int_ref.obs` | Negative | A holder where a primitive is wanted is its value, converted as that value would be: an Int is no... | ✅ |
| 73 | `bad_boxing_bool_ref_argument.obs` | Negative | An argument is unboxed only into a primitive its value converts to. A BoolRef does not fit an Int... | ✅ |
| 74 | `bad_boxing_float_ref_index.obs` | Negative | An index is unboxed from an IntRef, a CharRef or a ByteRef. A FloatRef is not an index, as a Floa... | ✅ |
| 75 | `bad_boxing_int_for_bool_ref.obs` | Negative | A primitive is boxed only into a holder it converts to: an Int is not a Bool, so it does not beco... | ✅ |
| 76 | `bad_boxing_int_from_bool_ref.obs` | Negative | A BoolRef is never a number: returned from a method that returns an Int it is refused, at the 're... | ✅ |
| 77 | `bad_call_arguments_unclosed.obs` | Negative | A call missing its ')' produced four errors (Expected expression, twice; Expected ')'; Expected '... | ✅ |
| 78 | `bad_call_overload_detail.obs` | Negative | A call that matches no overload shows how it was called and the overloads there are. | ✅ |
| 79 | `bad_call_receiver_inherited.obs` | Negative | A method the class inherits is found on the way up: the report still names the class the call was... | ✅ |
| 80 | `bad_call_receiver_instance.obs` | Negative | The same for a method called on an object: the report names the class of the object, and lists th... | ✅ |
| 81 | `bad_call_receiver_no_method.obs` | Negative | The second line of the same report: the class that has no such method is the one the call was mad... | ✅ |
| 82 | `bad_call_receiver_own.obs` | Negative | A call with no class before it is still reported against the class it stands in. | ✅ |
| 83 | `bad_call_receiver_static.obs` | Negative | A call of a function another class does not have is reported against that class. The search for t... | ✅ |
| 84 | `bad_call_undefined.obs` | Negative | rejects call undefined | ✅ |
| 85 | `bad_char_literal_two_chars.obs` | Negative | A character literal with more than one character used to be "Invalid expression: unexpected token... | ✅ |
| 86 | `bad_char_literal_unknown_escape.obs` | Negative | An escape the language does not have in a character literal used to leave the token's type unset.... | ✅ |
| 87 | `bad_class_enum_other_bundle.obs` | Negative | A class's enum is in scope in its class, the class's children and the other classes of its bundle... | ✅ |
| 88 | `bad_class_scope_initializer.obs` | Negative | A declaration with a value written between two methods is refused, and its value is not analyzed:... | ✅ |
| 89 | `bad_collection_literal_box_bool.obs` | Negative | The numbers of a collection literal for a Vector of a holder are boxed by the rule for an argumen... | ✅ |
| 90 | `bad_collection_literal_method_ref.obs` | Negative | An element of a collection literal that names a function is a method reference, with a reference'... | ✅ |
| 91 | `bad_collection_literal_type.obs` | Negative | The literal is typed by the target, so elements of the wrong type are the same error as the long... | ✅ |
| 92 | `bad_cond_expr_dimension_mismatch.obs` | Negative | Guards #867's companion check. Once a conditional keeps its branches' dimension, an array branch... | ✅ |
| 93 | `bad_cond_expr_row_and_scalar.obs` | Negative | A jagged array's row is an array, so it cannot be one branch of '?' when the other is a single va... | ✅ |
| 94 | `bad_cond_expr_scalar_or_nil.obs` | Negative | Nil is a value of every class and of every array (cond_expr_array_or_nil.obs), and of nothing els... | ✅ |
| 95 | `bad_constructor_missing.obs` | Negative | 'Shapeless->New()' on a class with no constructor at all resolved to System.Base's New: it built... | ✅ |
| 96 | `bad_constructor_missing_library.obs` | Negative | A library class with no constructor, System.Runtime, resolved 'Runtime->New()' to System.Base's N... | ✅ |
| 97 | `bad_constructor_not_inherited.obs` | Negative | A constructor is not inherited. 'Kid->New()', on a class that declares no 'New' of its own, walke... | ✅ |
| 98 | `bad_ctor_promotion_syntax.obs` | Negative | A constructor with a promoted field ('New(@x : Int)') whose first statement does not parse is a s... | ✅ |
| 99 | `bad_ctor_promotion_type.obs` | Negative | A promoted parameter takes the field's value, so the types must agree (the assignment is checked... | ✅ |
| 100 | `bad_curried_call_not_function.obs` | Negative | `Foo(2)(5)` is a curried call (#1184): it calls the value Foo(2) returns. Foo returns an Int, so... | ✅ |
| 101 | `bad_decl_inside_body.obs` | Negative | A method missing its closing '}' ran into the next 'function', reported as "Expected expression,... | ✅ |
| 102 | `bad_div_zero_folded.obs` | Negative | A constant division by zero must still trap at runtime, at every optimization level. The constant... | ✅ |
| 103 | `bad_dot_without_name.obs` | Negative | A '.' with nothing after it used to stay on the end of the name, which then failed far from the c... | ✅ |
| 104 | `bad_duplicate_class.obs` | Negative | rejects duplicate class | ✅ |
| 105 | `bad_error_raise_unhandled.obs` | Negative | Runtime->Raise with no ?-> region to recover from (#1247): the program reports "Unhandled <class>... | ✅ |
| 106 | `bad_expression_too_long.obs` | Negative | A statement chaining thousands of operators crashed the compiler (the tree it builds is as deep a... | ✅ |
| 107 | `bad_field_accessor_local.obs` | Negative | A local is not a field (line 7). | ✅ |
| 108 | `bad_field_accessor_static.obs` | Negative | Accessors are for instance fields (line 6). | ✅ |
| 109 | `bad_field_typo_compound_assign.obs` | Negative | A misspelt field in a compound assignment is an error (#1184). Any unknown name used to become a... | ✅ |
| 110 | `bad_float_div_zero_dead_store.obs` | Negative | A Float division by zero must raise even when its result is never read (#1178). Dead-store elimin... | ✅ |
| 111 | `bad_float_div_zero_folded.obs` | Negative | A constant Float division by zero must raise at every optimization level (#1178). The VM raises "... | ✅ |
| 112 | `bad_float_literal_negative_overflow.obs` | Negative | A negated float literal just past the largest double is rejected too; it compiled silently as neg... | ✅ |
| 113 | `bad_float_literal_overflow.obs` | Negative | A float literal past the largest double must be rejected. It was parsed with wcstod, whose range... | ✅ |
| 114 | `bad_float_literal_underflow.obs` | Negative | A nonzero float literal below the smallest subnormal double rounds to zero and must be rejected r... | ✅ |
| 115 | `bad_float_literal_underflow_edge.obs` | Negative | 2.4e-324 is below half the smallest subnormal double (4.9e-324), so it rounds to zero and is reje... | ✅ |
| 116 | `bad_generic_arg_mismatch.obs` | Generics (neg) | Guards the generic type-argument compatibility check (and its readable diagnostic). Assigning a V... | ✅ |
| 117 | `bad_generic_array_type_argument.obs` | Generics (neg) | 'Vector->New()<String[]>' was accepted at the declaration, and then every AddBack(String[]) faile... | ✅ |
| 118 | `bad_generic_bounded_param.obs` | Generics (neg) | A bounded parameter (T : Compare) takes the type argument, not any value of the bound: Holder<Str... | ✅ |
| 119 | `bad_generic_compound_bound.obs` | Generics (neg) | A concrete type argument that satisfies only one of a compound bound (T : A & B) must be rejected... | ✅ |
| 120 | `bad_generic_fbound.obs` | Generics (neg) | A concrete argument that does not satisfy an F-bounded constraint (T : Compare<T>) must be reject... | ✅ |
| 121 | `bad_generic_infer_ambiguous.obs` | Generics (neg) | Two constructors take one argument, and a Vector<String> fits both: as the value (a Tagged<Vector... | ✅ |
| 122 | `bad_generic_infer_bound.obs` | Generics (neg) | An inferred type argument is checked against the parameter's bound exactly as a written one is: '... | ✅ |
| 123 | `bad_generic_infer_conflict.obs` | Generics (neg) | Two arguments give one type parameter two classes. No common parent is looked for: the call is re... | ✅ |
| 124 | `bad_generic_infer_mismatch.obs` | Generics (neg) | Written type arguments are never replaced by the target's: a Vector<IntRef> stored into a Vector<... | ✅ |
| 125 | `bad_generic_infer_nil.obs` | Generics (neg) | 'Nil' is a value of every class, so it says nothing about a type parameter. | ✅ |
| 126 | `bad_generic_infer_no_source.obs` | Generics (neg) | Nothing says what the Vector holds: the local has no declared type and the constructor takes no a... | ✅ |
| 127 | `bad_generic_infer_other_target.obs` | Generics (neg) | A declared type gives a constructor call its type arguments only when it is the class being const... | ✅ |
| 128 | `bad_generic_infer_viable_conflict.obs` | Generics (neg) | Two constructors take two arguments, and these arguments can be passed to both: a String and a Wi... | ✅ |
| 129 | `bad_generic_variance.obs` | Generics (neg) | Variance must stay sound: a covariant 'out T' does NOT allow the reverse direction. Producer<Anim... | ✅ |
| 130 | `bad_index_whole_array.obs` | Negative | An index is one value. An array's type is its element's with a dimension, so a whole Int[] (or Ch... | ✅ |
| 131 | `bad_inherit_unknown.obs` | Negative | rejects inherit unknown | ✅ |
| 132 | `bad_inherited_library_instance_call_lambda.obs` | Negative | A lambda does not capture self (bad_lambda_implicit_self.obs), so inside one an unqualified call... | ✅ |
| 133 | `bad_inherited_library_instance_call_static.obs` | Negative | A function has no instance, so an unqualified call of an instance method the class inherits from... | ✅ |
| 134 | `bad_int64_min_unnegated.obs` | Negative | 9223372036854775808 is Int's minimum only when negated (int64_min_literal). On its own, or as the... | ✅ |
| 135 | `bad_int_literal_negative_range.obs` | Negative | A negated integer literal past Int's range names the problem, with its sign. The negation path ha... | ✅ |
| 136 | `bad_int_literal_overflow.obs` | Negative | An integer literal past the signed range must be rejected. It used to be parsed with a saturating... | ✅ |
| 137 | `bad_int_literal_range.obs` | Negative | An integer literal too large for Int names the problem (#1184); it was reported only as "unexpect... | ✅ |
| 138 | `bad_interp_conditional_half.obs` | Negative | A conditional with one branch inside an interpolation: the same crash, from a tree whose second b... | ✅ |
| 139 | `bad_interp_conditional_open.obs` | Negative | A question mark left at the end of an interpolated expression starts a conditional with no branch... | ✅ |
| 140 | `bad_interp_tilde.obs` | Negative | A stray tilde after a call inside an interpolation is read as the start of a function type that n... | ✅ |
| 141 | `bad_interp_too_long.obs` | Negative | An interpolation is parsed by a parser of its own, when the string is analyzed. One with more cha... | ✅ |
| 142 | `bad_invisible_in_name.obs` | Negative | A zero-width space between two halves of a name (here U+200B after 'ab') looks like one word and... | ✅ |
| 143 | `bad_jagged_mixed_rows.obs` | Negative | Jagged arrays (#1246): an Int row and a String row in one literal. | ✅ |
| 144 | `bad_jagged_row_type.obs` | Negative | Jagged arrays (#1246): a Float[] row in an Int[][]. | ✅ |
| 145 | `bad_jagged_second_subscript.obs` | Negative | Jagged arrays (#1246): a second subscript on an Int[]. | ✅ |
| 146 | `bad_jagged_too_many_subscripts.obs` | Negative | Jagged arrays (#1246): a third subscript on an Int[][]. | ✅ |
| 147 | `bad_json_literal_array_close.obs` | Negative | An array in a JSON literal closes with ']'. | ✅ |
| 148 | `bad_json_literal_char_value.obs` | Negative | A Char is not a JSON value: it is neither a string nor a number until the program says which. | ✅ |
| 149 | `bad_json_literal_element.obs` | Negative | An element of an array in a JSON literal is one JsonElement has an Add for. | ✅ |
| 150 | `bad_json_literal_name.obs` | Negative | The name of a member is a String. | ✅ |
| 151 | `bad_json_literal_no_colon.obs` | Negative | A member is a name, a colon and a value. | ✅ |
| 152 | `bad_json_literal_no_comma.obs` | Negative | Members are separated by commas. | ✅ |
| 153 | `bad_json_literal_no_value.obs` | Negative | A member needs a value. | ✅ |
| 154 | `bad_json_literal_value.obs` | Negative | A value in a JSON literal is one JsonElement has an Insert for; the error names the value's type,... | ✅ |
| 155 | `bad_keyword_as_var.obs` | Negative | rejects keyword as var | ✅ |
| 156 | `bad_lambda_field_nested.obs` | Negative | A lambda inside a lambda does not capture the instance (documented limit): line 10 (the inner lam... | ✅ |
| 157 | `bad_lambda_field_static.obs` | Negative | A lambda in a function (no instance) cannot name a field: line 10. | ✅ |
| 158 | `bad_lambda_implicit_self.obs` | Negative | Guards the boundary of captured method calls (v2026.10.0 C3). A lambda body may call an instance... | ✅ |
| 159 | `bad_lambda_in_interpolation.obs` | Negative | A lambda inside a {$ } interpolation used to be reported as "Invalid escaped string literal" (the... | ✅ |
| 160 | `bad_lambda_multi_arg.obs` | Negative | A bare lambda is only inferred when it is a call's sole argument. As one of several arguments the... | ✅ |
| 161 | `bad_lambda_ternary.obs` | Negative | A bare lambda `\() => body` gets its type from a FuncRef<R> assignment target, return type or sin... | ✅ |
| 162 | `bad_leading_zero_nine.obs` | Negative | '09' has no octal reading. It was an unknown token, reported as an unexpected token with follow-o... | ✅ |
| 163 | `bad_leading_zero_octal.obs` | Negative | A decimal-looking literal with a leading zero was read as octal, so '010' compiled silently as 8.... | ✅ |
| 164 | `bad_local_redeclared_in_block.obs` | Negative | A block may hide a name of an enclosing block, but within one block a name is declared once. | ✅ |
| 165 | `bad_local_redeclares_parameter.obs` | Negative | A method's parameters and the locals of its body are declared in one block: a local there cannot... | ✅ |
| 166 | `bad_local_typo_compound_assign.obs` | Negative | Only `name := value` declares a local (#1184). The target of +=, -= and the like must already exi... | ✅ |
| 167 | `bad_loop_unassigned_function_value.obs` | Negative | A function variable declared without a value inside a loop is unassigned (a Nil dereference when... | ✅ |
| 168 | `bad_map_literal_after_nil_coalesce.obs` | Negative | A map literal after '??' is the default value, not the block of a nil-guard ('x := E ?? { ...; re... | ✅ |
| 169 | `bad_map_literal_argument.obs` | Negative | An argument is not a target a map literal takes its type from (the overload is chosen from the ar... | ✅ |
| 170 | `bad_map_literal_constructor_class.obs` | Negative | A constructor call in a map literal's entry takes its type arguments from the map's value type on... | ✅ |
| 171 | `bad_map_literal_duplicate_int_key.obs` | Negative | The same constant key twice, in a Hash keyed by number. | ✅ |
| 172 | `bad_map_literal_duplicate_key.obs` | Negative | The same constant key twice in a map that keeps one value per key would lose the first value with... | ✅ |
| 173 | `bad_map_literal_in_conditional.obs` | Negative | Inside another expression a map literal has no target of its own. | ✅ |
| 174 | `bad_map_literal_key_type.obs` | Negative | An entry's key is the map's key type. | ✅ |
| 175 | `bad_map_literal_no_colon.obs` | Negative | An entry is a key, a colon and a value. | ✅ |
| 176 | `bad_map_literal_no_comma.obs` | Negative | Entries are separated by commas. | ✅ |
| 177 | `bad_map_literal_no_type.obs` | Negative | A map literal is typed by its target; an inferred variable gives it none. | ✅ |
| 178 | `bad_map_literal_not_map.obs` | Negative | The target is a map class. | ✅ |
| 179 | `bad_map_literal_value_type.obs` | Negative | An entry's value is the map's value type, as for the Insert the entry stands for. | ✅ |
| 180 | `bad_match_all_nil.obs` | Negative | A value whose every arm is Nil has no type. | ✅ |
| 181 | `bad_match_arm_conversion.obs` | Negative | A Float arm after an Int one would be truncated without a word: it is an error, not a conversion. | ✅ |
| 182 | `bad_match_arm_types.obs` | Negative | The arms of a 'match' used as a value agree in type, as the two branches of '?' do. | ✅ |
| 183 | `bad_match_duplicate_pattern.obs` | Negative | A pattern names one arm. | ✅ |
| 184 | `bad_match_float_subject.obs` | Negative | The subject of a 'match' is what a 'select' takes: an Int, Char, Byte, enum or String. | ✅ |
| 185 | `bad_match_foreign_item.obs` | Negative | An item of another enum would be compared by its number. | ✅ |
| 186 | `bad_match_library_enum_items.obs` | Negative | An enum from a library is checked the same way. | ✅ |
| 187 | `bad_match_missing_arrow.obs` | Negative | A pattern is followed by '=>'. | ✅ |
| 188 | `bad_match_missing_items.obs` | Negative | A 'match' on an enum with no 'other' arm names every item; the error lists the ones it leaves out. | ✅ |
| 189 | `bad_match_negative_offset_enum.obs` | Negative | An enum numbered from a negative value is checked like any other (match_negative_offset_enum.obs)... | ✅ |
| 190 | `bad_match_no_arms.obs` | Negative | A 'match' has at least one arm. | ✅ |
| 191 | `bad_match_number_for_item.obs` | Negative | A number is not an item of the enum the 'match' is on. | ✅ |
| 192 | `bad_match_returns_not_exhaustive.obs` | Negative | A method that ends in a 'match' with no 'other' arm returns on every path only when the match is... | ✅ |
| 193 | `bad_match_statement_arm.obs` | Negative | The first arm of a 'match' that starts a statement decides its form: after a block, every arm is... | ✅ |
| 194 | `bad_match_statement_missing_items.obs` | Negative | The statement form is checked as the value form is. | ✅ |
| 195 | `bad_match_value_needs_other.obs` | Negative | A 'match' used as a value yields one for every subject: on an Int that takes an 'other' arm. | ✅ |
| 196 | `bad_method_generic_bound.obs` | Negative | Method generics (#1242): an argument that does not meet the bound. | ✅ |
| 197 | `bad_method_generic_call.obs` | Negative | A method's own type parameter (#1242) is not a class. 'T->New()' in 'Id<T>' named whatever class... | ✅ |
| 198 | `bad_method_generic_conflict.obs` | Negative | Method generics (#1242): two arguments that bind T to different classes. | ✅ |
| 199 | `bad_method_generic_library_bound.obs` | Negative | Method-level generics in libraries (#1242, phase 2): CompareList's Map<R : Compare> is checked ag... | ✅ |
| 200 | `bad_method_generic_primitive.obs` | Negative | Method generics (#1242): a primitive argument: a type parameter takes an object, as class generic... | ✅ |
| 201 | `bad_method_ref_instance.obs` | Negative | Only functions can be referenced, not instance methods. | ✅ |
| 202 | `bad_method_ref_mismatch.obs` | Negative | The function's own signature is what is taken, so a target of another shape is still a type error... | ✅ |
| 203 | `bad_method_ref_overloaded.obs` | Negative | A method reference without a signature names more than one function: the signature cannot be read... | ✅ |
| 204 | `bad_method_ref_primitive.obs` | Negative | A FuncRef holds class types, so a function over Int cannot be referenced without its signature (l... | ✅ |
| 205 | `bad_missing_semicolon_eol.obs` | Negative | A field declaration missing its ';' was reported at the NEXT line's first token, with a cascade a... | ✅ |
| 206 | `bad_mod_zero.obs` | Negative | Modulus by zero must trap like division by zero does. The interpreter's ModInt had no zero check... | ✅ |
| 207 | `bad_named_argument_function_value.obs` | Negative | Named arguments (#1244): a function value: a function type has no parameter names. | ✅ |
| 208 | `bad_named_argument_library.obs` | Negative | Named arguments (#1244): a name a library method does not have. The error lists the parameter nam... | ✅ |
| 209 | `bad_named_argument_missing_required.obs` | Negative | Named arguments (#1244): skipping a parameter that has no default. | ✅ |
| 210 | `bad_named_argument_positional_after.obs` | Negative | Named arguments (#1244): a positional argument after a named one. | ✅ |
| 211 | `bad_named_argument_primitive.obs` | Negative | Named arguments (#1244): a call on a primitive value is a static call with the value as a hidden... | ✅ |
| 212 | `bad_named_argument_twice.obs` | Negative | Named arguments (#1244): one parameter given by position and again by name. | ✅ |
| 213 | `bad_named_argument_unknown.obs` | Negative | Named arguments (#1244): a parameter name the method does not have; the error lists the ones it has. | ✅ |
| 214 | `bad_nested_undefined_calls.obs` | Negative | A call to a method that does not exist, nested 30 deep, took exponential time: each call was trie... | ✅ |
| 215 | `bad_nesting_too_deep.obs` | Negative | 400 nested parentheses crashed the compiler with a stack overflow (exit code 0xC00000FD). Nesting... | ✅ |
| 216 | `bad_new_negative_array.obs` | Negative | A negative array size must stop the VM in every mode (#1181). The JIT wrapped the count to 0 and... | ✅ |
| 217 | `bad_new_negative_dim.obs` | Negative | A negative dimension beside a zero one must stop the VM too (#1181): the product is 0, so the siz... | ✅ |
| 218 | `bad_nil_array_argument_ambiguous.obs` | Negative | A literal Nil matches an array parameter as it matches an object one (core_nil_array_argument.obs... | ✅ |
| 219 | `bad_nil_guard_block_syntax.obs` | Negative | A syntax error in the last statement of a nil-guard's block is reported as a syntax error. The ch... | ✅ |
| 220 | `bad_nil_guard_expression.obs` | Negative | '?? return' belongs to an assignment statement, not to an argument (line 9). | ✅ |
| 221 | `bad_nil_guard_falls_through.obs` | Negative | A guard block that can fall through would leave the variable Nil (line 10). | ✅ |
| 222 | `bad_number_literal_forms.obs` | Negative | Each malformed number is its own error, said in its own words, at its own first character; one ba... | ✅ |
| 223 | `bad_numeric_literal_in_array.obs` | Negative | An out-of-range literal inside an array literal is the only error. Recovery used to skip to the n... | ✅ |
| 224 | `bad_numeric_literal_mid_expression.obs` | Negative | An out-of-range literal in the middle of an expression is the only error. Recovery used to skip t... | ✅ |
| 225 | `bad_param_missing_comma.obs` | Negative | A missing ',' between parameters skipped the parser to the next '}', the end of the whole body, a... | ✅ |
| 226 | `bad_primitive_receiver_arity.obs` | Negative | A call on a primitive passes the receiver as parameter 0 of a function on its $ class, so `255->T... | ✅ |
| 227 | `bad_prompt_brace.obs` | Negative | A closing brace that closes no hole of a prompt literal is a compile error where it stands; a lit... | ✅ |
| 228 | `bad_prompt_hole_empty.obs` | Negative | An empty hole in a prompt literal is a compile error at the hole; a literal brace is written doub... | ✅ |
| 229 | `bad_prompt_hole_object.obs` | Negative | Only a String, Int, Float, Bool, Char or Byte can be written into a prompt: a hole whose variable... | ✅ |
| 230 | `bad_prompt_hole_type.obs` | Negative | A hole declared with a type its variable does not have is a compile error at the hole: no convers... | ✅ |
| 231 | `bad_prompt_hole_undefined.obs` | Negative | A hole of a prompt literal that names no variable in scope is a compile error at the hole, here o... | ✅ |
| 232 | `bad_quote_in_interpolation.obs` | Negative | A quote inside an interpolation ends the string literal there: the scanner stops a string at its... | ✅ |
| 233 | `bad_readonly_record_assignment.obs` | Negative | rejects readonly record assignment | ✅ |
| 234 | `bad_readonly_record_op_assignment.obs` | Negative | rejects readonly record op assignment | ✅ |
| 235 | `bad_ref_argument_expression.obs` | Negative | Reference parameters (#1243): an array element passed as 'ref'; the copy back would evaluate its... | ✅ |
| 236 | `bad_ref_array_param.obs` | Negative | Reference parameters (#1243): a 'ref' parameter of an array type; an array is already shared. | ✅ |
| 237 | `bad_ref_default.obs` | Negative | Reference parameters (#1243): a 'ref' parameter has a default; the caller's cell is its value. | ✅ |
| 238 | `bad_ref_in_condition.obs` | Negative | Reference parameters (#1243): a call with a 'ref' argument in an 'if' condition; its copy back wo... | ✅ |
| 239 | `bad_ref_in_return.obs` | Negative | Reference parameters (#1243): a call with a 'ref' argument in a return; its copy back would never... | ✅ |
| 240 | `bad_runtime_bounds.obs` | Negative | VM must not crash (segfault) on array out-of-bounds access — exit 1 with message | ✅ |
| 241 | `bad_runtime_divzero.obs` | Negative | VM must not crash (segfault) on integer division by zero — exit 1 with message | ✅ |
| 242 | `bad_runtime_null.obs` | Negative | VM must not crash (segfault) when dereferencing a nil object — exit 1 with message | ✅ |
| 243 | `bad_runtime_stack.obs` | Negative | VM must not crash (segfault) on call stack overflow — exit 1 with message. NOTE: the recursion MU... | ✅ |
| 244 | `bad_select_label_call.obs` | Negative | A call on a select label was ignored: 'label "B"->ToLower()' was hashed as "B", so this compiled... | ✅ |
| 245 | `bad_select_label_concatenation.obs` | Negative | A select label is one constant, not an expression: a concatenation is refused by the parser. This... | ✅ |
| 246 | `bad_select_label_integer_on_string.obs` | Negative | An integer label in a select on a string compared the hash of the string with the integer: it com... | ✅ |
| 247 | `bad_select_label_interpolated.obs` | Negative | A select label is matched by the hash of its text, which the compiler takes. A label with a value... | ✅ |
| 248 | `bad_select_label_string_on_integer.obs` | Negative | A string label in a select on an integer compared the integer with the hash of the label's text:... | ✅ |
| 249 | `bad_select_value_block.obs` | Negative | A 'select' where a value is wanted takes a value after each label, not a block. | ✅ |
| 250 | `bad_select_value_missing_items.obs` | Negative | A 'select' used as a value on an enum, with no 'other', names every item. | ✅ |
| 251 | `bad_select_value_needs_other.obs` | Negative | A 'select' used as a value yields one for every subject: on an Int that takes 'other'. | ✅ |
| 252 | `bad_spawn_anonymous_class.obs` | Negative | 'spawn' is written out as an anonymous Thread class, and anonymous classes do not nest: a 'spawn'... | ✅ |
| 253 | `bad_spawn_implicit_self.obs` | Negative | The block of a 'spawn' follows a lambda's rules. It may call an instance method on an object it c... | ✅ |
| 254 | `bad_spawn_return_value.obs` | Negative | The block of a 'spawn' is a thread's body. 'return;' ends the thread; a value has nowhere to go,... | ✅ |
| 255 | `bad_spawn_thread_type.obs` | Negative | 'spawn { ... }' is the started thread, a System.Concurrency.Thread, and is typed as one: it canno... | ✅ |
| 256 | `bad_statements_same_line_assign.obs` | Negative | `x := 1 Foo(3);` compiled as an assignment and a call (#1184). Two statements on one line need a... | ✅ |
| 257 | `bad_statements_same_line_call.obs` | Negative | Semicolons between statements are optional, so `Foo(2) Foo(3);` compiled as two calls without a w... | ✅ |
| 258 | `bad_string_escape_unknown.obs` | Negative | The error names the escape and points at its backslash, with the source line, instead of "Invalid... | ✅ |
| 259 | `bad_string_interp_format.obs` | Negative | An unrecognized interpolation format specifier must be rejected at compile time. | ✅ |
| 260 | `bad_string_interp_format_range.obs` | Negative | An interpolation width or precision too large for an int is an invalid format specifier, reported... | ✅ |
| 261 | `bad_string_interp_literal.obs` | Negative | A malformed literal inside "{$...}" is reported as an invalid expression, not as an undefined var... | ✅ |
| 262 | `bad_string_plus_int_advice.obs` | Negative | Adding a String and an Int says how to convert, and the variable that failed to get a type from t... | ✅ |
| 263 | `bad_string_runs_on_note.obs` | Negative | A string missing its closing quote runs on to the next quote in the file, so the first error was... | ✅ |
| 264 | `bad_string_unterminated.obs` | Negative | A string with no closing quote swallowed the rest of the file, and the only error was "Expected '... | ✅ |
| 265 | `bad_syntax_unclosed.obs` | Negative | rejects syntax unclosed | ✅ |
| 266 | `bad_try_empty.obs` | Negative | try (#1247): a try with neither handles nothing. | ✅ |
| 267 | `bad_try_finally_break.obs` | Negative | try (#1247): a break out of a finally block would drop the error it runs for. | ✅ |
| 268 | `bad_try_finally_return.obs` | Negative | try (#1247): a return out of a finally block would drop the error it runs for. | ✅ |
| 269 | `bad_try_on_not_error.obs` | Negative | try (#1247): an 'on' clause names a class that is not an Error. | ✅ |
| 270 | `bad_try_on_unreachable.obs` | Negative | try (#1247): a clause for a subclass after one for its parent can never run. | ✅ |
| 271 | `bad_type_test_array_type.obs` | Negative | An array type is not a class to test for. | ✅ |
| 272 | `bad_type_test_more_condition.obs` | Negative | The test is the whole condition: the name it binds exists only in the block, so nothing after it... | ✅ |
| 273 | `bad_type_test_name_after_block.obs` | Negative | The bound name belongs to the block of its 'if'. | ✅ |
| 274 | `bad_type_test_name_in_else.obs` | Negative | In the else block the value is not of the type, so the name is not there either. | ✅ |
| 275 | `bad_type_test_name_taken.obs` | Negative | The bound name is declared in the block as `name : Type` is, so the block cannot declare the name... | ✅ |
| 276 | `bad_type_test_no_name.obs` | Negative | The binding type test names the value it binds; without a name it is `value->TypeOf(Type)`. | ✅ |
| 277 | `bad_type_test_not_class.obs` | Negative | Only a class (or an interface) can be tested for. | ✅ |
| 278 | `bad_type_test_outside_if.obs` | Negative | 'is' tests a type only as the condition of an 'if'. | ✅ |
| 279 | `bad_type_test_unknown_class.obs` | Negative | The type must exist. | ✅ |
| 280 | `bad_type_test_unrelated_class.obs` | Negative | A value can only be tested for a class it could be: the cast the test stands for is checked. | ✅ |
| 281 | `bad_typo_class_suggestion.obs` | Negative | A misspelled class name names the class it is closest to. | ✅ |
| 282 | `bad_typo_method_suggestion.obs` | Negative | An undefined method says what it was called as, which class it was called on, and the method of t... | ✅ |
| 283 | `bad_typo_variable_suggestion.obs` | Negative | A misspelled variable before '->' was "Undefined class: 'cnt'". It is an undefined variable, and... | ✅ |
| 284 | `bad_unassigned_function_value.obs` | Negative | Calling a function variable that was never assigned is a Nil dereference (#1183). It used to run... | ✅ |
| 285 | `bad_unclosed_brace_indentation.obs` | Negative | A '}' missing inside a method was reported only as "Expected '}'" at the end of the file. The err... | ✅ |
| 286 | `bad_undefined_type.obs` | Negative | rejects undefined type | ✅ |
| 287 | `bad_undefined_var.obs` | Negative | A read of a variable that was never declared must be rejected with the undefined-variable diagnos... | ✅ |
| 288 | `bad_undefined_variable_read.obs` | Negative | Reading a name that was never declared (#1184). Inside a method an unknown name silently became a... | ✅ |
| 289 | `bad_unsigned_literal_suffix.obs` | Negative | The 'u' suffix reads a literal across the unsigned range, so a value past 2^64-1 has no represent... | ✅ |
| 290 | `bad_unsigned_suffix_on_float.obs` | Negative | The unsigned suffix is meaningless on a floating-point literal and must not be accepted silently. | ✅ |
| 291 | `bad_use_unlinked_bundle.obs` | Negative | A 'use' of a bundle whose library was not linked names that library. The undefined-bundle error u... | ✅ |
| 292 | `bad_using_equals.obs` | Negative | rejects using equals | ✅ |
| 293 | `bad_using_no_close.obs` | Negative | rejects using no close | ✅ |
| 294 | `bad_using_scope.obs` | Negative | rejects using scope | ✅ |
| 295 | `bad_wrong_return.obs` | Negative | rejects wrong return | ✅ |
| 296 | `base_tostring.obs` | Other | System.Base->ToString (#1248, from #1186). Every object has a ToString now. A Stringify instance... | ✅ |
| 297 | `block_local_shadowing.obs` | Other | A block may declare a name that an enclosing block (or the parameter list) already has: its own v... | ✅ |
| 298 | `bool_array_call_index.obs` | Other | Subscripting a Bool[] returned from a call (#988). A Bool[] is allocated as NEW_BYTE_ARY and a lo... | ✅ |
| 299 | `bool_array_nil_comparison.obs` | Other | Comparing a call that returns Bool[] against Nil. `if(obj->Predict(x) = Nil)` used to fail to com... | ✅ |
| 300 | `boxing_before_after.obs` | Other | The boxing example of docs/FEATURES.md, kept compiling and running: a small tally written with ev... | ✅ |
| 301 | `boxing_overload_array_argument.obs` | Other | An array argument is never boxed, and an overload that would need it boxed does not get in the wa... | ✅ |
| 302 | `byte_array_header_test.obs` | Other | A Byte[] reports one size to Objeck and another to native code. Size() reads header word [2]; nat... | ✅ |
| 303 | `byte_array_hex_string.obs` | Other | Byte[]->ToHexString is a hex digest It returned a bracketed list, "[0x76,0x4f,...]", while the ex... | ✅ |
| 304 | `byte_narrowing.obs` | Other | Regression: a scalar Byte wraps to 8 bits (#1176). A value stored into a Byte local, field or par... | ✅ |
| 305 | `bytes_to_string_length.obs` | Other | String->New(Byte[]) must decode exactly the bytes of the array. A Byte[] is not NUL-terminated. T... | ✅ |
| 306 | `calculated_operand_call_chain.obs` | Other | A CHAIN of method calls attached to a parenthesised calculation, used as an operand of another ca... | ✅ |
| 307 | `calculated_receiver_call.obs` | Other | A method call attached to a calculated expression, used inside another calculated expression. `(1... | ✅ |
| 308 | `call_indices_nested_link.obs` | Other | A subscript on the result of a LATER link of a call chain: 'cells->Get(r)->Get()[c]'. 'GetItems()... | ✅ |
| 309 | `cast_and_typeof_chains.obs` | Other | Regression: a call chain after As(T), and TypeOf(..) at the end of a chain. Found by the Rosetta... | ✅ |
| 310 | `char_array_exact_length.obs` | Other | Char[] and Byte[] natives use the array's own length: exact-length arrays and offset/num ranges T... | ✅ |
| 311 | `char_read_embedded_nul.obs` | Other | ReadBuffer(offset, num, Char[]) keeps a zero byte: it is the character U+0000, not the end of the... | ✅ |
| 312 | `char_read_invalid_utf8.obs` | Other | ReadBuffer(offset, num, Char[]) and bytes that are not UTF-8: nothing that is a character is lost... | ✅ |
| 313 | `char_read_slice_terminator.obs` | Other | ReadBuffer(offset, num, Char[]) never writes its zero on the element after a full slice A read in... | ✅ |
| 314 | `char_read_whole_characters.obs` | Other | ReadBuffer(offset, num, Char[]) stores whole characters and returns how many A Char[] read of a b... | ✅ |
| 315 | `checkers_engine.obs` | Other | GENERATED by programs/examples/checkers/make_regression_test.py from the checkers_*.obs files nex... | ✅ |
| 316 | `cipher_aes_versioned.obs` | Other | Cipher.Encrypt/Decrypt->AES256 versioned format: AES-256-GCM with a PBKDF2 derived key, random sa... | ✅ |
| 317 | `client_socket_errors.obs` | Other | A client socket that fails says why, and keeps saying it TCPSocket->GetLastError() read the live... | ✅ |
| 318 | `closure_array_param_capture.obs` | Other | Guards #849: a lambda capturing an object array crashed obr (0xC0000005). Inside a lambda a captu... | ✅ |
| 319 | `closure_array_var_index.obs` | Other | A captured array indexed by a variable inside a lambda crashed obc (0xC0000005). Inside a lambda... | ✅ |
| 320 | `closure_bare_lambda.obs` | Other | Regression for bare lambdas `\(...) => body` with the return type inferred from context, auto-wra... | ✅ |
| 321 | `closure_block_body.obs` | Other | Regression for lambda block bodies: `\(...) ~ R : () => { ...; return e; }`. Previously a `{ }` l... | ✅ |
| 322 | `closure_body_call_result.obs` | Other | Regression: a call used as a statement inside a lambda body must discard its result. The context... | ✅ |
| 323 | `closure_capture_calls.obs` | Other | Calls through captured variables inside lambda bodies (v2026.10.0 C3 + C6). C3: an instance metho... | ✅ |
| 324 | `closure_capture_old_holder_g12.obs` | Other | G12: a closure capturing a young object, stored in an object that is already old, must keep the c... | ✅ |
| 325 | `closure_capture_resolution.obs` | Other | Guards #865 and #866: two sites resolved a name inside a lambda body without the lambda's capture... | ✅ |
| 326 | `closure_direct_call.obs` | Other | Regression for direct FuncRef callability `v()` and functional-call result chaining `v()->Method(... | ✅ |
| 327 | `closure_each_captured_array.obs` | Other | Guards the 'each' loop over an array captured by a lambda, which failed to compile with "Cannot r... | ✅ |
| 328 | `closure_enum_array_capture.obs` | Other | Guards enum-typed arrays against being freed while still live (G11). Storing an enum item into an... | ✅ |
| 329 | `closure_lambda_fn_param.obs` | Other | Guards a compiler defect present through v2026.9.4: a typed lambda with a FUNCTION-TYPED paramete... | ✅ |
| 330 | `closure_lambda_many_params.obs` | Other | Regression: lambdas whose parameter list is longer than the scanner's look-ahead buffer. ParseLam... | ✅ |
| 331 | `closure_lambda_params.obs` | Other | Guards two obc crashes (exit 0xC0000005, no diagnostic) present from v2026.6.3 through v2026.9.4:... | ✅ |
| 332 | `closure_multi_capture.obs` | Other | Regression for multiple capturing lambdas in one class. Each lambda's captured variables live in... | ✅ |
| 333 | `closure_nested_capture.obs` | Other | A lambda nested in a lambda could not use a variable its enclosing lambda captured: obc reported... | ✅ |
| 334 | `closure_param_lambda.obs` | Other | Regression: typed lambdas that take parameters, with and without captures. ParseLambda looks past... | ✅ |
| 335 | `code_formatter_brace.obs` | Other | System.Utility.CodeFormatter: what follows a closing brace, and two operators. The formatter join... | ✅ |
| 336 | `code_formatter_meaning.obs` | Other | System.Utility.CodeFormatter gives back the program it was given. The library's formatter changed... | ✅ |
| 337 | `collect_bimap.obs` | Collections | collect bimap | ✅ |
| 338 | `collect_bloom.obs` | Collections | collect bloom | ✅ |
| 339 | `collect_compare_vector.obs` | Collections | collect compare vector | ✅ |
| 340 | `collect_deque.obs` | Collections | collect deque | ✅ |
| 341 | `collect_each_patterns.obs` | Collections | collect each patterns | ✅ |
| 342 | `collect_fenwick.obs` | Collections | collect fenwick | ✅ |
| 343 | `collect_hash_heap_sort.obs` | Collections | Collection additions from #1186: Hash->Copy, Merge, Each and Filter; the hash-backed MultiHash; H... | ✅ |
| 344 | `collect_hash_open.obs` | Collections | Collection.Hash on open addressing: every public method, growth past one million entries (the old... | ✅ |
| 345 | `collect_hash_ops.obs` | Collections | collect hash ops | ✅ |
| 346 | `collect_map_ops.obs` | Collections | collect map ops | ✅ |
| 347 | `collect_multiset.obs` | Collections | collect multiset | ✅ |
| 348 | `collect_nested_ops.obs` | Collections | collect nested ops | ✅ |
| 349 | `collect_nil_guards.obs` | Collections | collect nil guards | ✅ |
| 350 | `collect_ordered_hash.obs` | Collections | collect ordered hash | ✅ |
| 351 | `collect_pair_compare.obs` | Collections | Collection.Pair->Compare takes the other pair, which is what a sorted collection passes. Pair->Co... | ✅ |
| 352 | `collect_pair_ops.obs` | Collections | collect pair ops | ✅ |
| 353 | `collect_primitive.obs` | Collections | Primitive collections in Collection: IntVector, FloatVector, IntMap, IntSet and BitSet. Covers em... | ✅ |
| 354 | `collect_priority_map.obs` | Collections | collect priority map | ✅ |
| 355 | `collect_queue_ops.obs` | Collections | collect queue ops | ✅ |
| 356 | `collect_set_algebra.obs` | Collections | Set algebra on Collection.Set, HashSet and SetHash, and the method names the three now share (#11... | ✅ |
| 357 | `collect_set_ops.obs` | Collections | collect set ops | ✅ |
| 358 | `collect_sort_stable.obs` | Collections | collect sort stable | ✅ |
| 359 | `collect_stack_deep.obs` | Collections | collect stack deep | ✅ |
| 360 | `collect_stack_ops.obs` | Collections | collect stack ops | ✅ |
| 361 | `collect_trie.obs` | Collections | collect trie | ✅ |
| 362 | `collect_union_find.obs` | Collections | collect union find | ✅ |
| 363 | `collect_vector_ops.obs` | Collections | collect vector ops | ✅ |
| 364 | `collection_literal_boxed.obs` | Other | The numbers of a collection literal for a Vector or CompareVector of a holder are boxed as that h... | ✅ |
| 365 | `collection_literal_method_refs.obs` | Other | In a collection literal for a Vector of function values, an element that names a static function... | ✅ |
| 366 | `collection_literals.obs` | Other | Collection literals typed by the target: `v : Vector<IntRef> := [a, b, c]` is `Vector->New([a, b,... | ✅ |
| 367 | `compile_alias_bundle_scope.obs` | Other | Regression: a bundle's alias is named with its bundle (bad_ambiguous_use_alias.obs). Checks two b... | ✅ |
| 368 | `compile_ambiguous_use_callee_scope.obs` | Other | Regression: a class's declarations resolve in the class's own bundle, wherever they are checked f... | ✅ |
| 369 | `compile_ambiguous_use_names.obs` | Other | Regression: an unqualified name that two 'use'd bundles both declare is a compile error (bad_ambi... | ✅ |
| 370 | `compile_as_call_compare.obs` | Other | Regression: a cast in a call chain used as a comparison operand. `x->As(Kid)->Only() <> "only"`,... | ✅ |
| 371 | `compile_bundle_class_enum_scope.obs` | Other | Regression: a class's enum is in scope in its class, its children and the other classes of its bu... | ✅ |
| 372 | `compile_bundle_enum_same_name.obs` | Other | Regression: bundle-level enums are named with their bundle, as classes are. A bundle-level enum w... | ✅ |
| 373 | `compile_bundle_qualified_enum.obs` | Other | Regression: bundle-qualified enum names. A bundle-level enum is named without its bundle (a class... | ✅ |
| 374 | `compile_class_scoped_enum_names.obs` | Other | Regression: class-scoped enums with the same name in two classes. The parser adds 'ScopedAa#' as... | ✅ |
| 375 | `compile_constructor_own.obs` | Other | Regression: a constructor is not inherited (bad_constructor_not_inherited.obs), so each class ins... | ✅ |
| 376 | `compile_inherited_library_instance_call.obs` | Other | Regression: an instance method calls an instance method its class inherits from a library class w... | ✅ |
| 377 | `compile_method_generic_shadow.obs` | Other | Regression: a method's own type parameter (#1242) shadows a class of the same name that the file'... | ✅ |
| 378 | `compiler_long_add_chain.obs` | Other | A forty-term addition chain. The compiler's AnalyzeCalculation recursed into each calculated oper... | ✅ |
| 379 | `compress_round_trip.obs` | I/O | Byte[] compression round trips: zlib, gzip and raw deflate on empty, one-byte, short and large in... | ✅ |
| 380 | `concurrent_critical_alloc.obs` | Other | Allocating inside a contended critical section (#1186). Eight threads share one ThreadMutex and a... | ✅ |
| 381 | `concurrent_sync_primitives.obs` | Other | System.Concurrency's blocking primitives (#1186): Condition (lang.obl, new COND_NEW/COND_WAIT/CON... | ✅ |
| 382 | `cond_expr_array_or_nil.obs` | Other | One branch of '?' may be Nil when the other is an array of a primitive. Nil is a value of every a... | ✅ |
| 383 | `cond_expr_array_type.obs` | Other | Guards #867: a conditional expression whose branches are primitive arrays was typed as a scalar.... | ✅ |
| 384 | `cond_expr_jagged_row.obs` | Other | The branches of '?' keep the dimension of the value they yield when that value comes out of a sub... | ✅ |
| 385 | `cond_expr_receiver.obs` | Other | A conditional expression used as a method receiver -- '(t ? "abcdef" : "xy")-> SubString(1, 2)' -... | ✅ |
| 386 | `core_abstract_virtual.obs` | Core Language | core abstract virtual | ✅ |
| 387 | `core_arithmetic.obs` | Core Language | Core Arithmetic Operations Test Tests basic arithmetic operations, type conversions, and operator... | ✅ |
| 388 | `core_array_element_interface_return.obs` | Core Language | Regression: a call on an array element whose result needs a class cast. 'c : Compare := rows[i]->... | ✅ |
| 389 | `core_array_interface_widening.obs` | Core Language | Regression: an array of a class where an array of an interface it implements is wanted. A String[... | ✅ |
| 390 | `core_array_literal_mixed_strings.obs` | Core Language | Regression: an array literal mixing string literals with other expressions. 'v := "xyz"; texts :=... | ✅ |
| 391 | `core_array_literal_nil_element.obs` | Core Language | Regression: Nil as an element of an array literal. 'row := ["a", Nil, "c"];' was refused, "An arr... | ✅ |
| 392 | `core_array_operations.obs` | Core Language | core array operations | ✅ |
| 393 | `core_arrays_simple.obs` | Core Language | Core Array Operations Test (Simplified) Tests basic array creation, access, and modification | ✅ |
| 394 | `core_basic_type_call_statement.obs` | Core Language | A function of a basic type's class called as a statement: 'Int->Clear(a);'. 'Runtime->Copy(..);'... | ✅ |
| 395 | `core_bitwise_ops.obs` | Core Language | core bitwise ops | ✅ |
| 396 | `core_bool_ops.obs` | Core Language | core bool ops | ✅ |
| 397 | `core_bool_short_circuit.obs` | Core Language | Evaluation order and skipping of `&` and `\|`: left operand first, right only when the left did no... | ✅ |
| 398 | `core_boxing_nil_unbox.obs` | Core Language (neg) | A holder that is Nil has no value. Unboxing it is the 'Nil' dereference that calling 'Get()' on i... | ✅ |
| 399 | `core_boxing_rules.obs` | Core Language | Boxing and unboxing at the boundary between a primitive and its holder class (Int/IntRef, Float/F... | ✅ |
| 400 | `core_break_continue.obs` | Core Language | core break continue | ✅ |
| 401 | `core_calc_chain_conversion.obs` | Core Language | A conversion in a calculation is applied to the VALUE of an operand that is a call chain. 'n * 2.... | ✅ |
| 402 | `core_call_depth.obs` | Core Language | Ordinary deep recursion runs: thousands of frames, not 256. The call stack was a fixed array of 2... | ✅ |
| 403 | `core_call_depth_max.obs` | Core Language | The maximum call depth is enforced: a runaway recursion reaches it and stops with "call stack bou... | ✅ |
| 404 | `core_char_methods.obs` | Core Language | core char methods | ✅ |
| 405 | `core_classes.obs` | Core Language | Core Classes Test Tests class instantiation, inheritance, method calls, and select statements Bas... | ✅ |
| 406 | `core_collections_perf.obs` | Core Language | core collections perf | ✅ |
| 407 | `core_control_flow.obs` | Core Language | Core Control Flow Test Tests if/else, loops, and select statements | ✅ |
| 408 | `core_do_while.obs` | Core Language | core do while | ✅ |
| 409 | `core_do_while_post_op.obs` | Core Language | A post-operation in a `do`/`while` condition must run once per iteration. EmitDoWhile chose betwe... | ✅ |
| 410 | `core_each_loop.obs` | Core Language | core each loop | ✅ |
| 411 | `core_enum.obs` | Core Language | core enum | ✅ |
| 412 | `core_enum_interp.obs` | Core Language | Enum and consts values in strings (v2026.10.0 hardening item C4, decision D4). Through v2026.9.4... | ✅ |
| 413 | `core_function_refs.obs` | Core Language | core function refs | ✅ |
| 414 | `core_generic_bounded_return.obs` | Generics | A program's own bounded generic class (T : Compare) returns its type argument, not its bound. The... | ✅ |
| 415 | `core_generic_compound_bounds.obs` | Generics | Compound generic bounds (T : A & B): a concrete type argument must satisfy ALL bounds. Person imp... | ✅ |
| 416 | `core_generic_fbound.obs` | Generics | F-bounded type-parameter constraint (T : Compare<T>): the bound may be generic and self-referenti... | ✅ |
| 417 | `core_generic_inference.obs` | Generics | Type arguments a constructor call leaves out are inferred, in this order: 1. from the declared ty... | ✅ |
| 418 | `core_generic_structural.obs` | Generics | Exercises the structural generic type comparison: deeply nested generic type arguments must round... | ✅ |
| 419 | `core_generic_variance.obs` | Generics | Declaration-site variance: 'out T' (covariant) lets Producer<Dog> be used where Producer<Animal>... | ✅ |
| 420 | `core_http_server.obs` | Core Language | HTTP Client/Server Loopback Test Tests HTTP GET and POST using raw TCP server + HttpClient. Verif... | ✅ |
| 421 | `core_index_call_chain.obs` | Core Language | An array index that is a call chain is the value its LAST call returns. The index's type was read... | ✅ |
| 422 | `core_inheritance_chain.obs` | Core Language | core inheritance chain | ✅ |
| 423 | `core_int_methods.obs` | Core Language | core int methods | ✅ |
| 424 | `core_interfaces.obs` | Core Language | core interfaces | ✅ |
| 425 | `core_interpolate_nil.obs` | Core Language | An interpolated String or object that is Nil reads as "Nil" instead of stopping the program: "x={... | ✅ |
| 426 | `core_json_escape.obs` | Core Language | core json escape | ✅ |
| 427 | `core_method_overload.obs` | Core Language | core method overload | ✅ |
| 428 | `core_multi_dim_array.obs` | Core Language | core multi dim array | ✅ |
| 429 | `core_net_buffer.obs` | Core Language | Network Buffer Read Test Tests that TCP socket ReadBuffer correctly handles partial reads by veri... | ✅ |
| 430 | `core_nil_array_argument.obs` | Core Language | Regression: a literal Nil passed for an array parameter. With 'function : F(a : String[]) ~ Int',... | ✅ |
| 431 | `core_odbc.obs` | Core Language | Core ODBC Value Classes Test Date, Timestamp and ColumnInfo are pure Objeck. Only Connection, Par... | ✅ |
| 432 | `core_opencv.obs` | Core Language | Core OpenCV Bindings Test Tests helper classes, constants, and VideoWriter FourCC without requiri... | ✅ |
| 433 | `core_paren_method_chain.obs` | Core Language | Verifies a method call on a parenthesized method-call expression chains onto the parenthesized re... | ✅ |
| 434 | `core_records.obs` | Core Language | Core Records Test Exercises record-generated constructors, accessors, mutators, generics, readonl... | ✅ |
| 435 | `core_recursion.obs` | Core Language | Core Recursion Test Tests recursive function calls and tail recursion | ✅ |
| 436 | `core_select_ops.obs` | Core Language | core select ops | ✅ |
| 437 | `core_static_array_literals.obs` | Core Language | Regression test for the static-array literal pool (compiler bug, 2026-06): the bool literal-pool... | ✅ |
| 438 | `core_static_fields.obs` | Core Language | core static fields | ✅ |
| 439 | `core_string_escape_esc.obs` | Core Language | String Escape \e Test "\e" in a string literal is ESC (27) on every platform; a Windows-built obc... | ✅ |
| 440 | `core_string_format.obs` | Core Language | core string format | ✅ |
| 441 | `core_string_interp_expr.obs` | Core Language | Verifies operator expressions inside "{$...}" string interpolation. | ✅ |
| 442 | `core_string_interp_format.obs` | Core Language | Verifies inline format specifiers "{$expr:spec}" in string interpolation. | ✅ |
| 443 | `core_string_interp_literal.obs` | Core Language | Verifies a literal directly inside "{$...}": hex, octal, binary, decimal, float, negative, charac... | ✅ |
| 444 | `core_string_literal_after_branch.obs` | Core Language | A string literal in a condition ('s = "hi"') is consumed by the comparison, so the first literal... | ✅ |
| 445 | `core_string_methods.obs` | Core Language | core string methods | ✅ |
| 446 | `core_strings_simple.obs` | Core Language | Core String Operations Test (Simplified) Tests basic string operations without complex method cha... | ✅ |
| 447 | `core_ternary_call_chain_type.obs` | Core Language | Regression: a '?:' whose first branch is a call chain takes the chain's type. 'return c ? cols->G... | ✅ |
| 448 | `core_thread_gc_stress.obs` | Concurrency/GC | Multithreaded GC stop-the-world stress test. Guards the GC bugs fixed on branch fix/gc-stop-the-w... | ✅ |
| 449 | `core_type_checking.obs` | Core Language | core type checking | ✅ |
| 450 | `core_uninit_loop_local.obs` | Core Language | A local declared without an initializer starts as 0, 0.0, false, Nil or an empty reference every... | ✅ |
| 451 | `cpu_count_affinity.obs` | Other | runtime.cpu.count is the number of CPUs this process may use, not the machine's. A cluster job gi... | ✅ |
| 452 | `critical_exit_unlocks.obs` | Other | Regression: leaving a critical() block by return, break or continue never released its mutex (#11... | ✅ |
| 453 | `csv_median.obs` | Other | CsvRow->Median and CsvColumn->Median return the exact median. Both used to box every value into a... | ✅ |
| 454 | `csv_nil_data.obs` | Other | A CSV file that is not there reports, it does not crash the VM. FileReader->ReadFile returns Nil... | ✅ |
| 455 | `csv_quoting.obs` | Other | Data.CSV reads RFC 4180 quoting and reports malformed rows. CsvTable used to split the text into... | ✅ |
| 456 | `csv_unparsed_table.obs` | Other | An unparsed CsvTable reports, it does not crash the VM. CsvTable->Init only built @header_names w... | ✅ |
| 457 | `ctor_field_promotion.obs` | Other | Constructor field promotion: in a constructor, a parameter written `@name` where the class has al... | ✅ |
| 458 | `curried_calls.obs` | Other | Regression: curried calls (#1184). `F(a)(b)` calls the function value F(a) returns with (b); chai... | ✅ |
| 459 | `dap_databreak_test.obs` | Debugger | Fixture for dap_databreak_test.py. Stops once with everything initialized, then mutates two local... | ✅ |
| 460 | `dap_drilldown_test.obs` | Debugger | dap drilldown test | ✅ |
| 461 | `dap_exception_test.obs` | Debugger (neg) | Triggers an uncaught runtime error (Nil dereference) so the DAP test suite can verify exception b... | ✅ |
| 462 | `dap_frame_eval_test.obs` | Debugger | Fixture for frame-scoped DAP evaluation and hit-count breakpoints. The point of the nesting is th... | ✅ |
| 463 | `dap_setvar_test.obs` | Debugger | Fixture for DAP setVariable through a drill-down handle. A local named 'count' and an object whos... | ✅ |
| 464 | `date_arithmetic.obs` | Date/Time | date arithmetic | ✅ |
| 465 | `date_basic_ops.obs` | Date/Time | date basic ops | ✅ |
| 466 | `date_gmt_calendar.obs` | Date/Time | Regression: System.Time GMT dates were computed as local time (#1179). - Date->New(day, month, ye... | ✅ |
| 467 | `debugger_coll_test.obs` | Debugger | debugger coll test | ✅ |
| 468 | `debugger_eval_test.obs` | Debugger | Fixture for the debugger's expression evaluator and breakpoint bookkeeping. Separate from debugge... | ✅ |
| 469 | `debugger_test.obs` | Debugger | debugger test | ✅ |
| 470 | `debugger_thread_test.obs` | Debugger | Fixture for debugging a program with several live threads. Each worker carries its own id and a l... | ✅ |
| 471 | `diag_concurrent_analysis.obs` | Other | Concurrency guard for the diagnostics/LSP analysis path (#659). The LSP server spawns a worker th... | ✅ |
| 472 | `dir_list_no_dot_entries.obs` | Other | Directory->List returns the same entries on every platform: the files and directories in the dire... | ✅ |
| 473 | `dirty_list_overflow.obs` | Other | Write-heavy old-to-young pointer workload for the write barrier's overflow path. The write barrie... | ✅ |
| 474 | `discarded_call_result_pop.obs` | Other | Regression: a discarded constructor call statement must pop its result. 'Pad->New(i);' as a state... | ✅ |
| 475 | `dtls_bind_address.obs` | Other | A DTLS server can listen on one address, and keeps to it DTLSSocketServer->New(cert, key, passwd,... | ✅ |
| 476 | `dtls_close_races.obs` | Other | Closing a DTLS server or a DTLS socket while other threads are using it The DTLS twin of tls_clos... | ✅ |
| 477 | `dtls_concurrent_clients.obs` | Other | A DTLS server serves a second client while its first is still connected One DTLSSocketServer, one... | ✅ |
| 478 | `dtls_socket_test.obs` | Other | DTLSSocket and DTLSSocketServer, end to end Until this test, nothing had ever run a DTLS call. Th... | ✅ |
| 479 | `dual_stack_servers.obs` | Other | Servers are reachable over IPv4 and IPv6 alike A wildcard server should take clients of either fa... | ✅ |
| 480 | `error_raise.obs` | Other | Raising an Error and reading the one a ?-> region recovered from (#1247, phase 1; docs/ERROR_HAND... | ✅ |
| 481 | `exit_with_compiled_thread_running.obs` | Other | number of lines differs from run to run; the exit status is the assertion Main may return while a... | ✅ |
| 482 | `exit_with_live_threads.obs` | Other | Ending the process while other threads are still running must exit cleanly. Guards #877. A runtim... | ✅ |
| 483 | `field_accessors.obs` | Other | Field accessor modifiers: `@x : get : T` writes the getter, `@x : get set : T` the getter and the... | ✅ |
| 484 | `file_fifo_gc_park.obs` | Other | A read of a FIFO through the file API parks, so a collection elsewhere does not wait for it. Driv... | ✅ |
| 485 | `file_io_edge_cases.obs` | Other | Regression: the smaller File IO defects of #1180. - FileReader->ReadFile returned Nil for an empt... | ✅ |
| 486 | `file_io_flush_eof_delete.obs` | Other | Regression: three file defects (#1180). 1. File->Flush() cleared the file handle before flushing... | ✅ |
| 487 | `filesystem_links.obs` | Other | System.IO.Filesystem.HardLink and SymbolicLink. HardLink->Create had no test and a "TODO: impleme... | ✅ |
| 488 | `fix524_array_cast_chain.obs` | Bug Fix | Fix #524: Cannot chain method calls on array-indexed elements after cast Tests that Get(index)->A... | ✅ |
| 489 | `fix534_substring_crash.obs` | Bug Fix | Fix #534: String->SubString crash on negative or zero length argument Tests that a negative lengt... | ✅ |
| 490 | `fix_array_bounds.obs` | Bug Fix | fix array bounds | ✅ |
| 491 | `fix_chained_calls.obs` | Bug Fix | fix chained calls | ✅ |
| 492 | `fix_deep_recursion.obs` | Bug Fix | fix deep recursion | ✅ |
| 493 | `fix_float_precision.obs` | Bug Fix | fix float precision | ✅ |
| 494 | `fix_int_boundary.obs` | Bug Fix | fix int boundary | ✅ |
| 495 | `fix_large_arrays.obs` | Bug Fix | fix large arrays | ✅ |
| 496 | `fix_nested_generics.obs` | Bug Fix | fix nested generics | ✅ |
| 497 | `fix_nil_chain_ops.obs` | Bug Fix | fix nil chain ops | ✅ |
| 498 | `fix_polymorphic_calls.obs` | Bug Fix | fix polymorphic calls | ✅ |
| 499 | `fix_scope_shadowing.obs` | Bug Fix | fix scope shadowing | ✅ |
| 500 | `fix_string_concat.obs` | Bug Fix | fix string concat | ✅ |
| 501 | `float_constants_abs_binary.obs` | Other | Regression: Float constants, Float->Abs, ToBinaryString and hoisted Float division (#1176, #1178)... | ✅ |
| 502 | `float_literal_range.obs` | Other | Float literals at the edges of the double range compile to exactly the double strtod gives. Out-o... | ✅ |
| 503 | `float_precision_long_lines.obs` | Other | Regression: Float->ToString(v, precision) above 2^63 (#1173) and FileReader->ReadLine on lines lo... | ✅ |
| 504 | `float_tostring_large.obs` | Other | Regression: converting a Float of about 1e57 or more to a String killed the VM. Float->ToString,... | ✅ |
| 505 | `func_closure_field.obs` | Functional | func closure field | ✅ |
| 506 | `func_filter_ops.obs` | Functional | func filter ops | ✅ |
| 507 | `func_higher_order.obs` | Functional | func higher order | ✅ |
| 508 | `func_reduce_ops.obs` | Functional | func reduce ops | ✅ |
| 509 | `func_sort_custom.obs` | Functional | func sort custom | ✅ |
| 510 | `function_types.obs` | Other | Regression: function types (#1183). - An array parameter: `r := Plain(Int[]) ~ Int; r(a)` was "Un... | ✅ |
| 511 | `gc_bool_array_declaration.obs` | Other | Bool[] references must be declared as byte arrays. Bool->New[n] allocates a byte array (NEW_BYTE_... | ✅ |
| 512 | `gc_closure_capture_nursery_end.obs` | Other | A closure's captured young object must survive a collection that runs while the closure is being... | ✅ |
| 513 | `gc_closure_ids_stress.obs` | Other | Many distinct closure ids going live on several threads at once, while minor collections run. Thi... | ✅ |
| 514 | `gc_cond_value_root.obs` | Other | A conditional's object value must survive a minor collection that runs before the value is consum... | ✅ |
| 515 | `gc_cond_value_threads.obs` | Other | A conditional's object value must survive another thread's collection that runs while this thread... | ✅ |
| 516 | `gc_conservative_bad_class.obs` | Other | Guards G5: a conservative root that points INTO a young object must not have its "class pointer"... | ✅ |
| 517 | `gc_deep_chain.obs` | Other | A very long chain of objects must not overflow the collector's native stack CheckObject -> CheckM... | ✅ |
| 518 | `gc_fixed_threshold_scales.obs` | Other | A heap threshold set with --gc-threshold is a trigger, not a promise to collect on every allocati... | ✅ |
| 519 | `gc_heap_sizing.obs` | Other | The threshold for a major collection follows the live heap. After each major collection the next... | ✅ |
| 520 | `gc_inline_conditional_temp.obs` | Other | An inlined method's and/or temporary must be declared as what it holds. A conditional ('c ? a : b... | ✅ |
| 521 | `gc_instance_id_stable.obs` | Other | An object's instance ID survives the collector moving it GetInstanceID() was the object's address... | ✅ |
| 522 | `gc_int_not_relocated.obs` | Other | A minor collection leaves an Int alone, whatever its value (#1452). The collector relocated every... | ✅ |
| 523 | `gc_minor_closure_capture.obs` | Other | Guards G12: a minor GC must trace the captures of a closure stored in an OLD object. A closure's... | ✅ |
| 524 | `gc_minor_str_array_literal.obs` | Other | Guards G13: the elements of a String[] literal must survive a minor GC. A string-array literal is... | ✅ |
| 525 | `gc_mt_small_nursery_stress.obs` | Other | Multithreaded GC stress with thread exits overlapping collections. Guards the crash the stress pr... | ✅ |
| 526 | `gc_nursery_knob.obs` | Other | Nursery knob (--nursery / OBJECK_NURSERY) and the GC statistics it is read with. Guards: 1. runti... | ✅ |
| 527 | `gc_roots_jit_bridge.obs` | Other | Compiled code calling back into the interpreter keeps the collector's stack typing honest (#1475,... | ✅ |
| 528 | `gc_scoped_local_slot_types.obs` | Other | Frame slot types must match what the frame's slots actually hold. The collector (and OBJECK_GC_VE... | ✅ |
| 529 | `gc_verify_reflection.obs` | Other | Reflection objects the VM builds while the nursery is full. The VM allocates the Class, Method an... | ✅ |
| 530 | `gc_zero_field_nursery_end.obs` | Other | A zero-field object allocated just before a collection must survive it. An object's address is th... | ✅ |
| 531 | `gemini_error_message.obs` | Other | gemini error message | ✅ |
| 532 | `gemini_parse_test.obs` | Other | API.Google.Gemini response parsing, tested offline from recorded bodies that follow the public AP... | ✅ |
| 533 | `gemini_query_results.obs` | Other | gemini query results | ✅ |
| 534 | `generic_inference_before_after.obs` | Other | The type-argument inference example of docs/FEATURES.md, kept compiling and running: one small cl... | ✅ |
| 535 | `generic_inference_viable_constructors.obs` | Other | When a generic class has several constructors that take as many arguments, its type arguments are... | ✅ |
| 536 | `generics_chained_calls.obs` | Other | Regression: generic type arguments through chained calls, generic returns and self-referential ge... | ✅ |
| 537 | `get_class_height_test.obs` | Other | Base->GetClass() in loops and branches, and in one thread while another collects. GetClass is 'LO... | ✅ |
| 538 | `gl_context_test.obs` | Other | Regression test for OpenGL 3.3 core support: proves a real context can be created AND that someth... | ✅ |
| 539 | `gl_quaternion_test.obs` | Other | Game.OpenGL Quaternion -- arithmetic only, so it needs no window. Every other GL test in this sui... | ✅ |
| 540 | `hpc_scale_kernel.obs` | Other | The HPC scaling kernel (tools/hpc/alderaan/bench/workloads/scale_kernel.obs): the checksum it pri... | ✅ |
| 541 | `http_body_limit.obs` | Other | http body limit | ✅ |
| 542 | `http_client_reuse.obs` | Other | HttpClient reuses a kept-alive connection, and retries only what reuse can lose (#1443) A request... | ✅ |
| 543 | `http_deadlines.obs` | Other | HttpClient takes a connect timeout, a read timeout and a deadline for a whole request HttpClient... | ✅ |
| 544 | `http_error_body.obs` | Other | HTTP error-response body test A client that reports a status code but no body for a server error... | ✅ |
| 545 | `http_header_flatten_test.obs` | Other | Request-header flattening (Web.HTTP.HeaderCheck->Flatten). HTTP/2 and HTTP/3 hand the request to... | ✅ |
| 546 | `http_header_validation_test.obs` | Other | Request-header validation (Web.HTTP.HeaderCheck). HttpClient->AddHeader was injectable: HTTP/1.1... | ✅ |
| 547 | `http_persistence_test.obs` | Other | http persistence test | ✅ |
| 548 | `http_query_params.obs` | Other | http query params | ✅ |
| 549 | `http_range_requests.obs` | Other | http range requests | ✅ |
| 550 | `http_redirect_credentials.obs` | Other | Credentials do not follow a redirect to another origin (#1453) HttpClient and HttpsClient follow... | ✅ |
| 551 | `http_request_hardening.obs` | Other | http request hardening | ✅ |
| 552 | `http_status_codes.obs` | Other | http status codes | ✅ |
| 553 | `http_stream_to_file.obs` | Other | Large responses: GetToFile streams to disk, and memory reads have no 256 MB cap A model file is h... | ✅ |
| 554 | `http_thread_reap.obs` | Other | http thread reap | ✅ |
| 555 | `https_deadlines.obs` | Other | HttpsClient takes a connect timeout, a read timeout and a deadline for a whole request The TLS tw... | ✅ |
| 556 | `https_persistence_test.obs` | Other | The exit 139 this test reported on linux-arm64 in nightly run 35027973357 is recorded in docs/PLA... | ✅ |
| 557 | `ieee_float_semantics.obs` | Other | IEEE 754 semantics for NaN and infinity, across the interpreter AND the JIT. Windows had two defe... | ✅ |
| 558 | `indexed_call_result.obs` | Other | Subscripting the result of a method call: 'GetItems()[0]->Name()'. This was never implemented, an... | ✅ |
| 559 | `inference_client_test.obs` | Other | API.Inference: tensor bookkeeping, error reporting, and the two wire formats. EXTRA_LIBS: models,... | ✅ |
| 560 | `inline_funcref_param.obs` | Other | A method that takes a func-ref parameter and is small enough for the compiler to inline: the inli... | ✅ |
| 561 | `int64_min_literal.obs` | Other | Int's minimum is writable as a literal. 9223372036854775808 is out of range on its own, so '-9223... | ✅ |
| 562 | `int_semantics.obs` | Other | Int arithmetic has one meaning in the interpreter, both JITs and at every -opt level (docs/FEATUR... | ✅ |
| 563 | `integer_literal_prefixes.obs` | Other | Integer literal prefixes. Octal is written with a '0o' prefix, as hex takes '0x' and binary '0b'.... | ✅ |
| 564 | `interact_accessors_promotion.obs` | Other | Field accessors and constructor field promotion on the same fields, with what they meet: - 'New(@... | ✅ |
| 565 | `interact_agent_prompt_json.obs` | Other | An agent declaration, a prompt literal and a typed result ('class Name : json') in one program, a... | ✅ |
| 566 | `interact_json_literal_boxing.obs` | Other | A JSON literal is the Insert and Add calls it stands for, so a holder (IntRef, FloatRef, BoolRef)... | ✅ |
| 567 | `interact_literals_inference_boxing.obs` | Other | Collection literals, map literals, type-argument inference and boxing in the same statements, eac... | ✅ |
| 568 | `interact_match_is_nil_guard.obs` | Other | 'match' and 'select' as values, the binding type test and the nil-guard, where they meet: - 'x :=... | ✅ |
| 569 | `interact_method_refs_using_spawn.obs` | Other | Method references, 'using', the nil-guard, 'spawn' and 'match' in one flow: - 'Class->Function' a... | ✅ |
| 570 | `interact_spawn_lambda_self.obs` | Other | 'spawn' and a lambda that names its instance, together (the block of a 'spawn' is compiled to a l... | ✅ |
| 571 | `interact_type_test_shadow.obs` | Other | The binding type test with a name that an enclosing block already has: the bound name is declared... | ✅ |
| 572 | `interact_using_assert_try.obs` | Other | 'using', 'assert' and try/finally in the same methods. A log records the order of the closes, the... | ✅ |
| 573 | `interp_chained_call.obs` | Other | A chained method call inside string interpolation. "{$s->ToUpper()->Size()}" segfaulted the VM: n... | ✅ |
| 574 | `interp_float_fastpath.obs` | Other | Exercises the interpreter's inlined float fast-path (ADD/SUB/MUL_FLOAT and the six float comparis... | ✅ |
| 575 | `io_create_new_process.obs` | I/O | File->CreateNew creates a file only when none of that name exists, as one step (O_EXCL on POSIX,... | ✅ |
| 576 | `io_file_basic.obs` | I/O | io file basic | ✅ |
| 577 | `io_rename_replaces.obs` | I/O | File->Rename replaces an existing target on every platform, as POSIX rename() does. On Windows th... | ✅ |
| 578 | `io_unicode_paths.obs` | I/O | File and directory names with non-ASCII characters work on every platform. Objeck strings are UTF... | ✅ |
| 579 | `io_write_error_devfull.obs` | I/O | io write error devfull | ✅ |
| 580 | `io_write_errors.obs` | I/O | io write errors | ✅ |
| 581 | `jagged_arrays.obs` | Other | Jagged arrays (#1246; docs/JAGGED_ARRAYS_DESIGN.md). 'Int[][]' is an array of Int[] rows of any l... | ✅ |
| 582 | `jit_alloc_result_reg.obs` | AMD64/JIT | NEW_OBJ_INST in compiled code, in every shape the AMD64 inline allocator takes since v2026.11.4 (... | ✅ |
| 583 | `jit_array_index_registers.obs` | AMD64/JIT | Multi-dimensional array accesses in JIT-compiled loops: the register that holds a later index is... | ✅ |
| 584 | `jit_array_native.obs` | AMD64/JIT | jit array native | ✅ |
| 585 | `jit_autojit_race.obs` | AMD64/JIT | Auto-JIT concurrency guard. Many threads call the same hot method, crossing the auto-JIT threshol... | ✅ |
| 586 | `jit_bitwise_mem_operand.obs` | AMD64/JIT | Regression for the AMD64 JIT and/or/xor memory-operand encoding (fuzzer F4, seeds 140/48/18/236).... | ✅ |
| 587 | `jit_bounds_message.obs` | AMD64/JIT | An out-of-bounds array index reports the same text from the interpreter and from compiled code: "... | ✅ |
| 588 | `jit_branch_shapes.obs` | AMD64/JIT | Every branch shape the compiler emits for `&`, `\|` and `<>` in conditions, in loops that are JIT-... | ✅ |
| 589 | `jit_bridge_exception.obs` | AMD64/JIT (neg) | A C++ exception thrown inside a call the JIT's bridge made ends the program with the VM's interna... | ✅ |
| 590 | `jit_call_overhead.obs` | AMD64/JIT | F7 guard: a call from compiled code into compiled code must stay cheap. The calling convention (d... | ✅ |
| 591 | `jit_closure_call_in_creating_frame.obs` | AMD64/JIT | JIT: calling a capturing lambda while its creating frame is still on the stack. JitAmd64::Process... | ✅ |
| 592 | `jit_closure_gc_fixup.obs` | AMD64/JIT | Regression for the generational-GC fixup of closure captures (bug B1). The GC mark phase descends... | ✅ |
| 593 | `jit_closure_local_funcvar.obs` | AMD64/JIT | Guards a JIT crash present through v2026.9.4: a capturing lambda stored in a local function varia... | ✅ |
| 594 | `jit_concurrent_compile.obs` | AMD64/JIT | Concurrency guard for the JIT code-page allocator (PageManager::GetPage). Several threads JIT-com... | ✅ |
| 595 | `jit_conditional_native.obs` | AMD64/JIT | jit conditional native | ✅ |
| 596 | `jit_conditional_value_types.obs` | AMD64/JIT | A conditional expression ('c ? a : b') whose value is a Float or a function reference, interprete... | ✅ |
| 597 | `jit_const_char_store.obs` | AMD64/JIT | ARM64 JIT: a constant character stored into a Char[] element was written with a full 8-byte store... | ✅ |
| 598 | `jit_const_division.obs` | AMD64/JIT | n / d and n % d for a constant d give what the divide instruction gives. A compiled method divide... | ✅ |
| 599 | `jit_dispatch_native.obs` | AMD64/JIT | jit dispatch native | ✅ |
| 600 | `jit_dynamic_call_threshold.obs` | AMD64/JIT | A call through a function value is compiled by the auto-JIT on the callee's own call count, whate... | ✅ |
| 601 | `jit_entry_compiled.obs` | AMD64/JIT | jit entry compiled | ✅ |
| 602 | `jit_entry_shapes.obs` | AMD64/JIT | The entry and exit of a compiled method, in the shapes the ARM64 callee work touches (the AMD64 b... | ✅ |
| 603 | `jit_first_call_race.obs` | AMD64/JIT | Auto-JIT first-call race. Several threads make their FIRST call of the same method at once, and t... | ✅ |
| 604 | `jit_float_branch_nan.obs` | AMD64/JIT | Regression: under the x64 JIT, `if(nan = x)` was taken and `if(nan <> x)` was not. The fused comp... | ✅ |
| 605 | `jit_float_compare_store.obs` | AMD64/JIT | A float comparison whose result is STORED must not clobber a live register. `cmov_reg`'s first ac... | ✅ |
| 606 | `jit_float_divide_nan.obs` | AMD64/JIT | Float division by NaN, and division guarded by a Float compare against 0.0, interpreted and JIT-c... | ✅ |
| 607 | `jit_float_equality.obs` | AMD64/JIT | Regression test for float equality compares on array elements (2026-06). The front-end chose EQL_... | ✅ |
| 608 | `jit_float_intensive.obs` | AMD64/JIT | jit float intensive | ✅ |
| 609 | `jit_float_limits.obs` | AMD64/JIT | Float->NaN(), Float->Inf() and Float->NegInf() in JIT-compiled methods. The three are the opcodes... | ✅ |
| 610 | `jit_float_mem_ops.obs` | AMD64/JIT | Float arithmetic and comparison against MEMORY operands, under the JIT. IMPORTANT: must run with... | ✅ |
| 611 | `jit_float_pin_calls.obs` | AMD64/JIT | Pinned Float loop locals survive calls, allocation and a safepoint park. The JIT keeps a loop's h... | ✅ |
| 612 | `jit_float_round_trig.obs` | AMD64/JIT | Exercises two JIT float-codegen bugs that only surface once a method using them is auto-JIT'd (de... | ✅ |
| 613 | `jit_frame_trap_test.obs` | AMD64/JIT | Regression test for the JIT frame-dependent trap crash (2026-06). Traps such as SERL_INT/SERL_FLO... | ✅ |
| 614 | `jit_frame_unreferenced_local.obs` | AMD64/JIT | A compiled method declares a local that no instruction references, ahead of an object-array local... | ✅ |
| 615 | `jit_func_ref_hot.obs` | AMD64/JIT | jit func ref hot | ✅ |
| 616 | `jit_funcref_store_basic_lambda.obs` | AMD64/JIT | JIT: a func-ref store whose two words are different operand kinds. A func-ref variable is two wor... | ✅ |
| 617 | `jit_funcref_store_mixed_words.obs` | AMD64/JIT | JIT: storing a func-ref whose two words have different working-stack shapes. A capturing lambda b... | ✅ |
| 618 | `jit_gc_safepoint.obs` | AMD64/JIT | jit gc safepoint | ✅ |
| 619 | `jit_gc_stress.obs` | AMD64/JIT | JIT + GC interaction stress (2026-06). One CI run on linux-x64 failed with a JIT-to-JIT runtime e... | ✅ |
| 620 | `jit_huge_method_guard_reach.obs` | AMD64/JIT | A method too large for a conditional branch to cross is still run correctly. Big() below is one m... | ✅ |
| 621 | `jit_int_min_immediate.obs` | AMD64/JIT | Adding or subtracting an Int literal in compiled code, at the edges of what an instruction can ho... | ✅ |
| 622 | `jit_loop_native.obs` | AMD64/JIT | jit loop native | ✅ |
| 623 | `jit_native_call_depth.obs` | AMD64/JIT (neg) | Recursion deeper than the call stack allows, through compiled-to-compiled native calls (the calli... | ✅ |
| 624 | `jit_native_call_error.obs` | AMD64/JIT (neg) | A compiled callee, called straight from compiled code (the calling convention's phase 3), derefer... | ✅ |
| 625 | `jit_native_cls_fields.obs` | AMD64/JIT | JIT Native Class Fields Test Tests object reference storage in class instance fields with GC pres... | ✅ |
| 626 | `jit_native_float_array.obs` | AMD64/JIT | JIT Native Float Array Test Tests native function with float array creation and math operations R... | ✅ |
| 627 | `jit_native_func_ref.obs` | AMD64/JIT | JIT Native Function Reference Test Tests native functions with function reference storage in clas... | ✅ |
| 628 | `jit_native_inline.obs` | AMD64/JIT | jit native inline | ✅ |
| 629 | `jit_native_math.obs` | AMD64/JIT | JIT Native Math Builtins Test Tests native math functions: Factorial, Sinh/Cosh/Tanh/Log2/Cbrt, P... | ✅ |
| 630 | `jit_nil_array_clear.obs` | AMD64/JIT | Clearing a Nil array from compiled code is a runtime error like any other use of Nil: a ?-> regio... | ✅ |
| 631 | `jit_nil_inlined_field.obs` | AMD64/JIT (neg) | An inlined getter called on a Nil object must raise the same Nil-dereference error compiled as in... | ✅ |
| 632 | `jit_nil_inlined_field_float.obs` | AMD64/JIT (neg) | Float sibling of jit_nil_inlined_field.obs: an inlined Float getter on a Nil object. At -opt s3 c... | ✅ |
| 633 | `jit_nil_inlined_field_funcref.obs` | AMD64/JIT (neg) | Func-ref sibling of jit_nil_inlined_field.obs: an inlined getter returning a function-reference f... | ✅ |
| 634 | `jit_nil_inlined_field_object.obs` | AMD64/JIT (neg) | Object sibling of jit_nil_inlined_field.obs: an inlined getter returning an object field, on a Ni... | ✅ |
| 635 | `jit_off_interprets_native.obs` | AMD64/JIT | --jit=off runs the interpreter only, methods declared `native` included. A `native` method used t... | ✅ |
| 636 | `jit_pin_callee_saved.obs` | AMD64/JIT | Pinned integer loop locals survive calls, early exits, recursion and a safepoint park. The JIT ke... | ✅ |
| 637 | `jit_recovery_through_bridge.obs` | AMD64/JIT | Regression: `?->` recovery from a fault several frames below a compiled method (#1171, JIT half).... | ✅ |
| 638 | `jit_safepoint_pending_values.obs` | AMD64/JIT | A loop's GC safepoint keeps the values an expression still has pending. The JIT polls for a stop-... | ✅ |
| 639 | `jit_stack_value_across_branch.obs` | AMD64/JIT | JIT: a value left on the working stack across a branch (#1256). `f(a <> Nil & a->Kind() = 6, b <>... | ✅ |
| 640 | `jit_string_ops.obs` | AMD64/JIT | jit string ops | ✅ |
| 641 | `jit_tailcall_loop_recovery.obs` | AMD64/JIT | Regression: `?->` recovery from an error inside a method whose tail call the optimizer turned int... | ✅ |
| 642 | `jit_tco_bare_local.obs` | AMD64/JIT | Regression for the TCO deferred-local-load miscompile (both arches). A self-recursive tail call t... | ✅ |
| 643 | `jit_trap_register_pressure.obs` | AMD64/JIT | Calls whose arguments already fill most of the register pool still compile. A call into the VM (a... | ✅ |
| 644 | `jit_virtual_equals.obs` | AMD64/JIT | Issue #722: on ARM64 the JIT miscompiled String->Equals inside a virtual request-handler callback... | ✅ |
| 645 | `json_build_ops.obs` | JSON | json build ops | ✅ |
| 646 | `json_class_oracle_test.obs` | JSON | The oracle for 'class Name : json' (docs/design/AGENT_SYNTAX.md): the schema the compiler derives... | ✅ |
| 647 | `json_class_test.obs` | JSON | 'class Name : json' (docs/design/AGENT_SYNTAX.md): the compiler writes JsonSchema, JsonFill, From... | ✅ |
| 648 | `json_codec_escapes.obs` | JSON | JsonElement->Decode and Encode agree on every JSON escape. \b and \f were mapped to 0x10 and 0x14... | ✅ |
| 649 | `json_escape_test.obs` | JSON | JsonElement must escape on serialization. Format's STRING branch appended the raw value between t... | ✅ |
| 650 | `json_hand_built_escapes.obs` | JSON | JSON that a library builds by hand is valid JSON whatever the text holds. CsvRow->ToJson pasted e... | ✅ |
| 651 | `json_literal.obs` | JSON | The JSON literal: `json{"name": value, "list": [a, b], "inner": {"x": 1}}` is a `Data.JSON.JsonEl... | ✅ |
| 652 | `json_nil_string_null.obs` | JSON | A Nil String given to JsonElement is JSON's null. `obj->Insert("name", text)` with `text` Nil mad... | ✅ |
| 653 | `json_number_grammar.obs` | JSON | Data.JSON and Data.JSON.Stream number grammar (RFC 8259): -? (0 \| [1-9][0-9]*) (. [0-9]+)? ([eE]... | ✅ |
| 654 | `json_parse_ops.obs` | JSON | json parse ops | ✅ |
| 655 | `json_rpc_client_id.obs` | JSON | Data.JSON.RPC.Client: every request carries its own id, and the reply must echo it. - The id was... | ✅ |
| 656 | `json_rpc_escapes.obs` | JSON | Data.JSON.RPC over a loopback socket: strings are escaped once on the way out and decoded once on... | ✅ |
| 657 | `json_rpc_nil_handling.obs` | JSON | Data.JSON.RPC must not dereference Nil on a request or a reply that leaves a member out. - A requ... | ✅ |
| 658 | `json_serialize_paths.obs` | JSON | JSON serializing and scanning give the same text whichever path produces it. The serializer appen... | ✅ |
| 659 | `json_string_roundtrip.obs` | JSON | Data.JSON strings: the parser decodes, the serializer escapes, and a round trip is the identity (... | ✅ |
| 660 | `lambda_andor_entry_space.obs` | Other | Interpreter frames were one to two words too small for a method at the compiler's local limit. Th... | ✅ |
| 661 | `lambda_capture_after_func.obs` | Other | Regression: a lambda capture that follows a captured function value. Found by the Rosetta batch-3... | ✅ |
| 662 | `lambda_self_fields.obs` | Other | Lambdas in an instance method read and write the instance's fields (@x) and name the instance (@s... | ✅ |
| 663 | `lame_encode_test.obs` | Other | Audio.Lame->PcmToMp3 encodes PCM to MP3. EXTRA_LIBS: lame This library had no runtime test at all... | ✅ |
| 664 | `lang_array_bytes.obs` | Other | Raw conversion between numeric arrays and bytes, one memcpy each: Int->ToBytes / Byte->ToInts and... | ✅ |
| 665 | `lang_array_copy.obs` | Other | Bulk primitive array moves: Int/Float/Byte/Char->Copy(dest, dest_offset, src, src_offset, count)... | ✅ |
| 666 | `lang_bytebuffer_written_bytes.obs` | Other | System.ByteBuffer covers the bytes written, not its capacity. ToString hex-dumped the whole backi... | ✅ |
| 667 | `lang_compress_deflate.obs` | Other | Byte->CompressDeflate/UncompressDeflate are raw deflate (RFC 1951), and the web server's response... | ✅ |
| 668 | `lang_compress_level.obs` | Other | Byte->CompressZlib(input, level), and Serializer->Serialize compressing at level 1. Serialize use... | ✅ |
| 669 | `lang_float_exact.obs` | Other | Float->ToShortString: the shortest text that reads back as the same Float. Float->ToString prints... | ✅ |
| 670 | `lang_inflate_limit.obs` | Other | Byte->UncompressDeflate(input, limit) and Byte->UncompressGzip(input, limit) inflate at most 'lim... | ✅ |
| 671 | `lang_tofloat_range.obs` | Other | String->ToFloat on out-of-range text, and String->ToInt's prefixes under the JIT. ToFloat went th... | ✅ |
| 672 | `lang_trap_method_returns.obs` | Other | Library methods with a return type return a value. lang.obs is compiled by the bootstrap compiler... | ✅ |
| 673 | `lib_alias_vision.obs` | Other | The -lib alias @vision links: it is closed under its members' dependencies. EXTRA_LIBS: @vision A... | ✅ |
| 674 | `lib_interface_unqualified.obs` | Other | A library class's interface does not depend on the program's `use` list Inside bundle System.ML,... | ✅ |
| 675 | `lib_uninit_gemini.obs` | Other | A reply part of a kind the Gemini library does not model (here a function call with no callback r... | ✅ |
| 676 | `lib_uninit_nlp_json.obs` | Other | Two library bugs that depended on a local declared without a value inside a loop keeping the prev... | ✅ |
| 677 | `lib_uninit_query.obs` | Other | Query.Structured bugs that depended on a local declared without a value inside a loop keeping the... | ✅ |
| 678 | `library_nil_input_test.obs` | Other | Library entry points that take text must treat Nil, and a document that is not what it should be,... | ✅ |
| 679 | `library_nil_response_test.obs` | Other | A request that gets no answer must not dereference Nil, tested offline against a closed local por... | ✅ |
| 680 | `library_response_helpers_test.obs` | Other | A reply with no body, or an error reply of an unexpected shape, must not dereference Nil (offline... | ✅ |
| 681 | `literal_and_call_indexing.obs` | Other | Regression: subscripting a literal or a call result (#1184). - `["a", "b"][1]` did not parse: the... | ✅ |
| 682 | `llm_util_extract_test.obs` | Other | API.LlmUtil JSON extraction from chatty model replies and the lenient repair. The repair table sh... | ✅ |
| 683 | `llm_util_schema_test.obs` | Other | API.LlmUtil JSON Schema validation, as a table of (schema, value, expected errors). Each expected... | ✅ |
| 684 | `llm_util_structured_test.obs` | Other | API.LlmUtil StructuredCall against a scripted fake ChatFn: a reply that is valid the first time,... | ✅ |
| 685 | `llm_util_template_test.obs` | Other | API.LlmUtil prompt templates and the prompt builder: every placeholder form, the filters, loops,... | ✅ |
| 686 | `llm_util_tokens_test.obs` | Other | API.LlmUtil token estimation and the context window. The estimator is checked on fixed samples: t... | ✅ |
| 687 | `loop_literal_condition.obs` | Other | Loops whose condition is a Bool literal, a Bool variable or a call. A 'for' whose condition was a... | ✅ |
| 688 | `lsp_features.obs` | LSP | lsp features | ✅ |
| 689 | `map_literal.obs` | Other | The map literal: `m : Map<String, IntRef> := {"a": 1, "b": 2};` builds the map where it is writte... | ✅ |
| 690 | `map_literal_inferred_values.obs` | Other | A constructor call that is the value of a map literal's entry takes its type arguments from the m... | ✅ |
| 691 | `match_exhaustive_returns.obs` | Other | A method may end in a 'match' on an enum that names every item and returns from every arm, with n... | ✅ |
| 692 | `match_negative_offset_enum.obs` | Other | An enum numbered from a negative value is an enum to 'match'. `enum Level := -20 { Low, Mid, High... | ✅ |
| 693 | `match_shared_arm.obs` | Other | An arm that several labels share behaves the same for each of them. `match n { 0 \| 1 => i++, othe... | ✅ |
| 694 | `match_statement.obs` | Other | 'match' as a statement: `match subject { pattern \| pattern => { ... } other => { ... } }`. It is... | ✅ |
| 695 | `match_subject_forms.obs` | Other | What 'match' may be followed by. It is a keyword only where a name could not be followed by what... | ✅ |
| 696 | `match_subject_postfix.obs` | Other | A postfix update in the subject of a 'match' or 'select' runs once, whichever arm is taken, and t... | ✅ |
| 697 | `match_value.obs` | Other | 'match' used as a value: `x := match subject { pattern \| pattern => value, other => value };` It... | ✅ |
| 698 | `match_value_gc.obs` | Other | A 'match' used as a value runs its select in the middle of the enclosing expression, so whatever... | ✅ |
| 699 | `math_bigint.obs` | Math | System.Math.BigInt: exact integer arithmetic at any size. EXTRA_LIBS: math Objeck's Int is 64-bit... | ✅ |
| 700 | `math_complex.obs` | Math | System.Math.Complex: complex numbers with Float parts. EXTRA_LIBS: math What is pinned here are t... | ✅ |
| 701 | `math_float_ops.obs` | Math | math float ops | ✅ |
| 702 | `math_log_exp.obs` | Math | math log exp | ✅ |
| 703 | `math_random_ops.obs` | Math | math random ops | ✅ |
| 704 | `math_rational.obs` | Math | System.Math.Rational: exact fractions, backed by BigInt. EXTRA_LIBS: math Backed by BigInt rather... | ✅ |
| 705 | `math_rounding.obs` | Math | math rounding | ✅ |
| 706 | `math_sqrt_ops.obs` | Math | math sqrt ops | ✅ |
| 707 | `math_trig_funcs.obs` | Math | math trig funcs | ✅ |
| 708 | `mcp_agent_deadline_test.obs` | MCP Server | The run's deadline reaches a tool that runs on the loop's own thread (API.Agent, API.MCP), agains... | ✅ |
| 709 | `mcp_agent_test.obs` | MCP Server | Hands declared functions and MCP servers to an agent (McpAgentTools), against a scripted model, s... | ✅ |
| 710 | `mcp_cancel_before_validation_test.obs` | MCP Server | A tools/call can be cancelled from the moment the server has it, not only once its arguments were... | ✅ |
| 711 | `mcp_core_test.obs` | MCP Server | The MCP library (API.MCP) with no process and no network beyond loopback: a server in the same pr... | ✅ |
| 712 | `mcp_debug_test.obs` | MCP Server | DEBUG VERSION of mcp_server_test.obs Identical to programs/regression/mcp_server_test.obs except:... | ✅ |
| 713 | `mcp_declare_test.obs` | MCP Server | Declaring MCP tools from types (McpToolSpec), the effect annotations, the per-tool counters, the... | ✅ |
| 714 | `mcp_http_sessionless_cancel_test.obs` | MCP Server | The streamable HTTP transport with sessions off (McpService->SetSessions(false)): no client can c... | ✅ |
| 715 | `mcp_res_prompt_tags_test.obs` | MCP Server | Functions marked with a resource line or a prompt line in their doc comment become MCP resources... | ✅ |
| 716 | `mcp_server_bad_config.obs` | MCP Server | McpServer->Serve with a configuration it cannot use. Serve used to read config->Get("http_port"),... | ✅ |
| 717 | `mcp_server_bound_port_test.obs` | MCP Server | McpServer->Serve with an "http_port" of 0: the system chooses the port and McpServer->GetBoundPor... | ✅ |
| 718 | `mcp_server_malformed_requests.obs` | MCP Server | McpRequestHandler->ProcessPost read client-supplied JSON without checks: root_json->Get("method")... | ✅ |
| 719 | `mcp_server_test.obs` | MCP Server | mcp server test | ✅ |
| 720 | `mcp_stdio_fixture.obs` | MCP Server | The server (and a client) that mcp_stdio_test.py drives over standard input and output. Run by th... | ✅ |
| 721 | `mcp_tool_dollar_test.obs` | MCP Server | A dollar sign in the doc comment of a function marked as a tool reaches the tool as written. The... | ✅ |
| 722 | `mcp_tool_tags_scope_test.obs` | MCP Server | Tagged functions whose types and defaults the compiler has to write back as source, and whose typ... | ✅ |
| 723 | `mcp_tool_tags_test.obs` | MCP Server | Functions marked with an '@tool' line in their doc comment become MCP tools: the compiler writes... | ✅ |
| 724 | `method_generics.obs` | Other | Method-level generics, phase 1 (#1242; docs/METHOD_GENERICS_DESIGN.md). A function or method decl... | ✅ |
| 725 | `method_generics_library.obs` | Other | Method-level generics in libraries (#1242, phase 2; docs/METHOD_GENERICS_DESIGN.md). A library's... | ✅ |
| 726 | `method_ref_sugar.obs` | Other | Method references without ceremony: `Class->Function` where a FuncRef is expected. The signature... | ✅ |
| 727 | `minor_gc_stress.obs` | Other | Regression for generational MINOR GC: old objects holding young references. 'keep' is an object a... | ✅ |
| 728 | `ml_activations.obs` | System.ML | Numerical correctness of the System.ML activation functions and scalers. Softmax normalized over... | ✅ |
| 729 | `ml_adaboost_test.obs` | System.ML | Regression tests for System.ML AdaBoost (overhaul phase 3): boosting over boolean decision stumps... | ✅ |
| 730 | `ml_api_test.obs` | System.ML | Regression tests for the System.ML estimator API consistency sweep (item 11): RandomForest Fit (r... | ✅ |
| 731 | `ml_base_rate_groups.obs` | System.ML | Metrics->PrecisionAtBaseRate, ShuffleLabels, RecallByGroup and UnseenGroups. PrecisionAtBaseRate... | ✅ |
| 732 | `ml_boost_accuracy.obs` | System.ML | System.ML.Boost against scikit-learn and XGBoost. The datasets are generated here with System.ML.... | ✅ |
| 733 | `ml_boost_hand.obs` | System.ML | System.ML.Boost on cases small enough to work out on paper, so the expected numbers are exact (th... | ✅ |
| 734 | `ml_boost_perf.obs` | System.ML | System.ML.Boost performance guard: 10,000 rows x 20 features. Fits depth-4 trees (learning rate 0... | ✅ |
| 735 | `ml_boost_persist.obs` | System.ML | System.ML.Boost model files and strings. Store/Load (a file) and Export/Import (hex text) must gi... | ✅ |
| 736 | `ml_boost_properties.obs` | System.ML | System.ML.Boost properties that must hold whatever the data: the training loss never goes up (squ... | ✅ |
| 737 | `ml_clip_encoder.obs` | System.ML | System.ML.Clip ClipEncoder end to end on a tiny ONNX "image tower", and the pair scoring that fee... | ✅ |
| 738 | `ml_clip_pairs.obs` | System.ML | System.ML.Clip FacePairs: the LFW pair-list reader, the CSV reader and the list operations. Needs... | ✅ |
| 739 | `ml_clip_preprocess.obs` | System.ML | System.ML.Clip ClipPreprocessor against Hugging Face's CLIPImageProcessor (Pillow). data/clip/gen... | ✅ |
| 740 | `ml_cnn_cuda.obs` | System.ML | System.ML.Cnn on the GPU, the optional CUDA backend: unit tests. Every test here compares the GPU... | ✅ |
| 741 | `ml_cnn_cuda_fallback.obs` | System.ML | System.ML.Cnn without a GPU: what a machine that has no CUDA library, no NVIDIA driver or no GPU... | ✅ |
| 742 | `ml_cnn_cuda_parity.obs` | System.ML | System.ML.Cnn on the GPU (the optional CUDA backend) against PyTorch and against the CPU engine.... | ✅ |
| 743 | `ml_cnn_dataset.obs` | System.ML | System.ML.Cnn ImageDataset, Augmentation and CnnTrainer. * the synthetic set is the same for one... | ✅ |
| 744 | `ml_cnn_edges.obs` | System.ML | System.ML.Cnn edge cases and exact-value checks. * parameter counts against the closed form (conv... | ✅ |
| 745 | `ml_cnn_gradcheck.obs` | System.ML | Finite-difference gradient checks of System.ML.Cnn: every layer type and whole networks. The chec... | ✅ |
| 746 | `ml_cnn_loader.obs` | System.ML | System.ML.Cnn image loading: the decoding worker threads and the prefetch of the next batch. * th... | ✅ |
| 747 | `ml_cnn_parity.obs` | System.ML | System.ML.Cnn against PyTorch. data/cnn_parity/*.txt are written by tools/ml/cnn_parity.py from P... | ✅ |
| 748 | `ml_cnn_parity64.obs` | System.ML | System.ML.Cnn against PyTorch in float64, at 64 x 64, over 20 Adam steps. The float32 parity test... | ✅ |
| 749 | `ml_cnn_train.obs` | System.ML | System.ML.Cnn learns: the baseline architecture (three Conv 3x3 + ReLU + MaxPool blocks with 16,... | ✅ |
| 750 | `ml_column_sum_fractions.obs` | System.ML | Matrix2D->SumColumn and Matrix2D->AverageColumn started their accumulator as an Int (`sum := 0;`)... | ✅ |
| 751 | `ml_cross_val_oof.obs` | System.ML | CrossValidation: out-of-fold scores, the fold row mapping, stratification by a key other than the... | ✅ |
| 752 | `ml_dbscan_test.obs` | System.ML | Regression tests for System.ML DBSCAN (overhaul phase 3): two dense blobs plus far-away outliers... | ✅ |
| 753 | `ml_decision_tree_classifier.obs` | System.ML | DecisionTreeClassifier: continuous splits chosen by Gini. DecisionTree takes Bool[,], so continuo... | ✅ |
| 754 | `ml_degenerate_ratios.obs` | System.ML | Degenerate inputs must not take the VM down. Float division by zero RAISES in Objeck rather than... | ✅ |
| 755 | `ml_embedding_classifier.obs` | System.ML | EmbeddingClassifier: a logistic or multi-layer head over fixed-size embeddings, trained by miniba... | ✅ |
| 756 | `ml_experiment_runner.obs` | System.ML | CrossValidation's experiment runner: ScoreModel, Split and Evaluate. Two seams made every experim... | ✅ |
| 757 | `ml_feature_scaler_stateful.obs` | System.ML | FeatureScaler's stateful Fit/Transform pair. The static StandardScaler standardizes whatever matr... | ✅ |
| 758 | `ml_forest_lab_params.obs` | System.ML | The random-forest parameters a lab sweeps, on DecisionTreeClassifier and RandomForestClassifier:... | ✅ |
| 759 | `ml_forest_score_auc.obs` | System.ML | RandomForest->PredictScore (votes_true / ntrees) and AUC against hand-computed values. ml_score_m... | ✅ |
| 760 | `ml_gbt_classifier.obs` | System.ML | GradientBoostedClassifier: boosting under logistic loss. The regression GradientBoostedTrees fits... | ✅ |
| 761 | `ml_gbt_test.obs` | System.ML | Regression tests for System.ML gradient boosting (overhaul phase 3 leftover): a RegressionTree le... | ✅ |
| 762 | `ml_gmm_test.obs` | System.ML | Regression tests for System.ML GaussianMixture (overhaul phase 3): EM on two well-separated blobs... | ✅ |
| 763 | `ml_input_validation.obs` | System.ML | Fit, Predict, Transform and Score on a Nil, an empty or a wrong-width matrix, or on targets that... | ✅ |
| 764 | `ml_kdtree_test.obs` | System.ML | Regression tests for System.ML KDTree (overhaul phase 3): for several queries and k values over a... | ✅ |
| 765 | `ml_kernel_svm_edges.obs` | System.ML | KernelSVM edge cases, parameter semantics and determinism. Bad input is refused with a reason (on... | ✅ |
| 766 | `ml_kernel_svm_geometry.obs` | System.ML | KernelSVM against known geometry, with the optimality (KKT) conditions checked after every fit fr... | ✅ |
| 767 | `ml_kernel_svm_reference.obs` | System.ML | KernelSVM against an independent reference, without Python. Generated by tools/ml/svm_reference/g... | ✅ |
| 768 | `ml_kmeans_group_order.obs` | System.ML | KMeansGrouping lists its groups in a defined order: KMeans->Group keeps the order of its labels (... | ✅ |
| 769 | `ml_kmeans_groups.obs` | System.ML | KMeans->Group returned empty groups, and GetDunnIndex then divided by zero. Group filed each reco... | ✅ |
| 770 | `ml_knn_exact.obs` | System.ML | System.ML KNearestNeighbors->Query keeps the best k rows in a heap instead of sorting every row.... | ✅ |
| 771 | `ml_knn_rounded_ties.obs` | System.ML | System.ML KNearestNeighbors->Query reports a distance (the square root of the squared sum), and r... | ✅ |
| 772 | `ml_lab_chart_svg.obs` | System.ML | System.ML.Lab.Chart: standalone SVG line, bar and loss-curve charts. Three kinds of check. Struct... | ✅ |
| 773 | `ml_lab_experiment.obs` | System.ML | System.ML.Lab.Experiment: a sweep over one parameter, with repeats. The Trial here returns predic... | ✅ |
| 774 | `ml_lab_labels_encoding.obs` | System.ML | System.ML.Lab.Labels, and the NSL-KDD encoding path through TableEncoder and FeatureScaler. The l... | ✅ |
| 775 | `ml_lab_non_finite.obs` | System.ML | System.ML.Lab non-finite values: a NaN, +Infinity or -Infinity in a ResultTable cell, deviation o... | ✅ |
| 776 | `ml_lab_resample.obs` | System.ML | System.ML.Lab.Resample: a training set at a requested positive ratio, with the test set left exac... | ✅ |
| 777 | `ml_lab_result_table.obs` | System.ML | System.ML.Lab.ResultTable: the table a sweep hands in, rendered four ways and read back from two... | ✅ |
| 778 | `ml_lab_result_table_formats.obs` | System.ML | System.ML.Lab.ResultTable number formats: scientific notation, a "<0.0001" form for tiny p-values... | ✅ |
| 779 | `ml_lab_split.obs` | System.ML | System.ML.Lab.Partition: a deterministic, stratified hold-out split. The counts are exact, not ap... | ✅ |
| 780 | `ml_lab_stopwatch.obs` | System.ML | System.ML.Lab.Stopwatch: wall-clock milliseconds that accumulate across Start/Stop pairs until Re... | ✅ |
| 781 | `ml_library_test.obs` | System.ML | ml library test | ✅ |
| 782 | `ml_linearclf_test.obs` | System.ML | Regression tests for the System.ML linear classifiers (overhaul phase 2): Perceptron (mistake-dri... | ✅ |
| 783 | `ml_load_corrupt_model.obs` | System.ML | A model file that exists but is not a model must make Load answer Nil, not end the program. Every... | ✅ |
| 784 | `ml_load_truncated_model.obs` | System.ML | A model file that is valid zlib but short, or that claims more than it holds, makes Load answer N... | ✅ |
| 785 | `ml_matrix_reader_columns.obs` | System.ML | MatrixReader's target_offset is the number of target columns, taken from the end of each row. Its... | ✅ |
| 786 | `ml_matrix_reader_line_endings.obs` | System.ML | MatrixReader reads rows through CsvTable, which used to split on CRLF unless it was handed anothe... | ✅ |
| 787 | `ml_matrix_shape_mismatch.obs` | System.ML | Matrix2D operations on operands they cannot combine must return Nil, not fault. The native matrix... | ✅ |
| 788 | `ml_nn_test.obs` | System.ML | Regression tests for the System.ML NeuralNetwork with hidden/output bias vectors (ML overhaul ite... | ✅ |
| 789 | `ml_one_class_svm.obs` | System.ML | OneClassSVM: the nu property, the optimality conditions, sign conventions and novelty detection.... | ✅ |
| 790 | `ml_one_class_svm_reference.obs` | System.ML | OneClassSVM against an independent reference, without Python. Generated by tools/ml/svm_reference... | ✅ |
| 791 | `ml_one_class_svm_tolerance.obs` | System.ML | OneClassSVM: why the flagged training fraction can exceed nu at the default tolerance, and that i... | ✅ |
| 792 | `ml_outlier_autoencoder.obs` | System.ML | System.ML DenseNetwork and AutoencoderDetector against an independent numpy reference. The refere... | ✅ |
| 793 | `ml_outlier_dbscan.obs` | System.ML | System.ML DbscanDetector: DBSCAN's noise as an OutlierDetector. The outliers must be exactly the... | ✅ |
| 794 | `ml_outlier_envelope.obs` | System.ML | System.ML EllipticEnvelope (FastMCD) against independent references. * Chi-square quantiles again... | ✅ |
| 795 | `ml_outlier_iforest.obs` | System.ML | System.ML IsolationForest. The average path length c(n) is checked exactly against the formula. A... | ✅ |
| 796 | `ml_outlier_lof.obs` | System.ML | System.ML LocalOutlierFactor against an independent brute-force reference. LOF is deterministic,... | ✅ |
| 797 | `ml_outlier_mcd.obs` | System.ML | System.ML EllipticEnvelope's MCD search against an independent reference, on 30 contaminated near... | ✅ |
| 798 | `ml_outlier_ocsvm.obs` | System.ML | System.ML OneClassSvmDetector: OneClassSVM through the OutlierDetector interface. The adapter mus... | ✅ |
| 799 | `ml_outlier_sweep.obs` | System.ML | System.ML ConfusionMatrix, Metrics->FalsePositiveRate and SensitivitySweep. ConfusionMatrix is ch... | ✅ |
| 800 | `ml_outlier_zscore.obs` | System.ML | System.ML ZScoreDetector against an independent reference. The expected scores are numpy's (scipy... | ✅ |
| 801 | `ml_paired_bootstrap.obs` | System.ML | ml paired bootstrap | ✅ |
| 802 | `ml_pca_gnb_test.obs` | System.ML | Regression tests for System.ML PCA (power-iteration decomposition: dominant diagonal direction re... | ✅ |
| 803 | `ml_phase1_test.obs` | System.ML | Regression tests for the System.ML correctness fixes (phase 1): seedable PRNG, DotSigmoid dimensi... | ✅ |
| 804 | `ml_random_forest_classifier.obs` | System.ML | RandomForestClassifier: bootstrapped, feature-sampled continuous trees, averaged. Two properties... | ✅ |
| 805 | `ml_regularized_test.obs` | System.ML | Regression tests for the System.ML regularized linear models (overhaul phase 2): RidgeRegression... | ✅ |
| 806 | `ml_report_build.obs` | System.ML | System.ML.Report Report and ResultTables: escaping, numbering, tables, figures, files and the deg... | ✅ |
| 807 | `ml_report_csv_manifest.obs` | System.ML | System.ML.Report: reading a CSV without ending the program, and the pieces a study report needs b... | ✅ |
| 808 | `ml_report_golden.obs` | System.ML | System.ML.Report golden files: the miniature assignment of programs/examples/ml/study_report.obs... | ✅ |
| 809 | `ml_report_replicates.obs` | System.ML | System.ML.Report Replicates: the statistics a hand-in reports, against numpy, scipy and statsmode... | ✅ |
| 810 | `ml_score_metrics.obs` | System.ML | Score-based metrics: RecallAtFpr, ThresholdAtFpr, AucRoc, AveragePrecision and the two curves. Me... | ✅ |
| 811 | `ml_sort_depth.obs` | System.ML | System.ML's two hand-written quicksorts: KDTree's SortByDim (sorts row indexes by one coordinate)... | ✅ |
| 812 | `ml_stratified_kfold.obs` | System.ML | CrossValidation->StratifiedKFold: folds that keep the class ratio, from a seed. The existing KFol... | ✅ |
| 813 | `ml_table_encoder.obs` | System.ML | TableEncoder: CsvTable to Float[,] with a vocabulary fixed at Fit. Two properties carry this test... | ✅ |
| 814 | `ml_threshold_bootstrap.obs` | System.ML | Metrics->AtThreshold and Metrics->Bootstrap, plus Percentile underneath them. AtThreshold is chec... | ✅ |
| 815 | `ml_timeseries_arima.obs` | System.ML | System.ML.TimeSeries: ARIMA(p, d, q) with optional seasonal differencing. Series with known param... | ✅ |
| 816 | `ml_timeseries_edges.obs` | System.ML | System.ML.TimeSeries: edge cases and invariances. Series shorter than the period, constant series... | ✅ |
| 817 | `ml_timeseries_eval.obs` | System.ML | System.ML.TimeSeries: accuracy metrics, rolling-origin cross-validation, interval coverage and au... | ✅ |
| 818 | `ml_timeseries_smoothing.obs` | System.ML | System.ML.TimeSeries: the baseline forecasters and exponential smoothing. Baselines are checked o... | ✅ |
| 819 | `ml_timeseries_transform.obs` | System.ML | System.ML.TimeSeries: the container, the transforms, autocorrelation and the stationarity test. H... | ✅ |
| 820 | `ml_trees_test.obs` | System.ML | Regression tests for the System.ML tree models: the real recursive DecisionTree (left/right child... | ✅ |
| 821 | `ml_vector_edges.obs` | System.ML | System.ML.VectorSearch VectorIndex edges that the reference-comparison test does not reach. Magni... | ✅ |
| 822 | `ml_vector_exact.obs` | System.ML | System.ML.VectorSearch VectorIndex: Search is exact. For cosine, dot and L2 the top-k ids and sco... | ✅ |
| 823 | `ml_vector_parallel.obs` | System.ML | System.ML.VectorSearch VectorIndex->SetThreads: a search split over several threads must return e... | ✅ |
| 824 | `ml_vector_parallel_full.obs` | System.ML | System.ML.VectorSearch VectorIndex->Search over the whole index (k = the number of vectors, and k... | ✅ |
| 825 | `ml_vector_persist.obs` | System.ML | System.ML.VectorSearch VectorIndex Store and Load. A saved index must load back to an index that... | ✅ |
| 826 | `ml_vector_validation.obs` | System.ML | System.ML.VectorSearch VectorIndex: bad arguments return a value and never end the program. Check... | ✅ |
| 827 | `ml_verify_metrics.obs` | System.ML | System.ML.Verification against independent numbers. cases.txt holds 14 sets of scores and same /... | ✅ |
| 828 | `named_arguments.obs` | Other | Named arguments, phase 1 (#1244; docs/NAMED_ARGUMENTS_DESIGN.md). `name: value` binds an argument... | ✅ |
| 829 | `named_arguments_library.obs` | Other | Named arguments to library methods (#1244, phase 2; docs/NAMED_ARGUMENTS_DESIGN.md). A library's... | ✅ |
| 830 | `native_gc_barrier_test.obs` | Other | A value returned by a native library must survive a collection. A C++ shared library returns a va... | ✅ |
| 831 | `native_gc_leak_test.obs` | Other | Objects a native library returns must be RECLAIMED, not merely reachable. native_gc_barrier_test.... | ✅ |
| 832 | `native_lib_gc_park.obs` | Other | Blocking calls in native libraries must park for garbage collection A stop-the-world collection w... | ✅ |
| 833 | `net_io_gc_park.obs` | Other | Blocking I/O must park for garbage collection: DTLS, HTTP/3, UDP, named pipes and Runtime->Comman... | ✅ |
| 834 | `net_resolve_failure.obs` | Other | TCPSocket->Resolve failure-path test Resolve() freed its addrinfo result on the FAILURE path, whe... | ✅ |
| 835 | `new_array_indexed_size.obs` | Other | Regression: `T->New[a[i]]` was compiled as a COPY of the array a, not as an array of a[i] element... | ✅ |
| 836 | `nil_coalesce_call_arguments.obs` | Other | Regression: words left on the operand stack by `??` chains and by statements with a parenthesised... | ✅ |
| 837 | `nil_coalesce_default_postfix.obs` | Other | A postfix update in the default of '??' runs only when the default is evaluated. `chosen := curre... | ✅ |
| 838 | `nil_coalesce_values.obs` | Other | Regression: `??` after a guarded call that yields a value type (#1172). `??` tested its left side... | ✅ |
| 839 | `nil_guard.obs` | Other | Nil-guard-and-bail: `x := E ?? return V;`, `?? break;`, `?? continue;` and the block form `x := E... | ✅ |
| 840 | `nil_guard_element.obs` | Other | The nil guard of an assignment to an array element: 'cells[i] := E ?? return V;' leaves when the... | ✅ |
| 841 | `nil_safe_call_index.obs` | Core Language | A subscript on a call that follows '?->' or '??' selects the element. `(provider ?? fallback)->It... | ✅ |
| 842 | `nil_safe_ops.obs` | Core Language | Nil-safe operators: '??' (nil-coalesce) and '?->' (nil-safe call). Both desugar onto existing int... | ✅ |
| 843 | `nil_safe_statement.obs` | Core Language | Regression: a `?->` call used as a statement (#1113). `t?->Down();` was emitted link by link as p... | ✅ |
| 844 | `nlp_api_sequence.obs` | System.NLP | ApiSequenceTokenizer: a vocabulary of API names built from training traces, and the encodings of... | ✅ |
| 845 | `nlp_bpe_tokenizer.obs` | System.NLP | BpeTokenizer: byte-level BPE (RoBERTa, GPT-2), id for id and offset for offset with Hugging Face'... | ✅ |
| 846 | `nlp_levenshtein_bounded.obs` | System.NLP | System.NLP TextSimilarity->LevenshteinDistance: the two-row version must equal the textbook full... | ✅ |
| 847 | `nlp_levenshtein_ceiling.obs` | System.NLP | System.NLP TextSimilarity->LevenshteinDistance with a ceiling: an effectively unbounded ceiling (... | ✅ |
| 848 | `nlp_onnx_embedder.obs` | System.NLP | BertEmbedder: sentence embeddings from a BERT-shaped ONNX model, checked against the model's clos... | ✅ |
| 849 | `nlp_onnx_layers.obs` | System.NLP | BertEmbedder layer pooling: the final layer (as always), the sum of the last four hidden layers a... | ✅ |
| 850 | `nlp_onnx_text_classifier.obs` | System.NLP | TextClassifier: a fine-tuned classifier opened from its model folder, checked against Python. EXT... | ✅ |
| 851 | `nlp_porter.obs` | System.NLP | System.NLP Porter stemmer (docs/NLP_DESIGN.md, section 4): the worked examples from Porter's 1980... | ✅ |
| 852 | `nlp_sentiment.obs` | System.NLP | System.NLP sentiment (docs/NLP_DESIGN.md, section 5): each VADER-style rule on its own (negation,... | ✅ |
| 853 | `nlp_spell_features.obs` | System.NLP | System.NLP SpellChecker and TextFeatures, with values worked out by hand. SpellChecker: a word li... | ✅ |
| 854 | `nlp_tfidf.obs` | System.NLP | System.NLP TF-IDF and similarity (docs/NLP_DESIGN.md, sections 6 and 7): a refit starts from an e... | ✅ |
| 855 | `nlp_tfidf_ngrams.obs` | System.NLP | System.NLP TF_IDF n-grams, min_df and max_features, checked against scikit-learn. The expected vo... | ✅ |
| 856 | `nlp_tokenize.obs` | System.NLP | System.NLP tokenization (docs/NLP_DESIGN.md, sections 2 and 3): contractions, numbers, hyphenated... | ✅ |
| 857 | `nlp_topics_bson.obs` | System.NLP | System.NLP.Topics BsonReader: the decoder, the streaming reader, field extraction, and what it do... | ✅ |
| 858 | `nlp_topics_bson_pymongo.obs` | System.NLP | System.NLP.Topics BsonReader against pymongo's bson package. The bytes below were produced by bso... | ✅ |
| 859 | `nlp_topics_dictionary.obs` | System.NLP | System.NLP.Topics text side: stopword lists, the tokenizer, TokenDictionary (counts, FilterExtrem... | ✅ |
| 860 | `nlp_topics_lda.obs` | System.NLP | System.NLP.Topics Lda: the model against independently computed values, planted-topic recovery, d... | ✅ |
| 861 | `nlp_topics_metrics.obs` | System.NLP | System.NLP.Topics TopicMetrics: u_mass, c_npmi and c_v coherence, topic diversity, overlap and st... | ✅ |
| 862 | `nlp_topics_plots.obs` | System.NLP | System.NLP.Topics TopicPlots: the data behind the static topic report and the SVG itself. Checked... | ✅ |
| 863 | `nlp_wordpiece.obs` | System.NLP | WordPieceTokenizer: BERT's BasicTokenizer and WordPiece, and the encodings a BERT-family model ta... | ✅ |
| 864 | `nlp_wordpiece_parity.obs` | System.NLP | WordPieceTokenizer gives the same ids as BERT's own tokenizer on a corpus: 234 texts, each in thr... | ✅ |
| 865 | `oauth_test.obs` | Networking | oauth test | ✅ |
| 866 | `obj_size_layout.obs` | Other | Object field layout must fit the object allocation exactly. The compiler records a class's instan... | ✅ |
| 867 | `odbc_prepared_reuse.obs` | ODBC | A Database.ODBC ParameterStatement runs as many times as its caller likes. The statement's column... | ✅ |
| 868 | `odbc_sqlite_test.obs` | ODBC | ODBC SQLite Integration Test Tests live database operations against an in-memory SQLite database:... | ✅ |
| 869 | `odbc_whole_values.obs` | ODBC | Database.ODBC reads and writes whole values, and says why a SELECT failed. Four defects in the OD... | ✅ |
| 870 | `ollama_api_test.obs` | Other | API.Ollama against recorded payloads, no server needed: chat tool calling (the tool_calls reply,... | ✅ |
| 871 | `ollama_http_status_test.obs` | Other | API.Ollama and the HTTP status of a reply, against a scripted server on loopback (no model, no ne... | ✅ |
| 872 | `ollama_parse_test.obs` | Other | Completion->ParseGenerateResponse: the response handling behind every Completion->Generate overlo... | ✅ |
| 873 | `onnx_concurrency.obs` | Other | One ONNX Runtime session used by several Objeck threads, beside a thread that makes garbage. EXTR... | ✅ |
| 874 | `onnx_concurrency_gpu.obs` | Other | The same on the GPU: several Objeck threads share one CUDA or TensorRT session. Skips without a G... | ✅ |
| 875 | `onnx_failed_session.obs` | Other | A failed ONNX session is detectable, and inference refuses instead of aborting. EXTRA_LIBS: model... | ✅ |
| 876 | `onnx_float16.obs` | Other | float16, bfloat16 and 8-bit element types in API.Onnx. EXTRA_LIBS: models,onnx,opencv,cipher,json... | ✅ |
| 877 | `onnx_generic_run.obs` | Other | The generic ONNX run entry point: its refusals, and the tensor bookkeeping around it. EXTRA_LIBS:... | ✅ |
| 878 | `onnx_gpu_options.obs` | Other | GPU provider options: the CUDA and TensorRT option sets, CUDA graphs and the engine cache. EXTRA_... | ✅ |
| 879 | `onnx_iobinding.obs` | Other | IO binding: DeviceTensor and BoundSession. EXTRA_LIBS: models,onnx,opencv,cipher,json A normal Se... | ✅ |
| 880 | `onnx_iobinding_gpu.obs` | Other | IO binding on the GPU: the checks of onnx_iobinding.obs, repeated on ep=cuda and ep=tensorrt, plu... | ✅ |
| 881 | `onnx_provider_select.obs` | Other | The ONNX execution provider is chosen per session with "ep", the runtime can be asked what it car... | ✅ |
| 882 | `onnx_runtime_test.obs` | Other | API.Onnx.OnnxRuntime->GetProviders() reaches the native ONNX Runtime. EXTRA_LIBS: models,onnx,ope... | ✅ |
| 883 | `onnx_session_options.obs` | Other | Session options in the config map, and the TensorRT provider's refusal. EXTRA_LIBS: models,onnx,o... | ✅ |
| 884 | `onnx_tiny_bert_model.obs` | Other | The tiny BERT-shaped test models under data/bert run on this platform's ONNX Runtime and compute... | ✅ |
| 885 | `openai_parse_test.obs` | Other | API.OpenAI response parsing, tested offline from recorded bodies that follow the public API refer... | ✅ |
| 886 | `openai_request_test.obs` | Other | API.OpenAI request bodies, built offline (no key, no network). Defect this guards: Responses.Resp... | ✅ |
| 887 | `openai_tool_calls_test.obs` | Other | API.OpenAI Chat Completions tool calls over the wire, against a local HTTPS server that answers w... | ✅ |
| 888 | `opencv_batch_unload.obs` | Other | opencv batch unload | ✅ |
| 889 | `opt_arith_identities.obs` | Other | Integer identities and multiply-by-a-power-of-two (-opt s2 and s3), checked at the edges. The com... | ✅ |
| 890 | `opt_dead_store_chain.obs` | Other | Regression: a dead store inside a chained assignment kept its value on the stack. seen := false;... | ✅ |
| 891 | `opt_dead_store_first_instr.obs` | Other | Regression: a dead store at a method's first instruction kept its value. Found by rosetta_atomic_... | ✅ |
| 892 | `opt_dead_store_liveness.obs` | Other | Dead-store elimination on the control-flow graph: what goes and what must stay. DeadStore used to... | ✅ |
| 893 | `opt_dead_store_side_effects.obs` | Other | Regression: dead-store elimination dropped side effects. Found by the differential fuzzer against... | ✅ |
| 894 | `opt_dead_store_stack_balance.obs` | Other | Regression: optimizer passes that left values on the operand stack. Found by the fuzzer against v... | ✅ |
| 895 | `opt_funcref_local_slots.obs` | Other | Optimizer slot numbering with a function-reference local. A func-ref local takes two slots, but t... | ✅ |
| 896 | `opt_inline_and_or_slots.obs` | Other | An object local of a method inlined at -opt s3 must stay a traced root. The inliner appends the c... | ✅ |
| 897 | `opt_inline_label_collision.obs` | Other | Two inlined bodies must not share a label space (#1037). A method's labels are numbered from its... | ✅ |
| 898 | `opt_inline_later_class.obs` | Other | Inlining must not depend on which class a callee is in. ItermediateOptimizer::Optimize used to op... | ✅ |
| 899 | `opt_inline_locals_zeroed.obs` | Other | An inlined call starts with its locals zeroed, as a call does (#1464). A local declared without a... | ✅ |
| 900 | `opt_int_division.obs` | Other | Integer division must give the same answer at every optimization level. Two -opt rewrites produce... | ✅ |
| 901 | `opt_licm_cfg.obs` | Other | Loop-invariant code motion on the control-flow graph: what moves and what must not. LICM moves a... | ✅ |
| 902 | `opt_licm_store_hoist.obs` | Other | Loop-invariant code motion must not hoist a store that did not always run first. The optimizer mo... | ✅ |
| 903 | `parser_gaps.obs` | Other | Regression: parse gaps found by the Rosetta batch-3 tasks (#1184). - An array literal ate the com... | ✅ |
| 904 | `pipe_closed_peer.obs` | Other | A write to a pipe whose other end has closed fails; it does not end the VM On POSIX a Pipe is a U... | ✅ |
| 905 | `plot_edge_cases.obs` | Other | System.Plot edge cases found by an independent review, each reproduced before it was fixed. The g... | ✅ |
| 906 | `plot_sdl.obs` | Other | System.Plot.SDL: the SDL2 backend, headless. SDL_VIDEODRIVER and SDL_AUDIODRIVER are set to "dumm... | ✅ |
| 907 | `plot_svg.obs` | Other | System.Plot: scatter, histogram, line and bar charts as standalone SVG. Four kinds of check. Stru... | ✅ |
| 908 | `plot_svg_more.obs` | Other | System.Plot, second set: heatmap, confusion matrix, box plot, ROC and precision-recall curves, te... | ✅ |
| 909 | `plot_view.obs` | Other | System.Plot interaction model: what a window does with the mouse, tested without one. Zoom and pa... | ✅ |
| 910 | `postfix_update_conditional_operand.obs` | Other | A postfix update written in an operand that only one path evaluates runs when that operand is eva... | ✅ |
| 911 | `postfix_update_if_condition.obs` | Other | A postfix update in an 'if' condition runs once, whichever branch is taken. `if(i++ = 5) { .. } e... | ✅ |
| 912 | `postfix_update_statement_end.obs` | Other | Every postfix update written in a statement has run when the statement ends. An update ('i++', 'i... | ✅ |
| 913 | `primitive_receiver_call_result.obs` | Other | Regression: a primitive method called on a CALL RESULT got its arguments reversed. Found by the R... | ✅ |
| 914 | `primitive_receiver_order.obs` | Other | Argument order for instance-style calls on primitives. Writing `v->Pow(10)` on a primitive does n... | ✅ |
| 915 | `primitive_receiver_resolution.obs` | Other | Calls on primitive receivers resolve to the $-class function that takes the receiver as parameter... | ✅ |
| 916 | `prompt_literal_test.obs` | Other | Prompt literals (docs/design/AGENT_SYNTAX.md, section 2): 'prompt "..."' with {name} and {name :... | ✅ |
| 917 | `query_aggregates.obs` | Query.Structured | Regression tests for Query.Structured (query.obl), which had none: - Average, Sum, Min, Max and C... | ✅ |
| 918 | `query_archives_in_sql.obs` | Query.Structured | Query.Structured: archives and workbooks named in SQL (docs/QUERY_DESIGN.md, "Archives and workbo... | ✅ |
| 919 | `query_bridges.obs` | Query.Structured | Query.Structured's bridges (docs/QUERY_DESIGN.md section 7, phase 4c): a table or a view out to C... | ✅ |
| 920 | `query_catalog_concurrent.obs` | Query.Structured | Query.Structured: one Catalog shared by several threads (docs/QUERY_DESIGN.md, "Concurrent reader... | ✅ |
| 921 | `query_differential.obs` | Query.Structured | Query.Structured differential test (docs/QUERY_DESIGN.md, "Testing"). Random tables (every column... | ✅ |
| 922 | `query_expr_concurrent.obs` | Query.Structured | Query.Structured: expressions under one Catalog shared by several threads (docs/QUERY_DESIGN.md,... | ✅ |
| 923 | `query_expr_differential.obs` | Query.Structured | Query.Structured expressions, differentially (docs/QUERY_DESIGN.md, "Expressions in every clause"... | ✅ |
| 924 | `query_expr_grouped.obs` | Query.Structured | Query.Structured: expressions in grouped queries (docs/QUERY_DESIGN.md, "Expressions in every cla... | ✅ |
| 925 | `query_expr_join.obs` | Query.Structured | Query.Structured: expressions in JOIN ... ON (docs/QUERY_DESIGN.md, "Expressions in every clause"... | ✅ |
| 926 | `query_expr_where.obs` | Query.Structured | Query.Structured: expressions in WHERE and ORDER BY (docs/QUERY_DESIGN.md, "Expressions in every... | ✅ |
| 927 | `query_expr_zone_maps.obs` | Query.Structured | Query.Structured expressions across row-group and batch boundaries (docs/QUERY_DESIGN.md, "Reduct... | ✅ |
| 928 | `query_file_rewritten.obs` | Query.Structured | Query.Structured: a file rewritten within the second it was read, to the same size, is read again... | ✅ |
| 929 | `query_index_basic.obs` | Query.Structured | Query.Structured indexes (docs/QUERY_DESIGN.md, section 5, phase 6): hash and sorted indexes on e... | ✅ |
| 930 | `query_index_concurrent.obs` | Query.Structured | Query.Structured: one table queried from several threads right after a write (docs/QUERY_DESIGN.m... | ✅ |
| 931 | `query_index_differential.obs` | Query.Structured | Query.Structured indexes, differentially (docs/QUERY_DESIGN.md, sections 5 and "Testing"). For a... | ✅ |
| 932 | `query_index_plan.obs` | Query.Structured | Query.Structured's rule-based planner (docs/QUERY_DESIGN.md, section 5, phase 6): the plan a view... | ✅ |
| 933 | `query_nan_compare.obs` | Query.Structured | NaN in Query.Structured conditions and sorts (found by query_index_differential). The comparison... | ✅ |
| 934 | `query_net_range.obs` | Query.Structured | Query.Structured reading an .otab file over HTTP (docs/QUERY_DESIGN.md, section 10, phase 5), aga... | ✅ |
| 935 | `query_net_read.obs` | Query.Structured | Query.Structured reading files over HTTP (docs/QUERY_DESIGN.md, section 10, phase 5), against an... | ✅ |
| 936 | `query_net_serve.obs` | Query.Structured | Query.Structured serving tables (docs/QUERY_DESIGN.md, section 10, phase 5), against an in-proces... | ✅ |
| 937 | `query_otab_gmt_groups.obs` | Query.Structured | Query.Structured's .otab file: a Date column whose row groups mix GMT and local dates. Each group... | ✅ |
| 938 | `query_persist_crash.obs` | Query.Structured | Query.Structured's .otab saves are crash safe (docs/OTAB_FORMAT.md, "Writing a file"): Save and A... | ✅ |
| 939 | `query_persist_indexes.obs` | Query.Structured | Query.Structured: index definitions stored in .otab files (docs/OTAB_FORMAT.md, format 2; docs/QU... | ✅ |
| 940 | `query_persist_lock.obs` | Query.Structured | Query.Structured's writer lock (QUERY_DESIGN.md, phase 3b): opening a table for writing takes 'pa... | ✅ |
| 941 | `query_persist_log.obs` | Query.Structured | Query.Structured's durable tables (docs/OTAB_FORMAT.md, "The change log"; QUERY_DESIGN.md, phase... | ✅ |
| 942 | `query_persist_roundtrip.obs` | Query.Structured | Query.Structured's .otab file (docs/QUERY_DESIGN.md, section 9, phase 3): a table with every colu... | ✅ |
| 943 | `query_select_expressions.obs` | Query.Structured | Query.Structured: arithmetic in the select list (docs/QUERY_DESIGN.md, section 4, "Expressions in... | ✅ |
| 944 | `query_serialize_generic.obs` | Query.Structured | Query.Structured tables through the language's own Serializer and Deserializer (docs/QUERY_DESIGN... | ✅ |
| 945 | `query_server_concurrent.obs` | Query.Structured | Query.Structured's TableServer answering requests at once (docs/QUERY_DESIGN.md, "A server that a... | ✅ |
| 946 | `query_source_archive.obs` | Query.Structured | Query.Structured ArchiveSource (docs/QUERY_DESIGN.md, section 6, phase 7): a zip, a tar and a gzi... | ✅ |
| 947 | `query_source_csv.obs` | Query.Structured | Query.Structured CsvSource (docs/QUERY_DESIGN.md, section 6): whole-column typing, empty cells, d... | ✅ |
| 948 | `query_source_files_in_sql.obs` | Query.Structured | Query.Structured files named in SQL (docs/QUERY_DESIGN.md, section 6, phase 4): a quoted path in... | ✅ |
| 949 | `query_source_filesystem.obs` | Query.Structured | Query.Structured FilesystemSource and Finder (docs/QUERY_DESIGN.md, section 6): a flat and a recu... | ✅ |
| 950 | `query_source_gzip.obs` | Query.Structured | Query.Structured reads gzipped sources (docs/QUERY_DESIGN.md, section 6, phase 2): a '.gz' CSV, J... | ✅ |
| 951 | `query_source_http.obs` | Query.Structured | Query.Structured HttpJsonSource (docs/QUERY_DESIGN.md, sections 6 and 10, phase 5), against an in... | ✅ |
| 952 | `query_source_json.obs` | Query.Structured | Query.Structured JsonSource (docs/QUERY_DESIGN.md, section 6, phase 4): an array of objects as a... | ✅ |
| 953 | `query_source_ml.obs` | Query.Structured | Query.Structured's System.ML source, MlResultSource (docs/QUERY_DESIGN.md section 6, tier 2, quer... | ✅ |
| 954 | `query_source_objects.obs` | Query.Structured | Query.Structured ObjectSource (docs/QUERY_DESIGN.md, section 6): the program's own objects as a t... | ✅ |
| 955 | `query_source_odbc.obs` | Query.Structured | Query.Structured's ODBC source, OdbcSource (docs/QUERY_DESIGN.md section 6, tier 2, query_odbc.ob... | ✅ |
| 956 | `query_source_text.obs` | Query.Structured | Query.Structured TextSource and JsonLinesSource (docs/QUERY_DESIGN.md, section 6, phase 2): a log... | ✅ |
| 957 | `query_source_tree.obs` | Query.Structured | Query.Structured over a directory tree (docs/QUERY_DESIGN.md, section 6, phase 4): the recursive... | ✅ |
| 958 | `query_source_vision.obs` | Query.Structured | Query.Structured's vision source, VisionSource (docs/QUERY_DESIGN.md section 6, tier 2, query_vis... | ✅ |
| 959 | `query_source_xlsx.obs` | Query.Structured | Query.Structured XlsxSource (docs/QUERY_DESIGN.md, section 6, phase 7): worksheets of an .xlsx wo... | ✅ |
| 960 | `query_source_xml.obs` | Query.Structured | Query.Structured XmlSource (docs/QUERY_DESIGN.md, section 6, phase 4): row elements found at any... | ✅ |
| 961 | `query_sql_relational.obs` | Query.Structured | Query.Structured's relational SQL (docs/QUERY_DESIGN.md, sections 4 and 6, phase 4): GROUP BY wit... | ✅ |
| 962 | `query_storage.obs` | Query.Structured | Query.Structured column storage (docs/QUERY_DESIGN.md, section 1, phase 1): typing and promotion,... | ✅ |
| 963 | `query_thread_errors.obs` | Query.Structured | Query.Structured: GetError reports the calling thread's last error (docs/QUERY_DESIGN.md, "Concur... | ✅ |
| 964 | `query_unknown_column.obs` | Query.Structured | Query.Structured: a name that is not a column is an error in every clause. The defect: 'select re... | ✅ |
| 965 | `query_url_cache_processes.obs` | Query.Structured | Query.Structured's URL download cache shared by several processes (docs/QUERY_DESIGN.md, "One cac... | ✅ |
| 966 | `query_views.obs` | Query.Structured | Query.Structured predicates, views and prepared statements (docs/QUERY_DESIGN.md, sections 2 and... | ✅ |
| 967 | `query_zone_map_wide_ints.obs` | Query.Structured | Zone maps keep each row group's minimum and maximum as Floats. An Int above 2^53 rounds when conv... | ✅ |
| 968 | `rag_adapters_ollama_test.obs` | Other | The RAG kit's Ollama adapters (RagOllamaEmbedder, RagOllamaGenerator) against a fake Ollama serve... | ✅ |
| 969 | `rag_chunking_test.obs` | Other | The RAG kit's chunker (System.ML.Rag.RagChunker): windows by size with overlap, whole paragraphs... | ✅ |
| 970 | `rag_index_test.obs` | Other | The RAG kit's toy embedder, index and persistence (System.ML.Rag). RagHashEmbedder is a hashed ba... | ✅ |
| 971 | `rag_pipeline_test.obs` | Other | The RAG kit's prompt builder, answer and pipeline (System.ML.Rag), end to end with the toy embedd... | ✅ |
| 972 | `recovered_errors.obs` | Other | Reading the error a ?-> region recovered from (#1247; docs/RECOVERED_ERRORS_DESIGN.md). ?-> turns... | ✅ |
| 973 | `ref_parameters.obs` | Other | Reference parameters (#1243; docs/REFERENCE_PARAMETERS_DESIGN.md). function : Swap(ref a : Int, r... | ✅ |
| 974 | `reflection_signatures.obs` | Other | Regression: reflection over method signatures (#1175). - DataType->GetType() returned -1000..-994... | ✅ |
| 975 | `regex_bench.obs` | Regex | regex bench | ✅ |
| 976 | `regex_classes.obs` | Regex | Query.RegEx: character classes (several ranges, negation, punctuation, escapes, literal '-' and '... | ✅ |
| 977 | `regex_dfa_test.obs` | Regex | regex dfa test | ✅ |
| 978 | `regex_lazy_dfa.obs` | Regex | Query.RegEx: the lazily built DFA that finds where a match ends. Covers a pattern that needs more... | ✅ |
| 979 | `regex_repeat_bounds.obs` | Regex | Query.RegEx: the repeat bounds {least} and {least,most}. - a{99999999999} expanded the program at... | ✅ |
| 980 | `regex_replace_escapes.obs` | Regex | Query.RegEx: ReplaceAll matches against the ORIGINAL input (a replacement can not create or remov... | ✅ |
| 981 | `regex_threads.obs` | Regex | Query.RegEx shared by several threads: one RegEx per pattern, used at the same time by every thre... | ✅ |
| 982 | `rosetta_additive_primes.obs` | Other | Rosetta Code: Additive primes https://rosettacode.org/wiki/Additive_primes A prime whose digit su... | ✅ |
| 983 | `rosetta_align_columns.obs` | Other | Rosetta Code: Align columns https://rosettacode.org/wiki/Align_columns Claimed in #1137 Splits th... | ✅ |
| 984 | `rosetta_anonymous_recursion.obs` | Other | Rosetta Code: Anonymous recursion https://rosettacode.org/wiki/Anonymous_recursion Claimed in #11... | ✅ |
| 985 | `rosetta_append_a_record_to_the_end_of_a_text_file.obs` | Other | Rosetta Code: Append a record to the end of a text file https://rosettacode.org/wiki/Append_a_rec... | ✅ |
| 986 | `rosetta_arithmetic_complex.obs` | Other | Rosetta Code: Arithmetic/Complex https://rosettacode.org/wiki/Arithmetic/Complex Claimed in #1164... | ✅ |
| 987 | `rosetta_associative_array_merging.obs` | Other | Rosetta Code: Associative array/Merging https://rosettacode.org/wiki/Associative_array/Merging Cl... | ✅ |
| 988 | `rosetta_atomic_updates.obs` | Other | Rosetta Code: Atomic updates https://rosettacode.org/wiki/Atomic_updates EXTRA_LIBS: gen_collect... | ✅ |
| 989 | `rosetta_balanced_ternary.obs` | Other | Rosetta Code: Balanced ternary https://rosettacode.org/wiki/Balanced_ternary Digits are -1, 0 and... | ✅ |
| 990 | `rosetta_bell_numbers.obs` | Other | Rosetta Code: Bell numbers https://rosettacode.org/wiki/Bell_numbers The number of ways to partit... | ✅ |
| 991 | `rosetta_bernoulli_numbers.obs` | Other | Rosetta Code: Bernoulli numbers https://rosettacode.org/wiki/Bernoulli_numbers Claimed in #1166 E... | ✅ |
| 992 | `rosetta_brace_expansion.obs` | Other | Rosetta Code: Brace expansion https://rosettacode.org/wiki/Brace_expansion Claimed in #1143 Shell... | ✅ |
| 993 | `rosetta_camel_case_and_snake_case.obs` | Other | Rosetta Code: Camel case and snake case https://rosettacode.org/wiki/Camel_case_and_snake_case Cl... | ✅ |
| 994 | `rosetta_cartesian_product.obs` | Other | Rosetta Code: Cartesian product of two or more lists https://rosettacode.org/wiki/Cartesian_produ... | ✅ |
| 995 | `rosetta_catalan_numbers.obs` | Other | Rosetta Code: Catalan numbers https://rosettacode.org/wiki/Catalan_numbers C(n) = C(2n,n)/(n+1).... | ✅ |
| 996 | `rosetta_checkpoint_synchronization.obs` | Other | Rosetta Code: Checkpoint synchronization https://rosettacode.org/wiki/Checkpoint_synchronization... | ✅ |
| 997 | `rosetta_church_numerals.obs` | Other | Rosetta Code: Church numerals https://rosettacode.org/wiki/Church_numerals Claimed in #1122 A Chu... | ✅ |
| 998 | `rosetta_circular_primes.obs` | Other | Rosetta Code: Circular primes https://rosettacode.org/wiki/Circular_primes Every rotation of the... | ✅ |
| 999 | `rosetta_collapsible.obs` | Other | Rosetta Code: Determine if a string is collapsible https://rosettacode.org/wiki/Determine_if_a_st... | ✅ |
| 1000 | `rosetta_combinations.obs` | Other | Rosetta Code: Combinations https://rosettacode.org/wiki/Combinations The k-subsets of 0..n-1 in a... | ✅ |
| 1001 | `rosetta_convert_seconds_to_compound_duration.obs` | Other | Rosetta Code: Convert seconds to compound duration https://rosettacode.org/wiki/Convert_seconds_t... | ✅ |
| 1002 | `rosetta_count_substring.obs` | Other | Rosetta Code: Count occurrences of a substring https://rosettacode.org/wiki/Count_occurrences_of_... | ✅ |
| 1003 | `rosetta_cuban_primes.obs` | Other | Rosetta Code: Cuban primes https://rosettacode.org/wiki/Cuban_primes Primes of the form (n+1)^3 -... | ✅ |
| 1004 | `rosetta_currying.obs` | Other | Rosetta Code: Currying https://rosettacode.org/wiki/Currying Claimed in #1118 A three-argument ad... | ✅ |
| 1005 | `rosetta_date_manipulation.obs` | Other | Rosetta Code: Date manipulation https://rosettacode.org/wiki/Date_manipulation Claimed in #1157 "... | ✅ |
| 1006 | `rosetta_day_of_the_week.obs` | Other | Rosetta Code: Day of the week https://rosettacode.org/wiki/Day_of_the_week Claimed in #1154 The y... | ✅ |
| 1007 | `rosetta_deepcopy.obs` | Other | Rosetta Code: Deepcopy https://rosettacode.org/wiki/Deepcopy Claimed in #1134 EXTRA_LIBS: gen_col... | ✅ |
| 1008 | `rosetta_detect_division_by_zero.obs` | Other | Rosetta Code: Detect division by zero https://rosettacode.org/wiki/Detect_division_by_zero Claime... | ✅ |
| 1009 | `rosetta_digital_root.obs` | Other | Rosetta Code: Digital root https://rosettacode.org/wiki/Digital_root The additive persistence is... | ✅ |
| 1010 | `rosetta_emirp_primes.obs` | Other | Rosetta Code: Emirp primes https://rosettacode.org/wiki/Emirp_primes A prime whose digit reversal... | ✅ |
| 1011 | `rosetta_enforced_immutability.obs` | Other | Rosetta Code: Enforced immutability https://rosettacode.org/wiki/Enforced_immutability Claimed in... | ✅ |
| 1012 | `rosetta_esthetic.obs` | Other | Rosetta Code: Esthetic numbers https://rosettacode.org/wiki/Esthetic_numbers Adjacent digits of a... | ✅ |
| 1013 | `rosetta_exceptions.obs` | Other | Rosetta Code: Exceptions https://rosettacode.org/wiki/Exceptions Claimed in #1123 Define an excep... | ✅ |
| 1014 | `rosetta_exceptions_catch_an_exception_thrown_in_a_nested_call.obs` | Other | Rosetta Code: Exceptions/Catch an exception thrown in a nested call https://rosettacode.org/wiki/... | ✅ |
| 1015 | `rosetta_extreme_floating_point_values.obs` | Other | Rosetta Code: Extreme floating point values https://rosettacode.org/wiki/Extreme_floating_point_v... | ✅ |
| 1016 | `rosetta_fast_fourier_transform.obs` | Other | Rosetta Code: Fast Fourier transform https://rosettacode.org/wiki/Fast_Fourier_transform Claimed... | ✅ |
| 1017 | `rosetta_flatten_a_list.obs` | Other | Rosetta Code: Flatten a list https://rosettacode.org/wiki/Flatten_a_list Arbitrarily nested lists... | ✅ |
| 1018 | `rosetta_formatted_numeric_output.obs` | Other | Rosetta Code: Formatted numeric output https://rosettacode.org/wiki/Formatted_numeric_output Clai... | ✅ |
| 1019 | `rosetta_fusc_sequence.obs` | Other | Rosetta Code: Fusc sequence https://rosettacode.org/wiki/Fusc_sequence fusc(2n) = fusc(n) and fus... | ✅ |
| 1020 | `rosetta_gapful.obs` | Other | Rosetta Code: Gapful numbers https://rosettacode.org/wiki/Gapful_numbers A gapful number is divis... | ✅ |
| 1021 | `rosetta_gaussian_elimination.obs` | Other | Rosetta Code: Gaussian elimination https://rosettacode.org/wiki/Gaussian_elimination Claimed in #... | ✅ |
| 1022 | `rosetta_generic_swap.obs` | Other | Rosetta Code: Generic swap https://rosettacode.org/wiki/Generic_swap Claimed in #1131 Objeck pass... | ✅ |
| 1023 | `rosetta_gray_code.obs` | Other | Rosetta Code: Gray code https://rosettacode.org/wiki/Gray_code Encode with n XOR (n >> 1) and dec... | ✅ |
| 1024 | `rosetta_hailstone.obs` | Other | Rosetta Code: Hailstone sequence https://rosettacode.org/wiki/Hailstone_sequence The sequence for... | ✅ |
| 1025 | `rosetta_hash_join.obs` | Other | Rosetta Code: Hash join https://rosettacode.org/wiki/Hash_join Claimed in #1151 EXTRA_LIBS: gen_c... | ✅ |
| 1026 | `rosetta_hex_dump.obs` | Other | Rosetta Code: Hex dump https://rosettacode.org/wiki/Hex_dump Claimed in #1158 hexdump -C's canoni... | ✅ |
| 1027 | `rosetta_huffman_coding.obs` | Other | Rosetta Code: Huffman coding https://rosettacode.org/wiki/Huffman_coding Claimed in #1152 EXTRA_L... | ✅ |
| 1028 | `rosetta_infinity.obs` | Other | Rosetta Code: Infinity https://rosettacode.org/wiki/Infinity Claimed in #1129 Float is an IEEE do... | ✅ |
| 1029 | `rosetta_integer_overflow.obs` | Other | Rosetta Code: Integer overflow https://rosettacode.org/wiki/Integer_overflow Claimed in #1125 Obj... | ✅ |
| 1030 | `rosetta_isqrt.obs` | Other | Rosetta Code: Isqrt (integer square root) of X https://rosettacode.org/wiki/Isqrt_(integer_square... | ✅ |
| 1031 | `rosetta_jacobsthal_numbers.obs` | Other | Rosetta Code: Jacobsthal numbers https://rosettacode.org/wiki/Jacobsthal_numbers J(n) = J(n-1) +... | ✅ |
| 1032 | `rosetta_kaprekar.obs` | Other | Rosetta Code: Kaprekar numbers https://rosettacode.org/wiki/Kaprekar_numbers A Kaprekar number's... | ✅ |
| 1033 | `rosetta_largest_proper_divisor.obs` | Other | Rosetta Code: Largest proper divisor of n https://rosettacode.org/wiki/Largest_proper_divisor_of_... | ✅ |
| 1034 | `rosetta_last_friday_of_each_month.obs` | Other | Rosetta Code: Last Friday of each month https://rosettacode.org/wiki/Last_Friday_of_each_month Cl... | ✅ |
| 1035 | `rosetta_leonardo_numbers.obs` | Other | Rosetta Code: Leonardo numbers https://rosettacode.org/wiki/Leonardo_numbers L(n) = L(n-1) + L(n-... | ✅ |
| 1036 | `rosetta_longest_common_subsequence.obs` | Other | Rosetta Code: Longest common subsequence https://rosettacode.org/wiki/Longest_common_subsequence... | ✅ |
| 1037 | `rosetta_look_and_say.obs` | Other | Rosetta Code: Look-and-say sequence https://rosettacode.org/wiki/Look-and-say_sequence Each term... | ✅ |
| 1038 | `rosetta_mertens.obs` | Other | Rosetta Code: Mertens function https://rosettacode.org/wiki/Mertens_function The running sum of t... | ✅ |
| 1039 | `rosetta_metered_concurrency.obs` | Other | Rosetta Code: Metered concurrency https://rosettacode.org/wiki/Metered_concurrency Claimed in #11... | ✅ |
| 1040 | `rosetta_mobius.obs` | Other | Rosetta Code: Möbius function https://rosettacode.org/wiki/M%C3%B6bius_function 0 when a square d... | ✅ |
| 1041 | `rosetta_modular_exponentiation.obs` | Other | Rosetta Code: Modular exponentiation https://rosettacode.org/wiki/Modular_exponentiation EXTRA_LI... | ✅ |
| 1042 | `rosetta_modular_inverse.obs` | Other | Rosetta Code: Modular inverse https://rosettacode.org/wiki/Modular_inverse The extended Euclidean... | ✅ |
| 1043 | `rosetta_motzkin_numbers.obs` | Other | Rosetta Code: Motzkin numbers https://rosettacode.org/wiki/Motzkin_numbers M(n) = M(n-1)*(2n+1)/(... | ✅ |
| 1044 | `rosetta_munchausen.obs` | Other | Rosetta Code: Munchausen numbers https://rosettacode.org/wiki/Munchausen_numbers A Munchausen num... | ✅ |
| 1045 | `rosetta_natural_sorting.obs` | Other | Rosetta Code: Natural sorting https://rosettacode.org/wiki/Natural_sorting Claimed in #1139 The t... | ✅ |
| 1046 | `rosetta_negative_base_numbers.obs` | Other | Rosetta Code: Negative base numbers https://rosettacode.org/wiki/Negative_base_numbers A negative... | ✅ |
| 1047 | `rosetta_optional_parameters.obs` | Other | Rosetta Code: Optional parameters https://rosettacode.org/wiki/Optional_parameters Claimed in #11... | ✅ |
| 1048 | `rosetta_partial_function_application.obs` | Other | Rosetta Code: Partial function application https://rosettacode.org/wiki/Partial_function_applicat... | ✅ |
| 1049 | `rosetta_pascals_triangle.obs` | Other | Rosetta Code: Pascal's triangle https://rosettacode.org/wiki/Pascal%27s_triangle Each entry is th... | ✅ |
| 1050 | `rosetta_pell_numbers.obs` | Other | Rosetta Code: Pell numbers https://rosettacode.org/wiki/Pell_numbers P(n) = 2*P(n-1) + P(n-2). Th... | ✅ |
| 1051 | `rosetta_permutations.obs` | Other | Rosetta Code: Permutations https://rosettacode.org/wiki/Permutations All n! orderings, in lexicog... | ✅ |
| 1052 | `rosetta_pernicious.obs` | Other | Rosetta Code: Pernicious numbers https://rosettacode.org/wiki/Pernicious_numbers A pernicious num... | ✅ |
| 1053 | `rosetta_population_count.obs` | Other | Rosetta Code: Population count https://rosettacode.org/wiki/Population_count The count of set bit... | ✅ |
| 1054 | `rosetta_power_set.obs` | Other | Rosetta Code: Power set https://rosettacode.org/wiki/Power_set All 2^n subsets, enumerated by tre... | ✅ |
| 1055 | `rosetta_priority_queue.obs` | Other | Rosetta Code: Priority queue https://rosettacode.org/wiki/Priority_queue Claimed in #1146 EXTRA_L... | ✅ |
| 1056 | `rosetta_range_expansion.obs` | Other | Rosetta Code: Range expansion https://rosettacode.org/wiki/Range_expansion Claimed in #1144 Expan... | ✅ |
| 1057 | `rosetta_read_a_specific_line_from_a_file.obs` | Other | Rosetta Code: Read a specific line from a file https://rosettacode.org/wiki/Read_a_specific_line_... | ✅ |
| 1058 | `rosetta_reflection_list_methods.obs` | Other | Rosetta Code: Reflection/List methods https://rosettacode.org/wiki/Reflection/List_methods Claime... | ✅ |
| 1059 | `rosetta_remove_lines_from_a_file.obs` | Other | Rosetta Code: Remove lines from a file https://rosettacode.org/wiki/Remove_lines_from_a_file Clai... | ✅ |
| 1060 | `rosetta_same_characters.obs` | Other | Rosetta Code: Determine if a string has all the same characters https://rosettacode.org/wiki/Dete... | ✅ |
| 1061 | `rosetta_set.obs` | Other | Rosetta Code: Set https://rosettacode.org/wiki/Set Claimed in #1148 EXTRA_LIBS: gen_collect Creat... | ✅ |
| 1062 | `rosetta_sieve.obs` | Other | Rosetta Code: Sieve of Eratosthenes https://rosettacode.org/wiki/Sieve_of_Eratosthenes The sieve... | ✅ |
| 1063 | `rosetta_square_but_not_cube.obs` | Other | Rosetta Code: Square but not cube https://rosettacode.org/wiki/Square_but_not_cube The squares th... | ✅ |
| 1064 | `rosetta_squeezable.obs` | Other | Rosetta Code: Determine if a string is squeezable https://rosettacode.org/wiki/Determine_if_a_str... | ✅ |
| 1065 | `rosetta_stern_brocot.obs` | Other | Rosetta Code: Stern-Brocot sequence https://rosettacode.org/wiki/Stern-Brocot_sequence Built by a... | ✅ |
| 1066 | `rosetta_symmetric_difference.obs` | Other | Rosetta Code: Symmetric difference https://rosettacode.org/wiki/Symmetric_difference Claimed in #... | ✅ |
| 1067 | `rosetta_tau_function.obs` | Other | Rosetta Code: Tau function https://rosettacode.org/wiki/Tau_function The count of a number's divi... | ✅ |
| 1068 | `rosetta_tokenize_a_string_with_escaping.obs` | Other | Rosetta Code: Tokenize a string with escaping https://rosettacode.org/wiki/Tokenize_a_string_with... | ✅ |
| 1069 | `rosetta_topological_sort.obs` | Other | Rosetta Code: Topological sort https://rosettacode.org/wiki/Topological_sort Claimed in #1147 EXT... | ✅ |
| 1070 | `rosetta_totient.obs` | Other | Rosetta Code: Totient function https://rosettacode.org/wiki/Totient_function Euler's totient, cou... | ✅ |
| 1071 | `rosetta_truncatable_primes.obs` | Other | Rosetta Code: Truncatable primes https://rosettacode.org/wiki/Truncatable_primes Left- and right-... | ✅ |
| 1072 | `rosetta_twos_complement.obs` | Other | Rosetta Code: Two's complement https://rosettacode.org/wiki/Two%27s_complement Claimed in #1126 T... | ✅ |
| 1073 | `rosetta_unique_characters.obs` | Other | Rosetta Code: Determine if a string has all unique characters https://rosettacode.org/wiki/Determ... | ✅ |
| 1074 | `rosetta_utf8_encode_and_decode.obs` | Other | Rosetta Code: UTF-8 encode and decode https://rosettacode.org/wiki/UTF-8_encode_and_decode Claime... | ✅ |
| 1075 | `rosetta_van_eck.obs` | Other | Rosetta Code: Van Eck sequence https://rosettacode.org/wiki/Van_Eck_sequence Each term is the gap... | ✅ |
| 1076 | `rosetta_variadic_function.obs` | Other | Rosetta Code: Variadic function https://rosettacode.org/wiki/Variadic_function Claimed in #1132 O... | ✅ |
| 1077 | `rosetta_word_wrap.obs` | Other | Rosetta Code: Word wrap https://rosettacode.org/wiki/Word_wrap Claimed in #1138 The minimum-lengt... | ✅ |
| 1078 | `rosetta_y_combinator.obs` | Other | Rosetta Code: Y combinator https://rosettacode.org/wiki/Y_combinator Claimed in #1120 The applica... | ✅ |
| 1079 | `rosetta_zeckendorf.obs` | Other | Rosetta Code: Zeckendorf number representation https://rosettacode.org/wiki/Zeckendorf_number_rep... | ✅ |
| 1080 | `rosetta_zero_to_the_zero_power.obs` | Other | Rosetta Code: Zero to the zero power https://rosettacode.org/wiki/Zero_to_the_zero_power Claimed... | ✅ |
| 1081 | `rss_fetch_failure.obs` | Other | Web.RSS: a feed that cannot be fetched leaves the reader not-OK; it does not raise. The defect: R... | ✅ |
| 1082 | `runtime_feature_test.obs` | Other | Regression tests for the "runtime.feature.*" properties, which report which optional protocol eng... | ✅ |
| 1083 | `runtime_gc_stats.obs` | Other | The runtime.* GC statistics must stay inside their own stated ranges. runtime.gc.nursery.occupanc... | ✅ |
| 1084 | `sdl2_bindings.obs` | Other | Regression test for the Game.SDL2 bindings added to close the 2026-10 gap survey: proves each new... | ✅ |
| 1085 | `sdl_game_sprite_clips.obs` | Other | Game.Framework: AnimatedImageSprite->AddClips cuts frames of the requested height. The defect: Ad... | ✅ |
| 1086 | `select_dispatch_test.obs` | Control Flow | Single-case, linear (2-5 cases), jump-table (dense >=6), and binary-tree (sparse) paths | ✅ |
| 1087 | `select_label_constants.obs` | Control Flow | The label forms a select accepts: each is a constant of the same kind as the value. The compiler... | ✅ |
| 1088 | `select_label_order.obs` | Control Flow | Select statements of 1, 2, 3, 4, 5 and 7 sparse cases compile to the same jump labels on every pl... | ✅ |
| 1089 | `select_string_expressions.obs` | Control Flow | A select on a string takes any string expression, not only a variable or a literal. The compiler... | ✅ |
| 1090 | `select_value.obs` | Control Flow | 'select' used as a value, a table of values: `x := select(subject) { label a, b: value label c: v... | ✅ |
| 1091 | `self_typeof_as_chain.obs` | Other | `@self->TypeOf(T)` and `@self->As(T)->M()` (found adding Base->ToString, #1248). EmitVariable ret... | ✅ |
| 1092 | `serial_nil_array_element.obs` | Other | A Nil element inside a serialized object array must come back as Nil in that array, and must not... | ✅ |
| 1093 | `serialize_float_array_type.obs` | Other | A Float[] field read back by the Deserializer is a Float[] to the VM. ObjectDeserializer::Deseria... | ✅ |
| 1094 | `serializer_bool_array.obs` | Other | Serializer->Write(Bool[]) and Deserializer->ReadBoolArray(). A Bool[] is a byte array, one byte p... | ✅ |
| 1095 | `serializer_multidim_arrays.obs` | Other | Regression: the Serializer dropped the tail of multi-dimensional arrays (#1174). An array's heade... | ✅ |
| 1096 | `server_bind_address.obs` | Other | A server can listen on one address instead of every interface TCPSocketServer->New(port) binds th... | ✅ |
| 1097 | `server_listen_error.obs` | Other | A server that cannot listen reports why When a server's port was already taken, Listen failed but... | ✅ |
| 1098 | `server_port_readback.obs` | Other | A server bound to port 0 reports the port the system chose The regression tests used to listen on... | ✅ |
| 1099 | `signal_dispatch.obs` | Other | Signal handlers registered with Runtime->SetSignal run on a VM thread, not inside the operating s... | ✅ |
| 1100 | `socket_accept_class.obs` | Other | An accepted socket is the kind of socket its server makes The VM caches the class ids it allocate... | ✅ |
| 1101 | `socket_accept_closed_server.obs` | Other | Accept() on a TCPSocketServer that is not open answers Nil; it used to end the process The VM's a... | ✅ |
| 1102 | `socket_count_range.obs` | Other | A socket read or write whose count does not fit the socket call is refused, not cut down and done... | ✅ |
| 1103 | `socket_graceful_close_peer_eof.obs` | Other | CloseGracefully() does not keep a peer that reads to the end waiting CloseGracefully() waited for... | ✅ |
| 1104 | `socket_graceful_close_test.obs` | Other | TCPSocket->CloseGracefully() must not lose the data it just wrote (#669). The shape this guards i... | ✅ |
| 1105 | `socket_server_stop_exit_time.obs` | Other | Stopping a server ends its accept thread, so the program leaves at once Closing a listening socke... | ✅ |
| 1106 | `sort_primitive_arrays.obs` | Other | Primitive array sorting: Int, Float, Char and Byte. Int->Sort and its Byte, Char and Float counte... | ✅ |
| 1107 | `spawn_before_after.obs` | Other | The 'spawn' example of docs/FEATURES.md, kept compiling and running: the same work written the wa... | ✅ |
| 1108 | `spawn_gc_stress.obs` | Other | 'spawn' under collection: threads started by 'spawn { ... }' read what they captured while every... | ✅ |
| 1109 | `spawn_thread_body.obs` | Other | 'spawn { ... }': a block that runs on a new thread; the expression is the started System.Concurre... | ✅ |
| 1110 | `static_files_test.obs` | Other | static files test | ✅ |
| 1111 | `stats_corr_ols.obs` | Other | System.Stats Pearson correlation (p value and Fisher interval from scipy.stats.pearsonr) and the... | ✅ |
| 1112 | `stats_degenerate.obs` | Other | System.Stats with bad input: Nil, a single value, zero variance, mismatched lengths, NaN and infi... | ✅ |
| 1113 | `stats_dist.obs` | Other | System.Stats distributions against scipy: Student t (p-value, CDF, quantile) at small and large d... | ✅ |
| 1114 | `stats_ttest.obs` | Other | System.Stats t-tests against scipy.stats.ttest_ind (equal and unequal variance), ttest_rel and tt... | ✅ |
| 1115 | `string_concat_chained_call.obs` | Strings | Regression: '+' concatenation with a chained method call appended the wrong overload. Found by th... | ✅ |
| 1116 | `string_concat_nesting.obs` | Strings | Nested string concatenation. The compiler lowers a concatenation to "allocate a System.String, st... | ✅ |
| 1117 | `string_concat_receiver_and_interp_index.obs` | Strings | Regression: a call on a parenthesised concatenation, and an indexed array in {$...}. Found by the... | ✅ |
| 1118 | `string_dollar_escape.obs` | Strings | The escape \$ in a string is a dollar sign. The scanner accepted it and the escape was not read,... | ✅ |
| 1119 | `string_edge_cases.obs` | Strings | Regression: String library edge cases (#1177), checked against Python. - Split(String) completed... | ✅ |
| 1120 | `string_find_ops.obs` | Strings | string find ops | ✅ |
| 1121 | `string_format_ops.obs` | Strings | Verifies String->Format() positional substitution. | ✅ |
| 1122 | `string_interp_concat.obs` | Strings | An interpolated string as the LEFT operand of a concatenation. "{$a}" + "{$b}" printed AAB. The c... | ✅ |
| 1123 | `string_literal_long_test.obs` | Strings | String array literals longer than a method's operand stack. GENERATED by gen_string_literal_long_... | ✅ |
| 1124 | `string_literal_receiver_nested_call.obs` | Strings | A method call whose receiver is already on the stack when its arguments are emitted -- a string l... | ✅ |
| 1125 | `string_number_conv.obs` | Strings | string number conv | ✅ |
| 1126 | `string_replace_ops.obs` | Strings | string replace ops | ✅ |
| 1127 | `string_split_ops.obs` | Strings | string split ops | ✅ |
| 1128 | `string_wrap_center_split.obs` | Strings | String additions from #1186: SplitWhitespace, Split(delim, escape), Center, Wrap and WrapLines, a... | ✅ |
| 1129 | `stw_watchdog.obs` | Other | Fixture for the stop-the-world watchdog (OBJECK_STW_WATCHDOG_MS), driven by run_vm_flag_tests.py... | ✅ |
| 1130 | `sys_env_command_vars.obs` | Other | Runtime->CommandOutput(command, variables) runs the command with the variables set and puts the e... | ✅ |
| 1131 | `task_scope.obs` | Other | Regression for a structured-concurrency nursery (TaskScope) built purely on the existing System.C... | ✅ |
| 1132 | `tco_receiver.obs` | Other | Tail-call optimization must respect the receiver. TCO used to fire on matching class-id and metho... | ✅ |
| 1133 | `telemetry_contention.obs` | Other | System.Telemetry ContentionRunner with scripted workers: K threads, each calling a function refer... | ✅ |
| 1134 | `telemetry_energy_writers.obs` | Other | System.Telemetry EnergyEstimator with several writers at once: three threads add samples in a tig... | ✅ |
| 1135 | `telemetry_profile.obs` | Other | System.Telemetry profiles: the recorder and its handles, aggregation by model, device and batch,... | ✅ |
| 1136 | `telemetry_sampler.obs` | Other | System.Telemetry resource samplers and the energy estimator, with no GPU and no network: - the nv... | ✅ |
| 1137 | `telemetry_sketch.obs` | Other | System.Telemetry LatencySketch: a quantile estimate must be within the stated relative error of t... | ✅ |
| 1138 | `telemetry_stats.obs` | Other | System.Telemetry SeriesStats and SampleRing against numbers worked out by hand: count, minimum, m... | ✅ |
| 1139 | `thread_accept_exit_test.obs` | Other | A thread parked in accept() must not take the VM down when Main returns (#681). The shape: one th... | ✅ |
| 1140 | `thread_exit_before_start.obs` | Other | Main may return while threads it has just started have not begun to run. The shape is the smalles... | ✅ |
| 1141 | `thread_fault_isolation.obs` | Other | Thread-fault isolation: an uncaught runtime error in a spawned thread ends THAT THREAD, not the p... | ✅ |
| 1142 | `thread_fault_join_many.obs` | Other | Thread-fault isolation: the fault of every ended, not yet joined thread is kept until its Join, h... | ✅ |
| 1143 | `thread_fault_join_own.obs` | Other | Thread-fault isolation: a Join reports the fault of the thread it joined, and of no other, while... | ✅ |
| 1144 | `thread_join_handles.obs` | Other | thread join handles | ✅ |
| 1145 | `thread_raise_exit.obs` | Other | Threads that caught an error end while Main returns An Error a thread raises (or the one GetLastE... | ✅ |
| 1146 | `time_strings_code_points.obs` | Other | Regression: Date strings (#1179, library half) and code points above U+FFFF (#1185). - Date->GetT... | ✅ |
| 1147 | `time_thread_safe.obs` | Other | Dates and file times read from several threads at once. Eight threads each move the same date (1... | ✅ |
| 1148 | `time_timer_wall_clock.obs` | Other | System.Time.Timer measures wall-clock time on every platform. The VM implemented the timer traps... | ✅ |
| 1149 | `tls_accept_deadline.obs` | Other | SetDeadline holds on an accepted TLS connection TCPSecureSocket->SetDeadline(ms) stores the deadl... | ✅ |
| 1150 | `tls_accept_recv_timeout.obs` | Other | A read on an accepted TLS connection does not wait for a silent peer for good A connection accept... | ✅ |
| 1151 | `tls_byte_read_eof.obs` | Other | A Byte[] read of a TLS socket at the end of the stream returns 0 and stores nothing. Driven by ru... | ✅ |
| 1152 | `tls_byte_read_partial.obs` | Other | A TLS Byte[] read that ends early returns the bytes it has, as a TCP read does TCPSecureSocket->R... | ✅ |
| 1153 | `tls_client_recv_timeout.obs` | Other | A TLS client socket times out on a peer that goes quiet; it does not block forever A plain TCP cl... | ✅ |
| 1154 | `tls_close_races.obs` | Other | Closing a TLS server or a TLS socket while other threads are using it A TLS server object and a T... | ✅ |
| 1155 | `tls_concurrent_connections.obs` | Other | TLS connections made at the same time, from several threads, all work A TLS server gives each con... | ✅ |
| 1156 | `tls_connect_gc_park.obs` | Other | TLS and HTTP/2 connects must park for garbage collection A stop-the-world collection waits until... | ✅ |
| 1157 | `tls_connection_cap.obs` | Other | tls connection cap | ✅ |
| 1158 | `tls_errors_and_verify.obs` | Other | TLS failures are reported, and verification cannot be switched off per socket TCPSecureSocket->Ge... | ✅ |
| 1159 | `tls_ip_san.obs` | Other | A client connecting by IP address verifies a certificate that names it A certificate names an IP... | ✅ |
| 1160 | `tls_server_cert_names.obs` | Other | TCPSecureSocketServer->GetIssuer and GetSubject report the server's own certificate The socket an... | ✅ |
| 1161 | `tls_stalled_handshake.obs` | Other | tls stalled handshake | ✅ |
| 1162 | `tls_verify_test.obs` | Other | TLS certificate verification must REFUSE. https_persistence_test proves the happy path (a pinned... | ✅ |
| 1163 | `tls_write_byte_result.obs` | Other | TCPSecureSocket->WriteByte and DTLSSocket->WriteByte say whether they wrote Both are documented "... | ✅ |
| 1164 | `trap_args_leave_stack.obs` | Other | A trap consumes the stack words its bytecode declares (#1457). A TRAP or TRAP_RTRN instruction na... | ✅ |
| 1165 | `trap_array_barrier_test.obs` | Other | A String[] returned by a VM trap must survive a collection. Every trap that returns an array of o... | ✅ |
| 1166 | `trap_array_mt_barrier_test.obs` | Other | A trap-returned array must survive ANOTHER THREAD's allocation. trap_array_barrier_test.obs cover... | ✅ |
| 1167 | `try_blocks.obs` | Exceptions | try / on / finally blocks (#1247, phases 2 and 3; docs/ERROR_HANDLING_DESIGN.md). try { ... } on(... | ✅ |
| 1168 | `try_chain_inline_compare.obs` | Exceptions | A recovered '?->' value compares the same way inline as it does through a local. 1067. The chain... | ✅ |
| 1169 | `try_otherwise.obs` | Exceptions | Try/Otherwise Error Handling Test Tests the Try() and Otherwise() intrinsic methods for error han... | ✅ |
| 1170 | `try_recovers_bounds_mid_args.obs` | Exceptions | Regression: a recovered array-bounds error corrupted the operand stack when the caller had alread... | ✅ |
| 1171 | `try_recovers_cast.obs` | Exceptions | Try() -- and '?->', which is Try() on the chain -- recovers from an invalid object cast, whether... | ✅ |
| 1172 | `try_recovers_depth.obs` | Exceptions | Try() -- and '?->', which is Try() on the chain -- recovers from a full call stack, as it does fr... | ✅ |
| 1173 | `try_recovers_jit_call.obs` | Exceptions | Try() -- and '?->', which is Try() on the chain -- recovers when a COMPILED method calls a COMPIL... | ✅ |
| 1174 | `try_recovery_stack_balance.obs` | Exceptions | Regression: `?->` recovery left the operand stack unbalanced, and `?->` after a call result or a... | ✅ |
| 1175 | `type_test_binding.obs` | Other | The binding type test: `if(value is Type name) { ... }` runs the block when the value is a `Type`... | ✅ |
| 1176 | `typeof_nil_is_false.obs` | Other | `value->TypeOf(Type)` on a Nil value is false, in the interpreter and under the JIT alike. The in... | ✅ |
| 1177 | `udp_departed_client.obs` | Other | A UDP server outlives a departed client; a client hears its peer's refusal A reply to a client th... | ✅ |
| 1178 | `udp_failover.obs` | Other | A UDP client tries each address its name resolves to UDPSocket->New used the first address a name... | ✅ |
| 1179 | `udp_host_names.obs` | Other | A UDP socket takes a host name or an IPv6 address, and a UDP server hears both UDPSocket->New(add... | ✅ |
| 1180 | `unary_minus_parenthesised.obs` | Other | Regression: unary minus on a parenthesised operand (#1099). `-(x)`, `-(F())` and `-(3 + 4)` repor... | ✅ |
| 1181 | `unsigned_literals.obs` | Other | Unsigned integer literals: the 'u'/'U' suffix, and hex/binary read as bit patterns. The suffix ch... | ✅ |
| 1182 | `unsigned_ops.obs` | Other | The '>>>' operator and the unsigned helpers on Int. Objeck stores every integer in a signed 64-bi... | ✅ |
| 1183 | `url_encode_utf8.obs` | Other | url encode utf8 | ✅ |
| 1184 | `using_block.obs` | Other | using(w := E) { ... } (docs/design/USING_BLOCK.md): w->Close() runs when the body ends and before... | ✅ |
| 1185 | `using_block_errors.obs` | Other | using(w := E) { ... } is exception safe (docs/design/USING_BLOCK.md): when an error unwinds past... | ✅ |
| 1186 | `using_block_reassign.obs` | Other | using(w := E) { ... } closes the value E gave (docs/design/USING_BLOCK.md), whatever the body doe... | ✅ |
| 1187 | `verifier_andor_slot_closure.obs` | Other | Load-time verifier false refusal: the and/or slot reused for a closure and a literal. The compile... | ✅ |
| 1188 | `virtual_dispatch_threads.obs` | Other | Virtual calls from several threads at once resolve to the right override. A class caches the over... | ✅ |
| 1189 | `virtual_first_call_threads.obs` | Other | Threads making the first virtual calls on a class at the same moment A class keeps, per virtual d... | ✅ |
| 1190 | `vm_call_depth_limit.obs` | Other | Fixture for run_vm_flag_tests.py section 16 (--call-depth and OBJECK_CALL_DEPTH). With arguments,... | ✅ |
| 1191 | `vm_callback_depth.obs` | Other | A call back into Objeck from inside the VM's C++ runs on the thread's own call stack, bounded lik... | ✅ |
| 1192 | `vm_error_exit.obs` | Other (neg) | A program that dies inside the VM must leave obr with a non-zero exit status. Execute (core/vm/vm... | ✅ |
| 1193 | `vm_error_exit_with_threads.obs` | Other (neg) | A C++ exception that leaves the MAIN thread's interpreter while other threads are running must en... | ✅ |
| 1194 | `vm_exit_flush.obs` | Other | Output printed just before the VM ends the process must reach the reader. Driven by run_vm_flag_t... | ✅ |
| 1195 | `vm_gc_verify_inject.obs` | Other | Fixture for the heap verifier (OBJECK_GC_VERIFY, core/vm/arch/memory_verify.cpp). On its own it i... | ✅ |
| 1196 | `vm_jit_equiv.obs` | Other | The interpreter and the JIT must agree. This program is run twice by run_vm_flag_tests.py -- once... | ✅ |
| 1197 | `vm_lib_path_native.obs` | Other | Fixture for run_vm_flag_tests.py: --lib-path must reach the VM's native-library loader. SHA256 is... | ✅ |
| 1198 | `vm_locale_wide.obs` | Other | Fixture for run_vm_flag_tests.py: obr must run, and write wide characters as UTF-8, under a local... | ✅ |
| 1199 | `vm_parse_ranges.obs` | Other | A float-formatting precision that is not an int is refused where it is set, with an error a ?-> h... | ✅ |
| 1200 | `vm_set_locale_refused.obs` | Other | Runtime->SetLocale with a name the system cannot supply. The VM switched the C library's locale a... | ✅ |
| 1201 | `vm_set_property_first.obs` | Other | A program whose first property access is a set still gets the runtime's own properties. The runti... | ✅ |
| 1202 | `vm_set_property_overwrite.obs` | Other | A runtime property set twice reads back the second value. StackProgram::SetProperty stored with s... | ✅ |
| 1203 | `vm_thread_error_exit.obs` | Other (neg) | A C++ exception that escapes the interpreter on a SPAWNED thread must end the program the way one... | ✅ |
| 1204 | `vm_thread_fault_modes.obs` | Other | Fixture for run_vm_flag_tests.py (check_thread_fault_isolation): what an error nothing catches do... | ✅ |
| 1205 | `vm_thread_stack_pages.obs` | Other | Fixture for run_vm_flag_tests.py section 19: transparent huge pages stay off the stacks of the th... | ✅ |
| 1206 | `vm_trace_fn_param_format.obs` | Other (neg) | The stack trace names each method the way its source declares it, including function-typed parame... | ✅ |
| 1207 | `vm_unicode_args_env.obs` | Other | Fixture for run_vm_flag_tests.py (section 14): arguments, environment variables and command outpu... | ✅ |
| 1208 | `vm_write_char_buffer.obs` | Other | Console->WriteBuffer(Char[]) wrote the buffer twice-encoded, and ignored num. The trap (STD_OUT_C... | ✅ |
| 1209 | `web_downloader_groups.obs` | Other | WebDownloader->Download fetches every URL it is given, however many groups it needs. The download... | ✅ |
| 1210 | `web_server_test.obs` | Other | Web.Server end-to-end coverage. Every method on Web.Server.Request and Response used to call a na... | ✅ |
| 1211 | `websocket_handshake.obs` | Networking | The WebSocket client checks the reply to its opening handshake (RFC 6455 section 4.1) and the fra... | ✅ |
| 1212 | `websocket_secure_frames.obs` | Networking | SecureWebSocket (wss) against a scripted TLS server on loopback: the checks of websocket_handshak... | ✅ |
| 1213 | `websocket_test.obs` | Networking | websocket test | ✅ |
| 1214 | `xml_attribute_order.obs` | XML | XmlElement serializes its attributes in the order they were added, and a parsed element keeps doc... | ✅ |
| 1215 | `xml_build_ops.obs` | XML | xml build ops | ✅ |
| 1216 | `xml_encoding_ops.obs` | XML | Unit tests for the 2026-06 Data.XML improvements: truncated/garbage input is rejected (previously... | ✅ |
| 1217 | `xml_parse_ops.obs` | XML | xml parse ops | ✅ |
| 1218 | `xml_path_position.obs` | XML | Data.XML: in a FindElements path, '[n]' is the n-th match (1-based), as in XPath. The defect: the... | ✅ |
| 1219 | `xml_self_closing_root.obs` | XML | Data.XML: a document whose root element is self-closing parses, and its attributes read. The defe... | ✅ |

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
