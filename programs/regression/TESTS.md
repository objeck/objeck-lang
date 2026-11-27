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


**Total runtime tests: 1178** (plus 14 debugger tests, see below).


## Tests by Category

| Category | Count |
|----------|-------|
| Other | 463 |
| Negative | 232 |
| System.ML | 100 |
| Core Language | 59 |
| AMD64/JIT | 54 |
| Query.Structured | 51 |
| Collections | 27 |
| System.NLP | 21 |
| MCP Server | 16 |
| JSON | 15 |
| Bug Fix | 13 |
| Generics (neg) | 13 |
| Strings | 13 |
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
| Other (neg) | 4 |
| Date/Time | 3 |
| Networking | 3 |
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
| 16 | `agent_worker_release_test.obs` | Other | A bounded worker is not lost when the wait for it ends in an error (API.Agent), against a scripte... | ✅ |
| 17 | `ai_game_test.obs` | System.AI | Regression tests for System.AI adversarial search using tic-tac-toe: Minimax takes a win-in-1, bl... | ✅ |
| 18 | `ai_hub_client.obs` | System.AI | HubClient: Hugging Face Hub downloads into a verified local cache Two in-process servers stand in... | ✅ |
| 19 | `ai_optimize_test.obs` | System.AI | Regression tests for System.AI optimization: GeneticAlgorithm solves OneMax (all-ones chromosome)... | ✅ |
| 20 | `ai_rl_test.obs` | System.AI | Regression tests for System.AI reinforcement learning using a 6-state chain world (move right to... | ✅ |
| 21 | `ai_search_test.obs` | System.AI | Regression tests for System.AI graph search: Dijkstra finds the minimum-cost path even when a dir... | ✅ |
| 22 | `anthropic_http_test.obs` | Other | The Anthropic client over the wire, against a local HTTPS server that speaks the Messages API sha... | ✅ |
| 23 | `anthropic_parse_test.obs` | Other | The Anthropic client's response handling, request building and stream assembly, run against recor... | ✅ |
| 24 | `api_gemini_test.obs` | API (network) | api gemini test | ✅ |
| 25 | `api_key_insecure_tls_anthropic.obs` | API (network) | Anthropic client's API key is not handed out while TLS certificate checking is off. OBJECK_TLS_IN... | ✅ |
| 26 | `api_key_insecure_tls_gemini.obs` | API (network) | Gemini client's API key is not handed out while TLS certificate checking is off. OBJECK_TLS_INSEC... | ✅ |
| 27 | `api_key_insecure_tls_openai.obs` | API (network) | OpenAI client's API key is not handed out while TLS certificate checking is off. OBJECK_TLS_INSEC... | ✅ |
| 28 | `api_ollama_test.obs` | API (network) | api ollama test | ✅ |
| 29 | `api_openai_test.obs` | API (network) | api openai test | ✅ |
| 30 | `arm64_bitwise.obs` | ARM64 JIT | ARM64 Bitwise Operations Test Tests bitwise AND, OR, XOR operations | ✅ |
| 31 | `arm64_char_arrays.obs` | ARM64 JIT | ARM64 Character Array Test Tests STRH/LDRH instruction encoding (16-bit operations) | ✅ |
| 32 | `arm64_imm64_ops.obs` | ARM64 JIT | 64-bit immediate operands in the ARM64 JIT. Windows is LLP64, so 'long' is 32 bits there while Li... | ✅ |
| 33 | `arm64_large_immediates.obs` | ARM64 JIT | ARM64 Large Immediate Test Tests add_imm_reg/sub_imm_reg with values > 4095 | ✅ |
| 34 | `arm64_multiply_constants.obs` | ARM64 JIT | ARM64 Multiply-by-Constant Test Tests multiply-by-constant optimization (especially multiply-by-6) | ✅ |
| 35 | `array_index_nested.obs` | Other | An array index that is itself a subscript. Every index was analyzed twice by the compiler, so 'a[... | ✅ |
| 36 | `array_index_nested_call_chain.obs` | Other | An array index that is both a subscript and a call chain. Two fixes changed how the compiler read... | ✅ |
| 37 | `array_literal_dims.obs` | Other | Multi-dimensional array literals have the shape they are written in. A String literal's dimension... | ✅ |
| 38 | `array_literal_expressions.obs` | Other | Array literals with expressions (#1245; docs/ARRAY_LITERALS_DESIGN.md). A literal used to accept... | ✅ |
| 39 | `array_receiver_order.obs` | Other | A call written on a whole array, 'a->Fill(7)', is a static function on the element type's hidden... | ✅ |
| 40 | `assert_statement.obs` | Other | assert statement: 'assert E : "label";' (docs/design/ASSERT_STATEMENT.md). Every assertion here h... | ✅ |
| 41 | `bad_agent_backend_type.obs` | Negative | A value of an agent declaration is checked where it is written: a backend that is not a ChatBacke... | ✅ |
| 42 | `bad_agent_budget_limit.obs` | Negative | A limit a budget does not have is a compile error at the limit, and the message lists the limits... | ✅ |
| 43 | `bad_agent_no_backend.obs` | Negative | An agent declaration must say which model the agent talks to: one without a backend is a compile... | ✅ |
| 44 | `bad_agent_tool_class.obs` | Negative | A class named in the tools of an agent declaration must have a function marked as a tool. The cla... | ✅ |
| 45 | `bad_agent_unknown_setting.obs` | Negative | A setting an agent declaration does not have is a compile error at the setting, and the message l... | ✅ |
| 46 | `bad_alias_type_incomplete.obs` | Negative | A type that starts with a backslash is an alias, '\Group->Name'. With the '->Name' left out no ty... | ✅ |
| 47 | `bad_ambiguous_bundle_class_enum.obs` | Negative | Two classes of one bundle each declare 'enum Mood'; a third class of the bundle writes 'Mood'. It... | ✅ |
| 48 | `bad_ambiguous_use_alias.obs` | Negative | Two bundles in scope both declare 'alias Funcs'. An alias was named without its bundle, so the tw... | ✅ |
| 49 | `bad_ambiguous_use_alias_member.obs` | Negative | An alias's member type names a class two used bundles both declare. The name was copied to each p... | ✅ |
| 50 | `bad_ambiguous_use_class.obs` | Negative | Two bundles in scope both declare 'class Shape'. The unqualified name silently resolved to whiche... | ✅ |
| 51 | `bad_ambiguous_use_enum.obs` | Negative | Two bundles in scope both declare 'enum Tint'. The unqualified name silently resolved to whicheve... | ✅ |
| 52 | `bad_ambiguous_use_lib_program.obs` | Negative | A program bundle and a library bundle, both 'use'd, each declare 'JsonElement'. The program's cla... | ✅ |
| 53 | `bad_ambiguous_use_parent.obs` | Negative | A class inherits 'from Root' while two bundles in scope both declare 'class Root'. The parent sil... | ✅ |
| 54 | `bad_ambiguous_use_row_type.obs` | Negative | A jagged array's row type (#1246) on a class two used bundles both declare: 'rows : Shape[][]' to... | ✅ |
| 55 | `bad_arith_mul_zero_trap.obs` | Negative | Multiplying by a literal zero must still evaluate the other operand. The optimizer rewrites `x *... | ✅ |
| 56 | `bad_array_literal_2d_expressions.obs` | Negative | Array literals with expressions (#1245): expressions are one-dimensional for now; ragged rows are... | ✅ |
| 57 | `bad_array_literal_enum_mix.obs` | Negative | An enum value is an Int at run time. Mixed with a primitive in an array literal, it was taken for... | ✅ |
| 58 | `bad_array_literal_mixed_primitives.obs` | Negative | Array literals with expressions (#1245): an Int and a Float in one literal: there is no implicit... | ✅ |
| 59 | `bad_array_literal_nil_element.obs` | Negative | Array literals with expressions (#1245): Nil takes its type from the literal's other elements (co... | ✅ |
| 60 | `bad_array_narrowing.obs` | Negative | An array of a class serves as an array of its parent or an interface it implements (core_array_in... | ✅ |
| 61 | `bad_array_too_many_dimensions.obs` | Negative | rejects array too many dimensions | ✅ |
| 62 | `bad_assert_label_type.obs` | Negative | rejects assert label type | ✅ |
| 63 | `bad_assert_missing_expr.obs` | Negative | rejects assert missing expr | ✅ |
| 64 | `bad_assert_not_bool.obs` | Negative | rejects assert not bool | ✅ |
| 65 | `bad_assert_undefined.obs` | Negative | rejects assert undefined | ✅ |
| 66 | `bad_assign_type_detail.obs` | Negative | "Invalid cast with classes: Int and String" said neither that this was an assignment, nor to what... | ✅ |
| 67 | `bad_bang_operator.obs` | Negative | '!' is not an Objeck operator; the message says what to write instead ('<>' for not). The stateme... | ✅ |
| 68 | `bad_boxing_ambiguous_overload.obs` | Negative | Two methods take the call once an argument is boxed, and neither fits every argument at least as... | ✅ |
| 69 | `bad_boxing_bool_from_int_ref.obs` | Negative | A holder where a primitive is wanted is its value, converted as that value would be: an Int is no... | ✅ |
| 70 | `bad_boxing_bool_ref_argument.obs` | Negative | An argument is unboxed only into a primitive its value converts to. A BoolRef does not fit an Int... | ✅ |
| 71 | `bad_boxing_float_ref_index.obs` | Negative | An index is unboxed from an IntRef, a CharRef or a ByteRef. A FloatRef is not an index, as a Floa... | ✅ |
| 72 | `bad_boxing_int_for_bool_ref.obs` | Negative | A primitive is boxed only into a holder it converts to: an Int is not a Bool, so it does not beco... | ✅ |
| 73 | `bad_boxing_int_from_bool_ref.obs` | Negative | A BoolRef is never a number: returned from a method that returns an Int it is refused, at the 're... | ✅ |
| 74 | `bad_call_arguments_unclosed.obs` | Negative | A call missing its ')' produced four errors (Expected expression, twice; Expected ')'; Expected '... | ✅ |
| 75 | `bad_call_overload_detail.obs` | Negative | A call that matches no overload shows how it was called and the overloads there are. | ✅ |
| 76 | `bad_call_receiver_inherited.obs` | Negative | A method the class inherits is found on the way up: the report still names the class the call was... | ✅ |
| 77 | `bad_call_receiver_instance.obs` | Negative | The same for a method called on an object: the report names the class of the object, and lists th... | ✅ |
| 78 | `bad_call_receiver_no_method.obs` | Negative | The second line of the same report: the class that has no such method is the one the call was mad... | ✅ |
| 79 | `bad_call_receiver_own.obs` | Negative | A call with no class before it is still reported against the class it stands in. | ✅ |
| 80 | `bad_call_receiver_static.obs` | Negative | A call of a function another class does not have is reported against that class. The search for t... | ✅ |
| 81 | `bad_call_undefined.obs` | Negative | rejects call undefined | ✅ |
| 82 | `bad_char_literal_two_chars.obs` | Negative | A character literal with more than one character used to be "Invalid expression: unexpected token... | ✅ |
| 83 | `bad_char_literal_unknown_escape.obs` | Negative | An escape the language does not have in a character literal used to leave the token's type unset.... | ✅ |
| 84 | `bad_class_enum_other_bundle.obs` | Negative | A class's enum is in scope in its class, the class's children and the other classes of its bundle... | ✅ |
| 85 | `bad_class_scope_initializer.obs` | Negative | A declaration with a value written between two methods is refused, and its value is not analyzed:... | ✅ |
| 86 | `bad_collection_literal_box_bool.obs` | Negative | The numbers of a collection literal for a Vector of a holder are boxed by the rule for an argumen... | ✅ |
| 87 | `bad_collection_literal_method_ref.obs` | Negative | An element of a collection literal that names a function is a method reference, with a reference'... | ✅ |
| 88 | `bad_collection_literal_type.obs` | Negative | The literal is typed by the target, so elements of the wrong type are the same error as the long... | ✅ |
| 89 | `bad_cond_expr_dimension_mismatch.obs` | Negative | Guards #867's companion check. Once a conditional keeps its branches' dimension, an array branch... | ✅ |
| 90 | `bad_constructor_missing.obs` | Negative | 'Shapeless->New()' on a class with no constructor at all resolved to System.Base's New: it built... | ✅ |
| 91 | `bad_constructor_missing_library.obs` | Negative | A library class with no constructor, System.Runtime, resolved 'Runtime->New()' to System.Base's N... | ✅ |
| 92 | `bad_constructor_not_inherited.obs` | Negative | A constructor is not inherited. 'Kid->New()', on a class that declares no 'New' of its own, walke... | ✅ |
| 93 | `bad_ctor_promotion_syntax.obs` | Negative | A constructor with a promoted field ('New(@x : Int)') whose first statement does not parse is a s... | ✅ |
| 94 | `bad_ctor_promotion_type.obs` | Negative | A promoted parameter takes the field's value, so the types must agree (the assignment is checked... | ✅ |
| 95 | `bad_curried_call_not_function.obs` | Negative | `Foo(2)(5)` is a curried call (#1184): it calls the value Foo(2) returns. Foo returns an Int, so... | ✅ |
| 96 | `bad_decl_inside_body.obs` | Negative | A method missing its closing '}' ran into the next 'function', reported as "Expected expression,... | ✅ |
| 97 | `bad_div_zero_folded.obs` | Negative | A constant division by zero must still trap at runtime, at every optimization level. The constant... | ✅ |
| 98 | `bad_dot_without_name.obs` | Negative | A '.' with nothing after it used to stay on the end of the name, which then failed far from the c... | ✅ |
| 99 | `bad_duplicate_class.obs` | Negative | rejects duplicate class | ✅ |
| 100 | `bad_error_raise_unhandled.obs` | Negative | Runtime->Raise with no ?-> region to recover from (#1247): the program reports "Unhandled <class>... | ✅ |
| 101 | `bad_expression_too_long.obs` | Negative | A statement chaining thousands of operators crashed the compiler (the tree it builds is as deep a... | ✅ |
| 102 | `bad_field_accessor_local.obs` | Negative | A local is not a field (line 7). | ✅ |
| 103 | `bad_field_accessor_static.obs` | Negative | Accessors are for instance fields (line 6). | ✅ |
| 104 | `bad_field_typo_compound_assign.obs` | Negative | A misspelt field in a compound assignment is an error (#1184). Any unknown name used to become a... | ✅ |
| 105 | `bad_float_div_zero_dead_store.obs` | Negative | A Float division by zero must raise even when its result is never read (#1178). Dead-store elimin... | ✅ |
| 106 | `bad_float_div_zero_folded.obs` | Negative | A constant Float division by zero must raise at every optimization level (#1178). The VM raises "... | ✅ |
| 107 | `bad_float_literal_negative_overflow.obs` | Negative | A negated float literal just past the largest double is rejected too; it compiled silently as neg... | ✅ |
| 108 | `bad_float_literal_overflow.obs` | Negative | A float literal past the largest double must be rejected. It was parsed with wcstod, whose range... | ✅ |
| 109 | `bad_float_literal_underflow.obs` | Negative | A nonzero float literal below the smallest subnormal double rounds to zero and must be rejected r... | ✅ |
| 110 | `bad_float_literal_underflow_edge.obs` | Negative | 2.4e-324 is below half the smallest subnormal double (4.9e-324), so it rounds to zero and is reje... | ✅ |
| 111 | `bad_generic_arg_mismatch.obs` | Generics (neg) | Guards the generic type-argument compatibility check (and its readable diagnostic). Assigning a V... | ✅ |
| 112 | `bad_generic_array_type_argument.obs` | Generics (neg) | 'Vector->New()<String[]>' was accepted at the declaration, and then every AddBack(String[]) faile... | ✅ |
| 113 | `bad_generic_bounded_param.obs` | Generics (neg) | A bounded parameter (T : Compare) takes the type argument, not any value of the bound: Holder<Str... | ✅ |
| 114 | `bad_generic_compound_bound.obs` | Generics (neg) | A concrete type argument that satisfies only one of a compound bound (T : A & B) must be rejected... | ✅ |
| 115 | `bad_generic_fbound.obs` | Generics (neg) | A concrete argument that does not satisfy an F-bounded constraint (T : Compare<T>) must be reject... | ✅ |
| 116 | `bad_generic_infer_ambiguous.obs` | Generics (neg) | Two constructors take one argument, and a Vector<String> fits both: as the value (a Tagged<Vector... | ✅ |
| 117 | `bad_generic_infer_bound.obs` | Generics (neg) | An inferred type argument is checked against the parameter's bound exactly as a written one is: '... | ✅ |
| 118 | `bad_generic_infer_conflict.obs` | Generics (neg) | Two arguments give one type parameter two classes. No common parent is looked for: the call is re... | ✅ |
| 119 | `bad_generic_infer_mismatch.obs` | Generics (neg) | Written type arguments are never replaced by the target's: a Vector<IntRef> stored into a Vector<... | ✅ |
| 120 | `bad_generic_infer_nil.obs` | Generics (neg) | 'Nil' is a value of every class, so it says nothing about a type parameter. | ✅ |
| 121 | `bad_generic_infer_no_source.obs` | Generics (neg) | Nothing says what the Vector holds: the local has no declared type and the constructor takes no a... | ✅ |
| 122 | `bad_generic_infer_other_target.obs` | Generics (neg) | A declared type gives a constructor call its type arguments only when it is the class being const... | ✅ |
| 123 | `bad_generic_variance.obs` | Generics (neg) | Variance must stay sound: a covariant 'out T' does NOT allow the reverse direction. Producer<Anim... | ✅ |
| 124 | `bad_inherit_unknown.obs` | Negative | rejects inherit unknown | ✅ |
| 125 | `bad_inherited_library_instance_call_lambda.obs` | Negative | A lambda does not capture self (bad_lambda_implicit_self.obs), so inside one an unqualified call... | ✅ |
| 126 | `bad_inherited_library_instance_call_static.obs` | Negative | A function has no instance, so an unqualified call of an instance method the class inherits from... | ✅ |
| 127 | `bad_int64_min_unnegated.obs` | Negative | 9223372036854775808 is Int's minimum only when negated (int64_min_literal). On its own, or as the... | ✅ |
| 128 | `bad_int_literal_negative_range.obs` | Negative | A negated integer literal past Int's range names the problem, with its sign. The negation path ha... | ✅ |
| 129 | `bad_int_literal_overflow.obs` | Negative | An integer literal past the signed range must be rejected. It used to be parsed with a saturating... | ✅ |
| 130 | `bad_int_literal_range.obs` | Negative | An integer literal too large for Int names the problem (#1184); it was reported only as "unexpect... | ✅ |
| 131 | `bad_interp_conditional_half.obs` | Negative | A conditional with one branch inside an interpolation: the same crash, from a tree whose second b... | ✅ |
| 132 | `bad_interp_conditional_open.obs` | Negative | A question mark left at the end of an interpolated expression starts a conditional with no branch... | ✅ |
| 133 | `bad_interp_tilde.obs` | Negative | A stray tilde after a call inside an interpolation is read as the start of a function type that n... | ✅ |
| 134 | `bad_invisible_in_name.obs` | Negative | A zero-width space between two halves of a name (here U+200B after 'ab') looks like one word and... | ✅ |
| 135 | `bad_jagged_mixed_rows.obs` | Negative | Jagged arrays (#1246): an Int row and a String row in one literal. | ✅ |
| 136 | `bad_jagged_row_type.obs` | Negative | Jagged arrays (#1246): a Float[] row in an Int[][]. | ✅ |
| 137 | `bad_jagged_second_subscript.obs` | Negative | Jagged arrays (#1246): a second subscript on an Int[]. | ✅ |
| 138 | `bad_jagged_too_many_subscripts.obs` | Negative | Jagged arrays (#1246): a third subscript on an Int[][]. | ✅ |
| 139 | `bad_json_literal_array_close.obs` | Negative | An array in a JSON literal closes with ']'. | ✅ |
| 140 | `bad_json_literal_char_value.obs` | Negative | A Char is not a JSON value: it is neither a string nor a number until the program says which. | ✅ |
| 141 | `bad_json_literal_element.obs` | Negative | An element of an array in a JSON literal is one JsonElement has an Add for. | ✅ |
| 142 | `bad_json_literal_name.obs` | Negative | The name of a member is a String. | ✅ |
| 143 | `bad_json_literal_no_colon.obs` | Negative | A member is a name, a colon and a value. | ✅ |
| 144 | `bad_json_literal_no_comma.obs` | Negative | Members are separated by commas. | ✅ |
| 145 | `bad_json_literal_no_value.obs` | Negative | A member needs a value. | ✅ |
| 146 | `bad_json_literal_value.obs` | Negative | A value in a JSON literal is one JsonElement has an Insert for; the error names the value's type,... | ✅ |
| 147 | `bad_keyword_as_var.obs` | Negative | rejects keyword as var | ✅ |
| 148 | `bad_lambda_field_nested.obs` | Negative | A lambda inside a lambda does not capture the instance (documented limit): line 10 (the inner lam... | ✅ |
| 149 | `bad_lambda_field_static.obs` | Negative | A lambda in a function (no instance) cannot name a field: line 10. | ✅ |
| 150 | `bad_lambda_implicit_self.obs` | Negative | Guards the boundary of captured method calls (v2026.10.0 C3). A lambda body may call an instance... | ✅ |
| 151 | `bad_lambda_in_interpolation.obs` | Negative | A lambda inside a {$ } interpolation used to be reported as "Invalid escaped string literal" (the... | ✅ |
| 152 | `bad_lambda_multi_arg.obs` | Negative | A bare lambda is only inferred when it is a call's sole argument. As one of several arguments the... | ✅ |
| 153 | `bad_lambda_ternary.obs` | Negative | A bare lambda `\() => body` gets its type from a FuncRef<R> assignment target, return type or sin... | ✅ |
| 154 | `bad_leading_zero_nine.obs` | Negative | '09' has no octal reading. It was an unknown token, reported as an unexpected token with follow-o... | ✅ |
| 155 | `bad_leading_zero_octal.obs` | Negative | A decimal-looking literal with a leading zero was read as octal, so '010' compiled silently as 8.... | ✅ |
| 156 | `bad_local_redeclared_in_block.obs` | Negative | A block may hide a name of an enclosing block, but within one block a name is declared once. | ✅ |
| 157 | `bad_local_redeclares_parameter.obs` | Negative | A method's parameters and the locals of its body are declared in one block: a local there cannot... | ✅ |
| 158 | `bad_local_typo_compound_assign.obs` | Negative | Only `name := value` declares a local (#1184). The target of +=, -= and the like must already exi... | ✅ |
| 159 | `bad_loop_unassigned_function_value.obs` | Negative | A function variable declared without a value inside a loop is unassigned (a Nil dereference when... | ✅ |
| 160 | `bad_map_literal_after_nil_coalesce.obs` | Negative | A map literal after '??' is the default value, not the block of a nil-guard ('x := E ?? { ...; re... | ✅ |
| 161 | `bad_map_literal_argument.obs` | Negative | An argument is not a target a map literal takes its type from (the overload is chosen from the ar... | ✅ |
| 162 | `bad_map_literal_constructor_class.obs` | Negative | A constructor call in a map literal's entry takes its type arguments from the map's value type on... | ✅ |
| 163 | `bad_map_literal_duplicate_int_key.obs` | Negative | The same constant key twice, in a Hash keyed by number. | ✅ |
| 164 | `bad_map_literal_duplicate_key.obs` | Negative | The same constant key twice in a map that keeps one value per key would lose the first value with... | ✅ |
| 165 | `bad_map_literal_in_conditional.obs` | Negative | Inside another expression a map literal has no target of its own. | ✅ |
| 166 | `bad_map_literal_key_type.obs` | Negative | An entry's key is the map's key type. | ✅ |
| 167 | `bad_map_literal_no_colon.obs` | Negative | An entry is a key, a colon and a value. | ✅ |
| 168 | `bad_map_literal_no_comma.obs` | Negative | Entries are separated by commas. | ✅ |
| 169 | `bad_map_literal_no_type.obs` | Negative | A map literal is typed by its target; an inferred variable gives it none. | ✅ |
| 170 | `bad_map_literal_not_map.obs` | Negative | The target is a map class. | ✅ |
| 171 | `bad_map_literal_value_type.obs` | Negative | An entry's value is the map's value type, as for the Insert the entry stands for. | ✅ |
| 172 | `bad_match_all_nil.obs` | Negative | A value whose every arm is Nil has no type. | ✅ |
| 173 | `bad_match_arm_conversion.obs` | Negative | A Float arm after an Int one would be truncated without a word: it is an error, not a conversion. | ✅ |
| 174 | `bad_match_arm_types.obs` | Negative | The arms of a 'match' used as a value agree in type, as the two branches of '?' do. | ✅ |
| 175 | `bad_match_duplicate_pattern.obs` | Negative | A pattern names one arm. | ✅ |
| 176 | `bad_match_float_subject.obs` | Negative | The subject of a 'match' is what a 'select' takes: an Int, Char, Byte, enum or String. | ✅ |
| 177 | `bad_match_foreign_item.obs` | Negative | An item of another enum would be compared by its number. | ✅ |
| 178 | `bad_match_library_enum_items.obs` | Negative | An enum from a library is checked the same way. | ✅ |
| 179 | `bad_match_missing_arrow.obs` | Negative | A pattern is followed by '=>'. | ✅ |
| 180 | `bad_match_missing_items.obs` | Negative | A 'match' on an enum with no 'other' arm names every item; the error lists the ones it leaves out. | ✅ |
| 181 | `bad_match_no_arms.obs` | Negative | A 'match' has at least one arm. | ✅ |
| 182 | `bad_match_number_for_item.obs` | Negative | A number is not an item of the enum the 'match' is on. | ✅ |
| 183 | `bad_match_statement_arm.obs` | Negative | The first arm of a 'match' that starts a statement decides its form: after a block, every arm is... | ✅ |
| 184 | `bad_match_statement_missing_items.obs` | Negative | The statement form is checked as the value form is. | ✅ |
| 185 | `bad_match_value_needs_other.obs` | Negative | A 'match' used as a value yields one for every subject: on an Int that takes an 'other' arm. | ✅ |
| 186 | `bad_method_generic_bound.obs` | Negative | Method generics (#1242): an argument that does not meet the bound. | ✅ |
| 187 | `bad_method_generic_call.obs` | Negative | A method's own type parameter (#1242) is not a class. 'T->New()' in 'Id<T>' named whatever class... | ✅ |
| 188 | `bad_method_generic_conflict.obs` | Negative | Method generics (#1242): two arguments that bind T to different classes. | ✅ |
| 189 | `bad_method_generic_library_bound.obs` | Negative | Method-level generics in libraries (#1242, phase 2): CompareList's Map<R : Compare> is checked ag... | ✅ |
| 190 | `bad_method_generic_primitive.obs` | Negative | Method generics (#1242): a primitive argument: a type parameter takes an object, as class generic... | ✅ |
| 191 | `bad_method_ref_instance.obs` | Negative | Only functions can be referenced, not instance methods. | ✅ |
| 192 | `bad_method_ref_mismatch.obs` | Negative | The function's own signature is what is taken, so a target of another shape is still a type error... | ✅ |
| 193 | `bad_method_ref_overloaded.obs` | Negative | A method reference without a signature names more than one function: the signature cannot be read... | ✅ |
| 194 | `bad_method_ref_primitive.obs` | Negative | A FuncRef holds class types, so a function over Int cannot be referenced without its signature (l... | ✅ |
| 195 | `bad_missing_semicolon_eol.obs` | Negative | A field declaration missing its ';' was reported at the NEXT line's first token, with a cascade a... | ✅ |
| 196 | `bad_mod_zero.obs` | Negative | Modulus by zero must trap like division by zero does. The interpreter's ModInt had no zero check... | ✅ |
| 197 | `bad_named_argument_function_value.obs` | Negative | Named arguments (#1244): a function value: a function type has no parameter names. | ✅ |
| 198 | `bad_named_argument_library.obs` | Negative | Named arguments (#1244): a name a library method does not have. The error lists the parameter nam... | ✅ |
| 199 | `bad_named_argument_missing_required.obs` | Negative | Named arguments (#1244): skipping a parameter that has no default. | ✅ |
| 200 | `bad_named_argument_positional_after.obs` | Negative | Named arguments (#1244): a positional argument after a named one. | ✅ |
| 201 | `bad_named_argument_primitive.obs` | Negative | Named arguments (#1244): a call on a primitive value is a static call with the value as a hidden... | ✅ |
| 202 | `bad_named_argument_twice.obs` | Negative | Named arguments (#1244): one parameter given by position and again by name. | ✅ |
| 203 | `bad_named_argument_unknown.obs` | Negative | Named arguments (#1244): a parameter name the method does not have; the error lists the ones it has. | ✅ |
| 204 | `bad_nested_undefined_calls.obs` | Negative | A call to a method that does not exist, nested 30 deep, took exponential time: each call was trie... | ✅ |
| 205 | `bad_nesting_too_deep.obs` | Negative | 400 nested parentheses crashed the compiler with a stack overflow (exit code 0xC00000FD). Nesting... | ✅ |
| 206 | `bad_new_negative_array.obs` | Negative | A negative array size must stop the VM in every mode (#1181). The JIT wrapped the count to 0 and... | ✅ |
| 207 | `bad_new_negative_dim.obs` | Negative | A negative dimension beside a zero one must stop the VM too (#1181): the product is 0, so the siz... | ✅ |
| 208 | `bad_nil_array_argument_ambiguous.obs` | Negative | A literal Nil matches an array parameter as it matches an object one (core_nil_array_argument.obs... | ✅ |
| 209 | `bad_nil_guard_block_syntax.obs` | Negative | A syntax error in the last statement of a nil-guard's block is reported as a syntax error. The ch... | ✅ |
| 210 | `bad_nil_guard_expression.obs` | Negative | '?? return' belongs to an assignment statement, not to an argument (line 9). | ✅ |
| 211 | `bad_nil_guard_falls_through.obs` | Negative | A guard block that can fall through would leave the variable Nil (line 10). | ✅ |
| 212 | `bad_number_literal_forms.obs` | Negative | Each malformed number is its own error, said in its own words, at its own first character; one ba... | ✅ |
| 213 | `bad_numeric_literal_in_array.obs` | Negative | An out-of-range literal inside an array literal is the only error. Recovery used to skip to the n... | ✅ |
| 214 | `bad_numeric_literal_mid_expression.obs` | Negative | An out-of-range literal in the middle of an expression is the only error. Recovery used to skip t... | ✅ |
| 215 | `bad_param_missing_comma.obs` | Negative | A missing ',' between parameters skipped the parser to the next '}', the end of the whole body, a... | ✅ |
| 216 | `bad_primitive_receiver_arity.obs` | Negative | A call on a primitive passes the receiver as parameter 0 of a function on its $ class, so `255->T... | ✅ |
| 217 | `bad_prompt_brace.obs` | Negative | A closing brace that closes no hole of a prompt literal is a compile error where it stands; a lit... | ✅ |
| 218 | `bad_prompt_hole_empty.obs` | Negative | An empty hole in a prompt literal is a compile error at the hole; a literal brace is written doub... | ✅ |
| 219 | `bad_prompt_hole_object.obs` | Negative | Only a String, Int, Float, Bool, Char or Byte can be written into a prompt: a hole whose variable... | ✅ |
| 220 | `bad_prompt_hole_type.obs` | Negative | A hole declared with a type its variable does not have is a compile error at the hole: no convers... | ✅ |
| 221 | `bad_prompt_hole_undefined.obs` | Negative | A hole of a prompt literal that names no variable in scope is a compile error at the hole, here o... | ✅ |
| 222 | `bad_quote_in_interpolation.obs` | Negative | A quote inside an interpolation ends the string literal there: the scanner stops a string at its... | ✅ |
| 223 | `bad_readonly_record_assignment.obs` | Negative | rejects readonly record assignment | ✅ |
| 224 | `bad_readonly_record_op_assignment.obs` | Negative | rejects readonly record op assignment | ✅ |
| 225 | `bad_ref_argument_expression.obs` | Negative | Reference parameters (#1243): an array element passed as 'ref'; the copy back would evaluate its... | ✅ |
| 226 | `bad_ref_array_param.obs` | Negative | Reference parameters (#1243): a 'ref' parameter of an array type; an array is already shared. | ✅ |
| 227 | `bad_ref_default.obs` | Negative | Reference parameters (#1243): a 'ref' parameter has a default; the caller's cell is its value. | ✅ |
| 228 | `bad_ref_in_condition.obs` | Negative | Reference parameters (#1243): a call with a 'ref' argument in an 'if' condition; its copy back wo... | ✅ |
| 229 | `bad_ref_in_return.obs` | Negative | Reference parameters (#1243): a call with a 'ref' argument in a return; its copy back would never... | ✅ |
| 230 | `bad_runtime_bounds.obs` | Negative | VM must not crash (segfault) on array out-of-bounds access — exit 1 with message | ✅ |
| 231 | `bad_runtime_divzero.obs` | Negative | VM must not crash (segfault) on integer division by zero — exit 1 with message | ✅ |
| 232 | `bad_runtime_null.obs` | Negative | VM must not crash (segfault) when dereferencing a nil object — exit 1 with message | ✅ |
| 233 | `bad_runtime_stack.obs` | Negative | VM must not crash (segfault) on call stack overflow — exit 1 with message. NOTE: the recursion MU... | ✅ |
| 234 | `bad_select_label_call.obs` | Negative | A call on a select label was ignored: 'label "B"->ToLower()' was hashed as "B", so this compiled... | ✅ |
| 235 | `bad_select_label_concatenation.obs` | Negative | A select label is one constant, not an expression: a concatenation is refused by the parser. This... | ✅ |
| 236 | `bad_select_label_integer_on_string.obs` | Negative | An integer label in a select on a string compared the hash of the string with the integer: it com... | ✅ |
| 237 | `bad_select_label_interpolated.obs` | Negative | A select label is matched by the hash of its text, which the compiler takes. A label with a value... | ✅ |
| 238 | `bad_select_label_string_on_integer.obs` | Negative | A string label in a select on an integer compared the integer with the hash of the label's text:... | ✅ |
| 239 | `bad_select_value_block.obs` | Negative | A 'select' where a value is wanted takes a value after each label, not a block. | ✅ |
| 240 | `bad_select_value_missing_items.obs` | Negative | A 'select' used as a value on an enum, with no 'other', names every item. | ✅ |
| 241 | `bad_select_value_needs_other.obs` | Negative | A 'select' used as a value yields one for every subject: on an Int that takes 'other'. | ✅ |
| 242 | `bad_spawn_anonymous_class.obs` | Negative | 'spawn' is written out as an anonymous Thread class, and anonymous classes do not nest: a 'spawn'... | ✅ |
| 243 | `bad_spawn_implicit_self.obs` | Negative | The block of a 'spawn' follows a lambda's rules. It may call an instance method on an object it c... | ✅ |
| 244 | `bad_spawn_return_value.obs` | Negative | The block of a 'spawn' is a thread's body. 'return;' ends the thread; a value has nowhere to go,... | ✅ |
| 245 | `bad_spawn_thread_type.obs` | Negative | 'spawn { ... }' is the started thread, a System.Concurrency.Thread, and is typed as one: it canno... | ✅ |
| 246 | `bad_statements_same_line_assign.obs` | Negative | `x := 1 Foo(3);` compiled as an assignment and a call (#1184). Two statements on one line need a... | ✅ |
| 247 | `bad_statements_same_line_call.obs` | Negative | Semicolons between statements are optional, so `Foo(2) Foo(3);` compiled as two calls without a w... | ✅ |
| 248 | `bad_string_escape_unknown.obs` | Negative | The error names the escape and points at its backslash, with the source line, instead of "Invalid... | ✅ |
| 249 | `bad_string_interp_format.obs` | Negative | An unrecognized interpolation format specifier must be rejected at compile time. | ✅ |
| 250 | `bad_string_interp_format_range.obs` | Negative | An interpolation width or precision too large for an int is an invalid format specifier, reported... | ✅ |
| 251 | `bad_string_interp_literal.obs` | Negative | A malformed literal inside "{$...}" is reported as an invalid expression, not as an undefined var... | ✅ |
| 252 | `bad_string_plus_int_advice.obs` | Negative | Adding a String and an Int says how to convert, and the variable that failed to get a type from t... | ✅ |
| 253 | `bad_string_runs_on_note.obs` | Negative | A string missing its closing quote runs on to the next quote in the file, so the first error was... | ✅ |
| 254 | `bad_string_unterminated.obs` | Negative | A string with no closing quote swallowed the rest of the file, and the only error was "Expected '... | ✅ |
| 255 | `bad_syntax_unclosed.obs` | Negative | rejects syntax unclosed | ✅ |
| 256 | `bad_try_empty.obs` | Negative | try (#1247): a try with neither handles nothing. | ✅ |
| 257 | `bad_try_finally_break.obs` | Negative | try (#1247): a break out of a finally block would drop the error it runs for. | ✅ |
| 258 | `bad_try_finally_return.obs` | Negative | try (#1247): a return out of a finally block would drop the error it runs for. | ✅ |
| 259 | `bad_try_on_not_error.obs` | Negative | try (#1247): an 'on' clause names a class that is not an Error. | ✅ |
| 260 | `bad_try_on_unreachable.obs` | Negative | try (#1247): a clause for a subclass after one for its parent can never run. | ✅ |
| 261 | `bad_type_test_array_type.obs` | Negative | An array type is not a class to test for. | ✅ |
| 262 | `bad_type_test_more_condition.obs` | Negative | The test is the whole condition: the name it binds exists only in the block, so nothing after it... | ✅ |
| 263 | `bad_type_test_name_after_block.obs` | Negative | The bound name belongs to the block of its 'if'. | ✅ |
| 264 | `bad_type_test_name_in_else.obs` | Negative | In the else block the value is not of the type, so the name is not there either. | ✅ |
| 265 | `bad_type_test_name_taken.obs` | Negative | The bound name is declared in the block as `name : Type` is, so the block cannot declare the name... | ✅ |
| 266 | `bad_type_test_no_name.obs` | Negative | The binding type test names the value it binds; without a name it is `value->TypeOf(Type)`. | ✅ |
| 267 | `bad_type_test_not_class.obs` | Negative | Only a class (or an interface) can be tested for. | ✅ |
| 268 | `bad_type_test_outside_if.obs` | Negative | 'is' tests a type only as the condition of an 'if'. | ✅ |
| 269 | `bad_type_test_unknown_class.obs` | Negative | The type must exist. | ✅ |
| 270 | `bad_type_test_unrelated_class.obs` | Negative | A value can only be tested for a class it could be: the cast the test stands for is checked. | ✅ |
| 271 | `bad_typo_class_suggestion.obs` | Negative | A misspelled class name names the class it is closest to. | ✅ |
| 272 | `bad_typo_method_suggestion.obs` | Negative | An undefined method says what it was called as, which class it was called on, and the method of t... | ✅ |
| 273 | `bad_typo_variable_suggestion.obs` | Negative | A misspelled variable before '->' was "Undefined class: 'cnt'". It is an undefined variable, and... | ✅ |
| 274 | `bad_unassigned_function_value.obs` | Negative | Calling a function variable that was never assigned is a Nil dereference (#1183). It used to run... | ✅ |
| 275 | `bad_unclosed_brace_indentation.obs` | Negative | A '}' missing inside a method was reported only as "Expected '}'" at the end of the file. The err... | ✅ |
| 276 | `bad_undefined_type.obs` | Negative | rejects undefined type | ✅ |
| 277 | `bad_undefined_var.obs` | Negative | A read of a variable that was never declared must be rejected with the undefined-variable diagnos... | ✅ |
| 278 | `bad_undefined_variable_read.obs` | Negative | Reading a name that was never declared (#1184). Inside a method an unknown name silently became a... | ✅ |
| 279 | `bad_unsigned_literal_suffix.obs` | Negative | The 'u' suffix reads a literal across the unsigned range, so a value past 2^64-1 has no represent... | ✅ |
| 280 | `bad_unsigned_suffix_on_float.obs` | Negative | The unsigned suffix is meaningless on a floating-point literal and must not be accepted silently. | ✅ |
| 281 | `bad_use_unlinked_bundle.obs` | Negative | A 'use' of a bundle whose library was not linked names that library. The undefined-bundle error u... | ✅ |
| 282 | `bad_using_equals.obs` | Negative | rejects using equals | ✅ |
| 283 | `bad_using_no_close.obs` | Negative | rejects using no close | ✅ |
| 284 | `bad_using_scope.obs` | Negative | rejects using scope | ✅ |
| 285 | `bad_wrong_return.obs` | Negative | rejects wrong return | ✅ |
| 286 | `base_tostring.obs` | Other | System.Base->ToString (#1248, from #1186). Every object has a ToString now. A Stringify instance... | ✅ |
| 287 | `block_local_shadowing.obs` | Other | A block may declare a name that an enclosing block (or the parameter list) already has: its own v... | ✅ |
| 288 | `bool_array_call_index.obs` | Other | Subscripting a Bool[] returned from a call (#988). A Bool[] is allocated as NEW_BYTE_ARY and a lo... | ✅ |
| 289 | `bool_array_nil_comparison.obs` | Other | Comparing a call that returns Bool[] against Nil. `if(obj->Predict(x) = Nil)` used to fail to com... | ✅ |
| 290 | `boxing_before_after.obs` | Other | The boxing example of docs/FEATURES.md, kept compiling and running: a small tally written with ev... | ✅ |
| 291 | `byte_array_header_test.obs` | Other | A Byte[] reports one size to Objeck and another to native code. Size() reads header word [2]; nat... | ✅ |
| 292 | `byte_array_hex_string.obs` | Other | Byte[]->ToHexString is a hex digest It returned a bracketed list, "[0x76,0x4f,...]", while the ex... | ✅ |
| 293 | `byte_narrowing.obs` | Other | Regression: a scalar Byte wraps to 8 bits (#1176). A value stored into a Byte local, field or par... | ✅ |
| 294 | `bytes_to_string_length.obs` | Other | String->New(Byte[]) must decode exactly the bytes of the array. A Byte[] is not NUL-terminated. T... | ✅ |
| 295 | `calculated_operand_call_chain.obs` | Other | A CHAIN of method calls attached to a parenthesised calculation, used as an operand of another ca... | ✅ |
| 296 | `calculated_receiver_call.obs` | Other | A method call attached to a calculated expression, used inside another calculated expression. `(1... | ✅ |
| 297 | `call_indices_nested_link.obs` | Other | A subscript on the result of a LATER link of a call chain: 'cells->Get(r)->Get()[c]'. 'GetItems()... | ✅ |
| 298 | `cast_and_typeof_chains.obs` | Other | Regression: a call chain after As(T), and TypeOf(..) at the end of a chain. Found by the Rosetta... | ✅ |
| 299 | `char_array_exact_length.obs` | Other | Char[] and Byte[] natives use the array's own length: exact-length arrays and offset/num ranges T... | ✅ |
| 300 | `char_read_slice_terminator.obs` | Other | ReadBuffer(offset, num, Char[]) never writes its zero on the element after a full slice A read in... | ✅ |
| 301 | `char_read_whole_characters.obs` | Other | ReadBuffer(offset, num, Char[]) stores whole characters and returns how many A Char[] read of a b... | ✅ |
| 302 | `checkers_engine.obs` | Other | GENERATED by programs/examples/checkers/make_regression_test.py from the checkers_*.obs files nex... | ✅ |
| 303 | `cipher_aes_versioned.obs` | Other | Cipher.Encrypt/Decrypt->AES256 versioned format: AES-256-GCM with a PBKDF2 derived key, random sa... | ✅ |
| 304 | `client_socket_errors.obs` | Other | A client socket that fails says why, and keeps saying it TCPSocket->GetLastError() read the live... | ✅ |
| 305 | `closure_array_param_capture.obs` | Other | Guards #849: a lambda capturing an object array crashed obr (0xC0000005). Inside a lambda a captu... | ✅ |
| 306 | `closure_array_var_index.obs` | Other | A captured array indexed by a variable inside a lambda crashed obc (0xC0000005). Inside a lambda... | ✅ |
| 307 | `closure_bare_lambda.obs` | Other | Regression for bare lambdas `\(...) => body` with the return type inferred from context, auto-wra... | ✅ |
| 308 | `closure_block_body.obs` | Other | Regression for lambda block bodies: `\(...) ~ R : () => { ...; return e; }`. Previously a `{ }` l... | ✅ |
| 309 | `closure_body_call_result.obs` | Other | Regression: a call used as a statement inside a lambda body must discard its result. The context... | ✅ |
| 310 | `closure_capture_calls.obs` | Other | Calls through captured variables inside lambda bodies (v2026.10.0 C3 + C6). C3: an instance metho... | ✅ |
| 311 | `closure_capture_old_holder_g12.obs` | Other | G12: a closure capturing a young object, stored in an object that is already old, must keep the c... | ✅ |
| 312 | `closure_capture_resolution.obs` | Other | Guards #865 and #866: two sites resolved a name inside a lambda body without the lambda's capture... | ✅ |
| 313 | `closure_direct_call.obs` | Other | Regression for direct FuncRef callability `v()` and functional-call result chaining `v()->Method(... | ✅ |
| 314 | `closure_each_captured_array.obs` | Other | Guards the 'each' loop over an array captured by a lambda, which failed to compile with "Cannot r... | ✅ |
| 315 | `closure_enum_array_capture.obs` | Other | Guards enum-typed arrays against being freed while still live (G11). Storing an enum item into an... | ✅ |
| 316 | `closure_lambda_fn_param.obs` | Other | Guards a compiler defect present through v2026.9.4: a typed lambda with a FUNCTION-TYPED paramete... | ✅ |
| 317 | `closure_lambda_many_params.obs` | Other | Regression: lambdas whose parameter list is longer than the scanner's look-ahead buffer. ParseLam... | ✅ |
| 318 | `closure_lambda_params.obs` | Other | Guards two obc crashes (exit 0xC0000005, no diagnostic) present from v2026.6.3 through v2026.9.4:... | ✅ |
| 319 | `closure_multi_capture.obs` | Other | Regression for multiple capturing lambdas in one class. Each lambda's captured variables live in... | ✅ |
| 320 | `closure_nested_capture.obs` | Other | A lambda nested in a lambda could not use a variable its enclosing lambda captured: obc reported... | ✅ |
| 321 | `closure_param_lambda.obs` | Other | Regression: typed lambdas that take parameters, with and without captures. ParseLambda looks past... | ✅ |
| 322 | `code_formatter_brace.obs` | Other | System.Utility.CodeFormatter: what follows a closing brace, and two operators. The formatter join... | ✅ |
| 323 | `code_formatter_meaning.obs` | Other | System.Utility.CodeFormatter gives back the program it was given. The library's formatter changed... | ✅ |
| 324 | `collect_bimap.obs` | Collections | collect bimap | ✅ |
| 325 | `collect_bloom.obs` | Collections | collect bloom | ✅ |
| 326 | `collect_compare_vector.obs` | Collections | collect compare vector | ✅ |
| 327 | `collect_deque.obs` | Collections | collect deque | ✅ |
| 328 | `collect_each_patterns.obs` | Collections | collect each patterns | ✅ |
| 329 | `collect_fenwick.obs` | Collections | collect fenwick | ✅ |
| 330 | `collect_hash_heap_sort.obs` | Collections | Collection additions from #1186: Hash->Copy, Merge, Each and Filter; the hash-backed MultiHash; H... | ✅ |
| 331 | `collect_hash_open.obs` | Collections | Collection.Hash on open addressing: every public method, growth past one million entries (the old... | ✅ |
| 332 | `collect_hash_ops.obs` | Collections | collect hash ops | ✅ |
| 333 | `collect_map_ops.obs` | Collections | collect map ops | ✅ |
| 334 | `collect_multiset.obs` | Collections | collect multiset | ✅ |
| 335 | `collect_nested_ops.obs` | Collections | collect nested ops | ✅ |
| 336 | `collect_nil_guards.obs` | Collections | collect nil guards | ✅ |
| 337 | `collect_ordered_hash.obs` | Collections | collect ordered hash | ✅ |
| 338 | `collect_pair_compare.obs` | Collections | Collection.Pair->Compare takes the other pair, which is what a sorted collection passes. Pair->Co... | ✅ |
| 339 | `collect_pair_ops.obs` | Collections | collect pair ops | ✅ |
| 340 | `collect_primitive.obs` | Collections | Primitive collections in Collection: IntVector, FloatVector, IntMap, IntSet and BitSet. Covers em... | ✅ |
| 341 | `collect_priority_map.obs` | Collections | collect priority map | ✅ |
| 342 | `collect_queue_ops.obs` | Collections | collect queue ops | ✅ |
| 343 | `collect_set_algebra.obs` | Collections | Set algebra on Collection.Set, HashSet and SetHash, and the method names the three now share (#11... | ✅ |
| 344 | `collect_set_ops.obs` | Collections | collect set ops | ✅ |
| 345 | `collect_sort_stable.obs` | Collections | collect sort stable | ✅ |
| 346 | `collect_stack_deep.obs` | Collections | collect stack deep | ✅ |
| 347 | `collect_stack_ops.obs` | Collections | collect stack ops | ✅ |
| 348 | `collect_trie.obs` | Collections | collect trie | ✅ |
| 349 | `collect_union_find.obs` | Collections | collect union find | ✅ |
| 350 | `collect_vector_ops.obs` | Collections | collect vector ops | ✅ |
| 351 | `collection_literal_boxed.obs` | Other | The numbers of a collection literal for a Vector or CompareVector of a holder are boxed as that h... | ✅ |
| 352 | `collection_literal_method_refs.obs` | Other | In a collection literal for a Vector of function values, an element that names a static function... | ✅ |
| 353 | `collection_literals.obs` | Other | Collection literals typed by the target: `v : Vector<IntRef> := [a, b, c]` is `Vector->New([a, b,... | ✅ |
| 354 | `compile_alias_bundle_scope.obs` | Other | Regression: a bundle's alias is named with its bundle (bad_ambiguous_use_alias.obs). Checks two b... | ✅ |
| 355 | `compile_ambiguous_use_callee_scope.obs` | Other | Regression: a class's declarations resolve in the class's own bundle, wherever they are checked f... | ✅ |
| 356 | `compile_ambiguous_use_names.obs` | Other | Regression: an unqualified name that two 'use'd bundles both declare is a compile error (bad_ambi... | ✅ |
| 357 | `compile_as_call_compare.obs` | Other | Regression: a cast in a call chain used as a comparison operand. `x->As(Kid)->Only() <> "only"`,... | ✅ |
| 358 | `compile_bundle_class_enum_scope.obs` | Other | Regression: a class's enum is in scope in its class, its children and the other classes of its bu... | ✅ |
| 359 | `compile_bundle_enum_same_name.obs` | Other | Regression: bundle-level enums are named with their bundle, as classes are. A bundle-level enum w... | ✅ |
| 360 | `compile_bundle_qualified_enum.obs` | Other | Regression: bundle-qualified enum names. A bundle-level enum is named without its bundle (a class... | ✅ |
| 361 | `compile_class_scoped_enum_names.obs` | Other | Regression: class-scoped enums with the same name in two classes. The parser adds 'ScopedAa#' as... | ✅ |
| 362 | `compile_constructor_own.obs` | Other | Regression: a constructor is not inherited (bad_constructor_not_inherited.obs), so each class ins... | ✅ |
| 363 | `compile_inherited_library_instance_call.obs` | Other | Regression: an instance method calls an instance method its class inherits from a library class w... | ✅ |
| 364 | `compile_method_generic_shadow.obs` | Other | Regression: a method's own type parameter (#1242) shadows a class of the same name that the file'... | ✅ |
| 365 | `compiler_long_add_chain.obs` | Other | A forty-term addition chain. The compiler's AnalyzeCalculation recursed into each calculated oper... | ✅ |
| 366 | `compress_round_trip.obs` | I/O | Byte[] compression round trips: zlib, gzip and raw deflate on empty, one-byte, short and large in... | ✅ |
| 367 | `concurrent_critical_alloc.obs` | Other | Allocating inside a contended critical section (#1186). Eight threads share one ThreadMutex and a... | ✅ |
| 368 | `concurrent_sync_primitives.obs` | Other | System.Concurrency's blocking primitives (#1186): Condition (lang.obl, new COND_NEW/COND_WAIT/CON... | ✅ |
| 369 | `cond_expr_array_type.obs` | Other | Guards #867: a conditional expression whose branches are primitive arrays was typed as a scalar.... | ✅ |
| 370 | `cond_expr_receiver.obs` | Other | A conditional expression used as a method receiver -- '(t ? "abcdef" : "xy")-> SubString(1, 2)' -... | ✅ |
| 371 | `core_abstract_virtual.obs` | Core Language | core abstract virtual | ✅ |
| 372 | `core_arithmetic.obs` | Core Language | Core Arithmetic Operations Test Tests basic arithmetic operations, type conversions, and operator... | ✅ |
| 373 | `core_array_element_interface_return.obs` | Core Language | Regression: a call on an array element whose result needs a class cast. 'c : Compare := rows[i]->... | ✅ |
| 374 | `core_array_interface_widening.obs` | Core Language | Regression: an array of a class where an array of an interface it implements is wanted. A String[... | ✅ |
| 375 | `core_array_literal_mixed_strings.obs` | Core Language | Regression: an array literal mixing string literals with other expressions. 'v := "xyz"; texts :=... | ✅ |
| 376 | `core_array_literal_nil_element.obs` | Core Language | Regression: Nil as an element of an array literal. 'row := ["a", Nil, "c"];' was refused, "An arr... | ✅ |
| 377 | `core_array_operations.obs` | Core Language | core array operations | ✅ |
| 378 | `core_arrays_simple.obs` | Core Language | Core Array Operations Test (Simplified) Tests basic array creation, access, and modification | ✅ |
| 379 | `core_basic_type_call_statement.obs` | Core Language | A function of a basic type's class called as a statement: 'Int->Clear(a);'. 'Runtime->Copy(..);'... | ✅ |
| 380 | `core_bitwise_ops.obs` | Core Language | core bitwise ops | ✅ |
| 381 | `core_bool_ops.obs` | Core Language | core bool ops | ✅ |
| 382 | `core_bool_short_circuit.obs` | Core Language | Evaluation order and skipping of `&` and `\|`: left operand first, right only when the left did no... | ✅ |
| 383 | `core_boxing_nil_unbox.obs` | Core Language (neg) | A holder that is Nil has no value. Unboxing it is the 'Nil' dereference that calling 'Get()' on i... | ✅ |
| 384 | `core_boxing_rules.obs` | Core Language | Boxing and unboxing at the boundary between a primitive and its holder class (Int/IntRef, Float/F... | ✅ |
| 385 | `core_break_continue.obs` | Core Language | core break continue | ✅ |
| 386 | `core_calc_chain_conversion.obs` | Core Language | A conversion in a calculation is applied to the VALUE of an operand that is a call chain. 'n * 2.... | ✅ |
| 387 | `core_call_depth.obs` | Core Language | Ordinary deep recursion runs: thousands of frames, not 256. The call stack was a fixed array of 2... | ✅ |
| 388 | `core_call_depth_max.obs` | Core Language | The maximum call depth is enforced: a runaway recursion reaches it and stops with "call stack bou... | ✅ |
| 389 | `core_char_methods.obs` | Core Language | core char methods | ✅ |
| 390 | `core_classes.obs` | Core Language | Core Classes Test Tests class instantiation, inheritance, method calls, and select statements Bas... | ✅ |
| 391 | `core_collections_perf.obs` | Core Language | core collections perf | ✅ |
| 392 | `core_control_flow.obs` | Core Language | Core Control Flow Test Tests if/else, loops, and select statements | ✅ |
| 393 | `core_do_while.obs` | Core Language | core do while | ✅ |
| 394 | `core_do_while_post_op.obs` | Core Language | A post-operation in a `do`/`while` condition must run once per iteration. EmitDoWhile chose betwe... | ✅ |
| 395 | `core_each_loop.obs` | Core Language | core each loop | ✅ |
| 396 | `core_enum.obs` | Core Language | core enum | ✅ |
| 397 | `core_enum_interp.obs` | Core Language | Enum and consts values in strings (v2026.10.0 hardening item C4, decision D4). Through v2026.9.4... | ✅ |
| 398 | `core_function_refs.obs` | Core Language | core function refs | ✅ |
| 399 | `core_generic_bounded_return.obs` | Generics | A program's own bounded generic class (T : Compare) returns its type argument, not its bound. The... | ✅ |
| 400 | `core_generic_compound_bounds.obs` | Generics | Compound generic bounds (T : A & B): a concrete type argument must satisfy ALL bounds. Person imp... | ✅ |
| 401 | `core_generic_fbound.obs` | Generics | F-bounded type-parameter constraint (T : Compare<T>): the bound may be generic and self-referenti... | ✅ |
| 402 | `core_generic_inference.obs` | Generics | Type arguments a constructor call leaves out are inferred, in this order: 1. from the declared ty... | ✅ |
| 403 | `core_generic_structural.obs` | Generics | Exercises the structural generic type comparison: deeply nested generic type arguments must round... | ✅ |
| 404 | `core_generic_variance.obs` | Generics | Declaration-site variance: 'out T' (covariant) lets Producer<Dog> be used where Producer<Animal>... | ✅ |
| 405 | `core_http_server.obs` | Core Language | HTTP Client/Server Loopback Test Tests HTTP GET and POST using raw TCP server + HttpClient. Verif... | ✅ |
| 406 | `core_index_call_chain.obs` | Core Language | An array index that is a call chain is the value its LAST call returns. The index's type was read... | ✅ |
| 407 | `core_inheritance_chain.obs` | Core Language | core inheritance chain | ✅ |
| 408 | `core_int_methods.obs` | Core Language | core int methods | ✅ |
| 409 | `core_interfaces.obs` | Core Language | core interfaces | ✅ |
| 410 | `core_interpolate_nil.obs` | Core Language | An interpolated String or object that is Nil reads as "Nil" instead of stopping the program: "x={... | ✅ |
| 411 | `core_json_escape.obs` | Core Language | core json escape | ✅ |
| 412 | `core_method_overload.obs` | Core Language | core method overload | ✅ |
| 413 | `core_multi_dim_array.obs` | Core Language | core multi dim array | ✅ |
| 414 | `core_net_buffer.obs` | Core Language | Network Buffer Read Test Tests that TCP socket ReadBuffer correctly handles partial reads by veri... | ✅ |
| 415 | `core_nil_array_argument.obs` | Core Language | Regression: a literal Nil passed for an array parameter. With 'function : F(a : String[]) ~ Int',... | ✅ |
| 416 | `core_odbc.obs` | Core Language | Core ODBC Value Classes Test Date, Timestamp and ColumnInfo are pure Objeck. Only Connection, Par... | ✅ |
| 417 | `core_opencv.obs` | Core Language | Core OpenCV Bindings Test Tests helper classes, constants, and VideoWriter FourCC without requiri... | ✅ |
| 418 | `core_paren_method_chain.obs` | Core Language | Verifies a method call on a parenthesized method-call expression chains onto the parenthesized re... | ✅ |
| 419 | `core_records.obs` | Core Language | Core Records Test Exercises record-generated constructors, accessors, mutators, generics, readonl... | ✅ |
| 420 | `core_recursion.obs` | Core Language | Core Recursion Test Tests recursive function calls and tail recursion | ✅ |
| 421 | `core_select_ops.obs` | Core Language | core select ops | ✅ |
| 422 | `core_static_array_literals.obs` | Core Language | Regression test for the static-array literal pool (compiler bug, 2026-06): the bool literal-pool... | ✅ |
| 423 | `core_static_fields.obs` | Core Language | core static fields | ✅ |
| 424 | `core_string_escape_esc.obs` | Core Language | String Escape \e Test "\e" in a string literal is ESC (27) on every platform; a Windows-built obc... | ✅ |
| 425 | `core_string_format.obs` | Core Language | core string format | ✅ |
| 426 | `core_string_interp_expr.obs` | Core Language | Verifies operator expressions inside "{$...}" string interpolation. | ✅ |
| 427 | `core_string_interp_format.obs` | Core Language | Verifies inline format specifiers "{$expr:spec}" in string interpolation. | ✅ |
| 428 | `core_string_interp_literal.obs` | Core Language | Verifies a literal directly inside "{$...}": hex, octal, binary, decimal, float, negative, charac... | ✅ |
| 429 | `core_string_literal_after_branch.obs` | Core Language | A string literal in a condition ('s = "hi"') is consumed by the comparison, so the first literal... | ✅ |
| 430 | `core_string_methods.obs` | Core Language | core string methods | ✅ |
| 431 | `core_strings_simple.obs` | Core Language | Core String Operations Test (Simplified) Tests basic string operations without complex method cha... | ✅ |
| 432 | `core_ternary_call_chain_type.obs` | Core Language | Regression: a '?:' whose first branch is a call chain takes the chain's type. 'return c ? cols->G... | ✅ |
| 433 | `core_thread_gc_stress.obs` | Concurrency/GC | Multithreaded GC stop-the-world stress test. Guards the GC bugs fixed on branch fix/gc-stop-the-w... | ✅ |
| 434 | `core_type_checking.obs` | Core Language | core type checking | ✅ |
| 435 | `core_uninit_loop_local.obs` | Core Language | A local declared without an initializer starts as 0, 0.0, false, Nil or an empty reference every... | ✅ |
| 436 | `cpu_count_affinity.obs` | Other | runtime.cpu.count is the number of CPUs this process may use, not the machine's. A cluster job gi... | ✅ |
| 437 | `critical_exit_unlocks.obs` | Other | Regression: leaving a critical() block by return, break or continue never released its mutex (#11... | ✅ |
| 438 | `csv_median.obs` | Other | CsvRow->Median and CsvColumn->Median return the exact median. Both used to box every value into a... | ✅ |
| 439 | `csv_nil_data.obs` | Other | A CSV file that is not there reports, it does not crash the VM. FileReader->ReadFile returns Nil... | ✅ |
| 440 | `csv_quoting.obs` | Other | Data.CSV reads RFC 4180 quoting and reports malformed rows. CsvTable used to split the text into... | ✅ |
| 441 | `csv_unparsed_table.obs` | Other | An unparsed CsvTable reports, it does not crash the VM. CsvTable->Init only built @header_names w... | ✅ |
| 442 | `ctor_field_promotion.obs` | Other | Constructor field promotion: in a constructor, a parameter written `@name` where the class has al... | ✅ |
| 443 | `curried_calls.obs` | Other | Regression: curried calls (#1184). `F(a)(b)` calls the function value F(a) returns with (b); chai... | ✅ |
| 444 | `dap_databreak_test.obs` | Debugger | Fixture for dap_databreak_test.py. Stops once with everything initialized, then mutates two local... | ✅ |
| 445 | `dap_drilldown_test.obs` | Debugger | dap drilldown test | ✅ |
| 446 | `dap_exception_test.obs` | Debugger (neg) | Triggers an uncaught runtime error (Nil dereference) so the DAP test suite can verify exception b... | ✅ |
| 447 | `dap_frame_eval_test.obs` | Debugger | Fixture for frame-scoped DAP evaluation and hit-count breakpoints. The point of the nesting is th... | ✅ |
| 448 | `dap_setvar_test.obs` | Debugger | Fixture for DAP setVariable through a drill-down handle. A local named 'count' and an object whos... | ✅ |
| 449 | `date_arithmetic.obs` | Date/Time | date arithmetic | ✅ |
| 450 | `date_basic_ops.obs` | Date/Time | date basic ops | ✅ |
| 451 | `date_gmt_calendar.obs` | Date/Time | Regression: System.Time GMT dates were computed as local time (#1179). - Date->New(day, month, ye... | ✅ |
| 452 | `debugger_coll_test.obs` | Debugger | debugger coll test | ✅ |
| 453 | `debugger_eval_test.obs` | Debugger | Fixture for the debugger's expression evaluator and breakpoint bookkeeping. Separate from debugge... | ✅ |
| 454 | `debugger_test.obs` | Debugger | debugger test | ✅ |
| 455 | `debugger_thread_test.obs` | Debugger | Fixture for debugging a program with several live threads. Each worker carries its own id and a l... | ✅ |
| 456 | `diag_concurrent_analysis.obs` | Other | Concurrency guard for the diagnostics/LSP analysis path (#659). The LSP server spawns a worker th... | ✅ |
| 457 | `dir_list_no_dot_entries.obs` | Other | Directory->List returns the same entries on every platform: the files and directories in the dire... | ✅ |
| 458 | `dirty_list_overflow.obs` | Other | Write-heavy old-to-young pointer workload for the write barrier's overflow path. The write barrie... | ✅ |
| 459 | `discarded_call_result_pop.obs` | Other | Regression: a discarded constructor call statement must pop its result. 'Pad->New(i);' as a state... | ✅ |
| 460 | `dtls_bind_address.obs` | Other | A DTLS server can listen on one address, and keeps to it DTLSSocketServer->New(cert, key, passwd,... | ✅ |
| 461 | `dtls_socket_test.obs` | Other | DTLSSocket and DTLSSocketServer, end to end Until this test, nothing had ever run a DTLS call. Th... | ✅ |
| 462 | `dual_stack_servers.obs` | Other | Servers are reachable over IPv4 and IPv6 alike A wildcard server should take clients of either fa... | ✅ |
| 463 | `error_raise.obs` | Other | Raising an Error and reading the one a ?-> region recovered from (#1247, phase 1; docs/ERROR_HAND... | ✅ |
| 464 | `exit_with_compiled_thread_running.obs` | Other | number of lines differs from run to run; the exit status is the assertion Main may return while a... | ✅ |
| 465 | `exit_with_live_threads.obs` | Other | Ending the process while other threads are still running must exit cleanly. Guards #877. A runtim... | ✅ |
| 466 | `field_accessors.obs` | Other | Field accessor modifiers: `@x : get : T` writes the getter, `@x : get set : T` the getter and the... | ✅ |
| 467 | `file_fifo_gc_park.obs` | Other | A read of a FIFO through the file API parks, so a collection elsewhere does not wait for it. Driv... | ✅ |
| 468 | `file_io_edge_cases.obs` | Other | Regression: the smaller File IO defects of #1180. - FileReader->ReadFile returned Nil for an empt... | ✅ |
| 469 | `file_io_flush_eof_delete.obs` | Other | Regression: three file defects (#1180). 1. File->Flush() cleared the file handle before flushing... | ✅ |
| 470 | `filesystem_links.obs` | Other | System.IO.Filesystem.HardLink and SymbolicLink. HardLink->Create had no test and a "TODO: impleme... | ✅ |
| 471 | `fix524_array_cast_chain.obs` | Bug Fix | Fix #524: Cannot chain method calls on array-indexed elements after cast Tests that Get(index)->A... | ✅ |
| 472 | `fix534_substring_crash.obs` | Bug Fix | Fix #534: String->SubString crash on negative or zero length argument Tests that a negative lengt... | ✅ |
| 473 | `fix_array_bounds.obs` | Bug Fix | fix array bounds | ✅ |
| 474 | `fix_chained_calls.obs` | Bug Fix | fix chained calls | ✅ |
| 475 | `fix_deep_recursion.obs` | Bug Fix | fix deep recursion | ✅ |
| 476 | `fix_float_precision.obs` | Bug Fix | fix float precision | ✅ |
| 477 | `fix_int_boundary.obs` | Bug Fix | fix int boundary | ✅ |
| 478 | `fix_large_arrays.obs` | Bug Fix | fix large arrays | ✅ |
| 479 | `fix_nested_generics.obs` | Bug Fix | fix nested generics | ✅ |
| 480 | `fix_nil_chain_ops.obs` | Bug Fix | fix nil chain ops | ✅ |
| 481 | `fix_polymorphic_calls.obs` | Bug Fix | fix polymorphic calls | ✅ |
| 482 | `fix_scope_shadowing.obs` | Bug Fix | fix scope shadowing | ✅ |
| 483 | `fix_string_concat.obs` | Bug Fix | fix string concat | ✅ |
| 484 | `float_constants_abs_binary.obs` | Other | Regression: Float constants, Float->Abs, ToBinaryString and hoisted Float division (#1176, #1178)... | ✅ |
| 485 | `float_literal_range.obs` | Other | Float literals at the edges of the double range compile to exactly the double strtod gives. Out-o... | ✅ |
| 486 | `float_precision_long_lines.obs` | Other | Regression: Float->ToString(v, precision) above 2^63 (#1173) and FileReader->ReadLine on lines lo... | ✅ |
| 487 | `float_tostring_large.obs` | Other | Regression: converting a Float of about 1e57 or more to a String killed the VM. Float->ToString,... | ✅ |
| 488 | `func_closure_field.obs` | Functional | func closure field | ✅ |
| 489 | `func_filter_ops.obs` | Functional | func filter ops | ✅ |
| 490 | `func_higher_order.obs` | Functional | func higher order | ✅ |
| 491 | `func_reduce_ops.obs` | Functional | func reduce ops | ✅ |
| 492 | `func_sort_custom.obs` | Functional | func sort custom | ✅ |
| 493 | `function_types.obs` | Other | Regression: function types (#1183). - An array parameter: `r := Plain(Int[]) ~ Int; r(a)` was "Un... | ✅ |
| 494 | `gc_bool_array_declaration.obs` | Other | Bool[] references must be declared as byte arrays. Bool->New[n] allocates a byte array (NEW_BYTE_... | ✅ |
| 495 | `gc_closure_capture_nursery_end.obs` | Other | A closure's captured young object must survive a collection that runs while the closure is being... | ✅ |
| 496 | `gc_closure_ids_stress.obs` | Other | Many distinct closure ids going live on several threads at once, while minor collections run. Thi... | ✅ |
| 497 | `gc_cond_value_root.obs` | Other | A conditional's object value must survive a minor collection that runs before the value is consum... | ✅ |
| 498 | `gc_cond_value_threads.obs` | Other | A conditional's object value must survive another thread's collection that runs while this thread... | ✅ |
| 499 | `gc_conservative_bad_class.obs` | Other | Guards G5: a conservative root that points INTO a young object must not have its "class pointer"... | ✅ |
| 500 | `gc_deep_chain.obs` | Other | A very long chain of objects must not overflow the collector's native stack CheckObject -> CheckM... | ✅ |
| 501 | `gc_fixed_threshold_scales.obs` | Other | A heap threshold set with --gc-threshold is a trigger, not a promise to collect on every allocati... | ✅ |
| 502 | `gc_heap_sizing.obs` | Other | The threshold for a major collection follows the live heap. After each major collection the next... | ✅ |
| 503 | `gc_inline_conditional_temp.obs` | Other | An inlined method's and/or temporary must be declared as what it holds. A conditional ('c ? a : b... | ✅ |
| 504 | `gc_instance_id_stable.obs` | Other | An object's instance ID survives the collector moving it GetInstanceID() was the object's address... | ✅ |
| 505 | `gc_int_not_relocated.obs` | Other | A minor collection leaves an Int alone, whatever its value (#1452). The collector relocated every... | ✅ |
| 506 | `gc_minor_closure_capture.obs` | Other | Guards G12: a minor GC must trace the captures of a closure stored in an OLD object. A closure's... | ✅ |
| 507 | `gc_minor_str_array_literal.obs` | Other | Guards G13: the elements of a String[] literal must survive a minor GC. A string-array literal is... | ✅ |
| 508 | `gc_mt_small_nursery_stress.obs` | Other | Multithreaded GC stress with thread exits overlapping collections. Guards the crash the stress pr... | ✅ |
| 509 | `gc_nursery_knob.obs` | Other | Nursery knob (--nursery / OBJECK_NURSERY) and the GC statistics it is read with. Guards: 1. runti... | ✅ |
| 510 | `gc_roots_jit_bridge.obs` | Other | Compiled code calling back into the interpreter keeps the collector's stack typing honest (#1475,... | ✅ |
| 511 | `gc_scoped_local_slot_types.obs` | Other | Frame slot types must match what the frame's slots actually hold. The collector (and OBJECK_GC_VE... | ✅ |
| 512 | `gc_verify_reflection.obs` | Other | Reflection objects the VM builds while the nursery is full. The VM allocates the Class, Method an... | ✅ |
| 513 | `gc_zero_field_nursery_end.obs` | Other | A zero-field object allocated just before a collection must survive it. An object's address is th... | ✅ |
| 514 | `gemini_error_message.obs` | Other | gemini error message | ✅ |
| 515 | `gemini_parse_test.obs` | Other | API.Google.Gemini response parsing, tested offline from recorded bodies that follow the public AP... | ✅ |
| 516 | `gemini_query_results.obs` | Other | gemini query results | ✅ |
| 517 | `generic_inference_before_after.obs` | Other | The type-argument inference example of docs/FEATURES.md, kept compiling and running: one small cl... | ✅ |
| 518 | `generics_chained_calls.obs` | Other | Regression: generic type arguments through chained calls, generic returns and self-referential ge... | ✅ |
| 519 | `gl_context_test.obs` | Other | Regression test for OpenGL 3.3 core support: proves a real context can be created AND that someth... | ✅ |
| 520 | `gl_quaternion_test.obs` | Other | Game.OpenGL Quaternion -- arithmetic only, so it needs no window. Every other GL test in this sui... | ✅ |
| 521 | `hpc_scale_kernel.obs` | Other | The HPC scaling kernel (tools/hpc/alderaan/bench/workloads/scale_kernel.obs): the checksum it pri... | ✅ |
| 522 | `http_body_limit.obs` | Other | http body limit | ✅ |
| 523 | `http_client_reuse.obs` | Other | HttpClient reuses a kept-alive connection, and retries only what reuse can lose (#1443) A request... | ✅ |
| 524 | `http_deadlines.obs` | Other | HttpClient takes a connect timeout, a read timeout and a deadline for a whole request HttpClient... | ✅ |
| 525 | `http_error_body.obs` | Other | HTTP error-response body test A client that reports a status code but no body for a server error... | ✅ |
| 526 | `http_header_flatten_test.obs` | Other | Request-header flattening (Web.HTTP.HeaderCheck->Flatten). HTTP/2 and HTTP/3 hand the request to... | ✅ |
| 527 | `http_header_validation_test.obs` | Other | Request-header validation (Web.HTTP.HeaderCheck). HttpClient->AddHeader was injectable: HTTP/1.1... | ✅ |
| 528 | `http_persistence_test.obs` | Other | http persistence test | ✅ |
| 529 | `http_query_params.obs` | Other | http query params | ✅ |
| 530 | `http_range_requests.obs` | Other | http range requests | ✅ |
| 531 | `http_redirect_credentials.obs` | Other | Credentials do not follow a redirect to another origin (#1453) HttpClient and HttpsClient follow... | ✅ |
| 532 | `http_request_hardening.obs` | Other | http request hardening | ✅ |
| 533 | `http_status_codes.obs` | Other | http status codes | ✅ |
| 534 | `http_stream_to_file.obs` | Other | Large responses: GetToFile streams to disk, and memory reads have no 256 MB cap A model file is h... | ✅ |
| 535 | `http_thread_reap.obs` | Other | http thread reap | ✅ |
| 536 | `https_deadlines.obs` | Other | HttpsClient takes a connect timeout, a read timeout and a deadline for a whole request The TLS tw... | ✅ |
| 537 | `https_persistence_test.obs` | Other | The exit 139 this test reported on linux-arm64 in nightly run 35027973357 is recorded in docs/PLA... | ✅ |
| 538 | `ieee_float_semantics.obs` | Other | IEEE 754 semantics for NaN and infinity, across the interpreter AND the JIT. Windows had two defe... | ✅ |
| 539 | `indexed_call_result.obs` | Other | Subscripting the result of a method call: 'GetItems()[0]->Name()'. This was never implemented, an... | ✅ |
| 540 | `inference_client_test.obs` | Other | API.Inference: tensor bookkeeping, error reporting, and the two wire formats. EXTRA_LIBS: models,... | ✅ |
| 541 | `inline_funcref_param.obs` | Other | A method that takes a func-ref parameter and is small enough for the compiler to inline: the inli... | ✅ |
| 542 | `int64_min_literal.obs` | Other | Int's minimum is writable as a literal. 9223372036854775808 is out of range on its own, so '-9223... | ✅ |
| 543 | `int_semantics.obs` | Other | Int arithmetic has one meaning in the interpreter, both JITs and at every -opt level (docs/FEATUR... | ✅ |
| 544 | `integer_literal_prefixes.obs` | Other | Integer literal prefixes. Octal is written with a '0o' prefix, as hex takes '0x' and binary '0b'.... | ✅ |
| 545 | `interact_accessors_promotion.obs` | Other | Field accessors and constructor field promotion on the same fields, with what they meet: - 'New(@... | ✅ |
| 546 | `interact_agent_prompt_json.obs` | Other | An agent declaration, a prompt literal and a typed result ('class Name : json') in one program, a... | ✅ |
| 547 | `interact_json_literal_boxing.obs` | Other | A JSON literal is the Insert and Add calls it stands for, so a holder (IntRef, FloatRef, BoolRef)... | ✅ |
| 548 | `interact_literals_inference_boxing.obs` | Other | Collection literals, map literals, type-argument inference and boxing in the same statements, eac... | ✅ |
| 549 | `interact_match_is_nil_guard.obs` | Other | 'match' and 'select' as values, the binding type test and the nil-guard, where they meet: - 'x :=... | ✅ |
| 550 | `interact_method_refs_using_spawn.obs` | Other | Method references, 'using', the nil-guard, 'spawn' and 'match' in one flow: - 'Class->Function' a... | ✅ |
| 551 | `interact_spawn_lambda_self.obs` | Other | 'spawn' and a lambda that names its instance, together (the block of a 'spawn' is compiled to a l... | ✅ |
| 552 | `interact_type_test_shadow.obs` | Other | The binding type test with a name that an enclosing block already has: the bound name is declared... | ✅ |
| 553 | `interact_using_assert_try.obs` | Other | 'using', 'assert' and try/finally in the same methods. A log records the order of the closes, the... | ✅ |
| 554 | `interp_chained_call.obs` | Other | A chained method call inside string interpolation. "{$s->ToUpper()->Size()}" segfaulted the VM: n... | ✅ |
| 555 | `interp_float_fastpath.obs` | Other | Exercises the interpreter's inlined float fast-path (ADD/SUB/MUL_FLOAT and the six float comparis... | ✅ |
| 556 | `io_create_new_process.obs` | I/O | File->CreateNew creates a file only when none of that name exists, as one step (O_EXCL on POSIX,... | ✅ |
| 557 | `io_file_basic.obs` | I/O | io file basic | ✅ |
| 558 | `io_rename_replaces.obs` | I/O | File->Rename replaces an existing target on every platform, as POSIX rename() does. On Windows th... | ✅ |
| 559 | `io_unicode_paths.obs` | I/O | File and directory names with non-ASCII characters work on every platform. Objeck strings are UTF... | ✅ |
| 560 | `io_write_error_devfull.obs` | I/O | io write error devfull | ✅ |
| 561 | `io_write_errors.obs` | I/O | io write errors | ✅ |
| 562 | `jagged_arrays.obs` | Other | Jagged arrays (#1246; docs/JAGGED_ARRAYS_DESIGN.md). 'Int[][]' is an array of Int[] rows of any l... | ✅ |
| 563 | `jit_alloc_result_reg.obs` | AMD64/JIT | NEW_OBJ_INST in compiled code, in every shape the AMD64 inline allocator takes since v2026.11.4 (... | ✅ |
| 564 | `jit_array_index_registers.obs` | AMD64/JIT | Multi-dimensional array accesses in JIT-compiled loops: the register that holds a later index is... | ✅ |
| 565 | `jit_array_native.obs` | AMD64/JIT | jit array native | ✅ |
| 566 | `jit_autojit_race.obs` | AMD64/JIT | Auto-JIT concurrency guard. Many threads call the same hot method, crossing the auto-JIT threshol... | ✅ |
| 567 | `jit_bitwise_mem_operand.obs` | AMD64/JIT | Regression for the AMD64 JIT and/or/xor memory-operand encoding (fuzzer F4, seeds 140/48/18/236).... | ✅ |
| 568 | `jit_bounds_message.obs` | AMD64/JIT | An out-of-bounds array index reports the same text from the interpreter and from compiled code: "... | ✅ |
| 569 | `jit_branch_shapes.obs` | AMD64/JIT | Every branch shape the compiler emits for `&`, `\|` and `<>` in conditions, in loops that are JIT-... | ✅ |
| 570 | `jit_bridge_exception.obs` | AMD64/JIT (neg) | A C++ exception thrown inside a call the JIT's bridge made ends the program with the VM's interna... | ✅ |
| 571 | `jit_call_overhead.obs` | AMD64/JIT | F7 guard: a call from compiled code into compiled code must stay cheap. The calling convention (d... | ✅ |
| 572 | `jit_closure_call_in_creating_frame.obs` | AMD64/JIT | JIT: calling a capturing lambda while its creating frame is still on the stack. JitAmd64::Process... | ✅ |
| 573 | `jit_closure_gc_fixup.obs` | AMD64/JIT | Regression for the generational-GC fixup of closure captures (bug B1). The GC mark phase descends... | ✅ |
| 574 | `jit_closure_local_funcvar.obs` | AMD64/JIT | Guards a JIT crash present through v2026.9.4: a capturing lambda stored in a local function varia... | ✅ |
| 575 | `jit_concurrent_compile.obs` | AMD64/JIT | Concurrency guard for the JIT code-page allocator (PageManager::GetPage). Several threads JIT-com... | ✅ |
| 576 | `jit_conditional_native.obs` | AMD64/JIT | jit conditional native | ✅ |
| 577 | `jit_conditional_value_types.obs` | AMD64/JIT | A conditional expression ('c ? a : b') whose value is a Float or a function reference, interprete... | ✅ |
| 578 | `jit_const_char_store.obs` | AMD64/JIT | ARM64 JIT: a constant character stored into a Char[] element was written with a full 8-byte store... | ✅ |
| 579 | `jit_const_division.obs` | AMD64/JIT | n / d and n % d for a constant d give what the divide instruction gives. A compiled method divide... | ✅ |
| 580 | `jit_dispatch_native.obs` | AMD64/JIT | jit dispatch native | ✅ |
| 581 | `jit_entry_compiled.obs` | AMD64/JIT | jit entry compiled | ✅ |
| 582 | `jit_entry_shapes.obs` | AMD64/JIT | The entry and exit of a compiled method, in the shapes the ARM64 callee work touches (the AMD64 b... | ✅ |
| 583 | `jit_first_call_race.obs` | AMD64/JIT | Auto-JIT first-call race. Several threads make their FIRST call of the same method at once, and t... | ✅ |
| 584 | `jit_float_branch_nan.obs` | AMD64/JIT | Regression: under the x64 JIT, `if(nan = x)` was taken and `if(nan <> x)` was not. The fused comp... | ✅ |
| 585 | `jit_float_compare_store.obs` | AMD64/JIT | A float comparison whose result is STORED must not clobber a live register. `cmov_reg`'s first ac... | ✅ |
| 586 | `jit_float_divide_nan.obs` | AMD64/JIT | Float division by NaN, and division guarded by a Float compare against 0.0, interpreted and JIT-c... | ✅ |
| 587 | `jit_float_equality.obs` | AMD64/JIT | Regression test for float equality compares on array elements (2026-06). The front-end chose EQL_... | ✅ |
| 588 | `jit_float_intensive.obs` | AMD64/JIT | jit float intensive | ✅ |
| 589 | `jit_float_limits.obs` | AMD64/JIT | Float->NaN(), Float->Inf() and Float->NegInf() in JIT-compiled methods. The three are the opcodes... | ✅ |
| 590 | `jit_float_mem_ops.obs` | AMD64/JIT | Float arithmetic and comparison against MEMORY operands, under the JIT. IMPORTANT: must run with... | ✅ |
| 591 | `jit_float_pin_calls.obs` | AMD64/JIT | Pinned Float loop locals survive calls, allocation and a safepoint park. The JIT keeps a loop's h... | ✅ |
| 592 | `jit_float_round_trig.obs` | AMD64/JIT | Exercises two JIT float-codegen bugs that only surface once a method using them is auto-JIT'd (de... | ✅ |
| 593 | `jit_frame_trap_test.obs` | AMD64/JIT | Regression test for the JIT frame-dependent trap crash (2026-06). Traps such as SERL_INT/SERL_FLO... | ✅ |
| 594 | `jit_frame_unreferenced_local.obs` | AMD64/JIT | A compiled method declares a local that no instruction references, ahead of an object-array local... | ✅ |
| 595 | `jit_func_ref_hot.obs` | AMD64/JIT | jit func ref hot | ✅ |
| 596 | `jit_funcref_store_basic_lambda.obs` | AMD64/JIT | JIT: a func-ref store whose two words are different operand kinds. A func-ref variable is two wor... | ✅ |
| 597 | `jit_funcref_store_mixed_words.obs` | AMD64/JIT | JIT: storing a func-ref whose two words have different working-stack shapes. A capturing lambda b... | ✅ |
| 598 | `jit_gc_safepoint.obs` | AMD64/JIT | jit gc safepoint | ✅ |
| 599 | `jit_gc_stress.obs` | AMD64/JIT | JIT + GC interaction stress (2026-06). One CI run on linux-x64 failed with a JIT-to-JIT runtime e... | ✅ |
| 600 | `jit_huge_method_guard_reach.obs` | AMD64/JIT | A method too large for a conditional branch to cross is still run correctly. Big() below is one m... | ✅ |
| 601 | `jit_int_min_immediate.obs` | AMD64/JIT | Adding or subtracting an Int literal in compiled code, at the edges of what an instruction can ho... | ✅ |
| 602 | `jit_loop_native.obs` | AMD64/JIT | jit loop native | ✅ |
| 603 | `jit_native_call_depth.obs` | AMD64/JIT (neg) | Recursion deeper than the call stack allows, through compiled-to-compiled native calls (the calli... | ✅ |
| 604 | `jit_native_call_error.obs` | AMD64/JIT (neg) | A compiled callee, called straight from compiled code (the calling convention's phase 3), derefer... | ✅ |
| 605 | `jit_native_cls_fields.obs` | AMD64/JIT | JIT Native Class Fields Test Tests object reference storage in class instance fields with GC pres... | ✅ |
| 606 | `jit_native_float_array.obs` | AMD64/JIT | JIT Native Float Array Test Tests native function with float array creation and math operations R... | ✅ |
| 607 | `jit_native_func_ref.obs` | AMD64/JIT | JIT Native Function Reference Test Tests native functions with function reference storage in clas... | ✅ |
| 608 | `jit_native_inline.obs` | AMD64/JIT | jit native inline | ✅ |
| 609 | `jit_native_math.obs` | AMD64/JIT | JIT Native Math Builtins Test Tests native math functions: Factorial, Sinh/Cosh/Tanh/Log2/Cbrt, P... | ✅ |
| 610 | `jit_nil_inlined_field.obs` | AMD64/JIT (neg) | An inlined getter called on a Nil object must raise the same Nil-dereference error compiled as in... | ✅ |
| 611 | `jit_nil_inlined_field_float.obs` | AMD64/JIT (neg) | Float sibling of jit_nil_inlined_field.obs: an inlined Float getter on a Nil object. At -opt s3 c... | ✅ |
| 612 | `jit_nil_inlined_field_funcref.obs` | AMD64/JIT (neg) | Func-ref sibling of jit_nil_inlined_field.obs: an inlined getter returning a function-reference f... | ✅ |
| 613 | `jit_nil_inlined_field_object.obs` | AMD64/JIT (neg) | Object sibling of jit_nil_inlined_field.obs: an inlined getter returning an object field, on a Ni... | ✅ |
| 614 | `jit_off_interprets_native.obs` | AMD64/JIT | --jit=off runs the interpreter only, methods declared `native` included. A `native` method used t... | ✅ |
| 615 | `jit_pin_callee_saved.obs` | AMD64/JIT | Pinned integer loop locals survive calls, early exits, recursion and a safepoint park. The JIT ke... | ✅ |
| 616 | `jit_recovery_through_bridge.obs` | AMD64/JIT | Regression: `?->` recovery from a fault several frames below a compiled method (#1171, JIT half).... | ✅ |
| 617 | `jit_safepoint_pending_values.obs` | AMD64/JIT | A loop's GC safepoint keeps the values an expression still has pending. The JIT polls for a stop-... | ✅ |
| 618 | `jit_stack_value_across_branch.obs` | AMD64/JIT | JIT: a value left on the working stack across a branch (#1256). `f(a <> Nil & a->Kind() = 6, b <>... | ✅ |
| 619 | `jit_string_ops.obs` | AMD64/JIT | jit string ops | ✅ |
| 620 | `jit_tailcall_loop_recovery.obs` | AMD64/JIT | Regression: `?->` recovery from an error inside a method whose tail call the optimizer turned int... | ✅ |
| 621 | `jit_tco_bare_local.obs` | AMD64/JIT | Regression for the TCO deferred-local-load miscompile (both arches). A self-recursive tail call t... | ✅ |
| 622 | `jit_trap_register_pressure.obs` | AMD64/JIT | Calls whose arguments already fill most of the register pool still compile. A call into the VM (a... | ✅ |
| 623 | `jit_virtual_equals.obs` | AMD64/JIT | Issue #722: on ARM64 the JIT miscompiled String->Equals inside a virtual request-handler callback... | ✅ |
| 624 | `json_build_ops.obs` | JSON | json build ops | ✅ |
| 625 | `json_class_oracle_test.obs` | JSON | The oracle for 'class Name : json' (docs/design/AGENT_SYNTAX.md): the schema the compiler derives... | ✅ |
| 626 | `json_class_test.obs` | JSON | 'class Name : json' (docs/design/AGENT_SYNTAX.md): the compiler writes JsonSchema, JsonFill, From... | ✅ |
| 627 | `json_codec_escapes.obs` | JSON | JsonElement->Decode and Encode agree on every JSON escape. \b and \f were mapped to 0x10 and 0x14... | ✅ |
| 628 | `json_escape_test.obs` | JSON | JsonElement must escape on serialization. Format's STRING branch appended the raw value between t... | ✅ |
| 629 | `json_hand_built_escapes.obs` | JSON | JSON that a library builds by hand is valid JSON whatever the text holds. CsvRow->ToJson pasted e... | ✅ |
| 630 | `json_literal.obs` | JSON | The JSON literal: `json{"name": value, "list": [a, b], "inner": {"x": 1}}` is a `Data.JSON.JsonEl... | ✅ |
| 631 | `json_nil_string_null.obs` | JSON | A Nil String given to JsonElement is JSON's null. `obj->Insert("name", text)` with `text` Nil mad... | ✅ |
| 632 | `json_number_grammar.obs` | JSON | Data.JSON and Data.JSON.Stream number grammar (RFC 8259): -? (0 \| [1-9][0-9]*) (. [0-9]+)? ([eE]... | ✅ |
| 633 | `json_parse_ops.obs` | JSON | json parse ops | ✅ |
| 634 | `json_rpc_client_id.obs` | JSON | Data.JSON.RPC.Client: every request carries its own id, and the reply must echo it. - The id was... | ✅ |
| 635 | `json_rpc_escapes.obs` | JSON | Data.JSON.RPC over a loopback socket: strings are escaped once on the way out and decoded once on... | ✅ |
| 636 | `json_rpc_nil_handling.obs` | JSON | Data.JSON.RPC must not dereference Nil on a request or a reply that leaves a member out. - A requ... | ✅ |
| 637 | `json_serialize_paths.obs` | JSON | JSON serializing and scanning give the same text whichever path produces it. The serializer appen... | ✅ |
| 638 | `json_string_roundtrip.obs` | JSON | Data.JSON strings: the parser decodes, the serializer escapes, and a round trip is the identity (... | ✅ |
| 639 | `lambda_andor_entry_space.obs` | Other | Interpreter frames were one to two words too small for a method at the compiler's local limit. Th... | ✅ |
| 640 | `lambda_capture_after_func.obs` | Other | Regression: a lambda capture that follows a captured function value. Found by the Rosetta batch-3... | ✅ |
| 641 | `lambda_self_fields.obs` | Other | Lambdas in an instance method read and write the instance's fields (@x) and name the instance (@s... | ✅ |
| 642 | `lame_encode_test.obs` | Other | Audio.Lame->PcmToMp3 encodes PCM to MP3. EXTRA_LIBS: lame This library had no runtime test at all... | ✅ |
| 643 | `lang_array_bytes.obs` | Other | Raw conversion between numeric arrays and bytes, one memcpy each: Int->ToBytes / Byte->ToInts and... | ✅ |
| 644 | `lang_array_copy.obs` | Other | Bulk primitive array moves: Int/Float/Byte/Char->Copy(dest, dest_offset, src, src_offset, count)... | ✅ |
| 645 | `lang_bytebuffer_written_bytes.obs` | Other | System.ByteBuffer covers the bytes written, not its capacity. ToString hex-dumped the whole backi... | ✅ |
| 646 | `lang_compress_deflate.obs` | Other | Byte->CompressDeflate/UncompressDeflate are raw deflate (RFC 1951), and the web server's response... | ✅ |
| 647 | `lang_compress_level.obs` | Other | Byte->CompressZlib(input, level), and Serializer->Serialize compressing at level 1. Serialize use... | ✅ |
| 648 | `lang_float_exact.obs` | Other | Float->ToShortString: the shortest text that reads back as the same Float. Float->ToString prints... | ✅ |
| 649 | `lang_inflate_limit.obs` | Other | Byte->UncompressDeflate(input, limit) and Byte->UncompressGzip(input, limit) inflate at most 'lim... | ✅ |
| 650 | `lang_tofloat_range.obs` | Other | String->ToFloat on out-of-range text, and String->ToInt's prefixes under the JIT. ToFloat went th... | ✅ |
| 651 | `lang_trap_method_returns.obs` | Other | Library methods with a return type return a value. lang.obs is compiled by the bootstrap compiler... | ✅ |
| 652 | `lib_alias_vision.obs` | Other | The -lib alias @vision links: it is closed under its members' dependencies. EXTRA_LIBS: @vision A... | ✅ |
| 653 | `lib_interface_unqualified.obs` | Other | A library class's interface does not depend on the program's `use` list Inside bundle System.ML,... | ✅ |
| 654 | `lib_uninit_gemini.obs` | Other | A reply part of a kind the Gemini library does not model (here a function call with no callback r... | ✅ |
| 655 | `lib_uninit_nlp_json.obs` | Other | Two library bugs that depended on a local declared without a value inside a loop keeping the prev... | ✅ |
| 656 | `lib_uninit_query.obs` | Other | Query.Structured bugs that depended on a local declared without a value inside a loop keeping the... | ✅ |
| 657 | `library_nil_input_test.obs` | Other | Library entry points that take text must treat Nil, and a document that is not what it should be,... | ✅ |
| 658 | `library_nil_response_test.obs` | Other | A request that gets no answer must not dereference Nil, tested offline against a closed local por... | ✅ |
| 659 | `library_response_helpers_test.obs` | Other | A reply with no body, or an error reply of an unexpected shape, must not dereference Nil (offline... | ✅ |
| 660 | `literal_and_call_indexing.obs` | Other | Regression: subscripting a literal or a call result (#1184). - `["a", "b"][1]` did not parse: the... | ✅ |
| 661 | `llm_util_extract_test.obs` | Other | API.LlmUtil JSON extraction from chatty model replies and the lenient repair. The repair table sh... | ✅ |
| 662 | `llm_util_schema_test.obs` | Other | API.LlmUtil JSON Schema validation, as a table of (schema, value, expected errors). Each expected... | ✅ |
| 663 | `llm_util_structured_test.obs` | Other | API.LlmUtil StructuredCall against a scripted fake ChatFn: a reply that is valid the first time,... | ✅ |
| 664 | `llm_util_template_test.obs` | Other | API.LlmUtil prompt templates and the prompt builder: every placeholder form, the filters, loops,... | ✅ |
| 665 | `llm_util_tokens_test.obs` | Other | API.LlmUtil token estimation and the context window. The estimator is checked on fixed samples: t... | ✅ |
| 666 | `loop_literal_condition.obs` | Other | Loops whose condition is a Bool literal, a Bool variable or a call. A 'for' whose condition was a... | ✅ |
| 667 | `lsp_features.obs` | LSP | lsp features | ✅ |
| 668 | `map_literal.obs` | Other | The map literal: `m : Map<String, IntRef> := {"a": 1, "b": 2};` builds the map where it is writte... | ✅ |
| 669 | `map_literal_inferred_values.obs` | Other | A constructor call that is the value of a map literal's entry takes its type arguments from the m... | ✅ |
| 670 | `match_statement.obs` | Other | 'match' as a statement: `match subject { pattern \| pattern => { ... } other => { ... } }`. It is... | ✅ |
| 671 | `match_value.obs` | Other | 'match' used as a value: `x := match subject { pattern \| pattern => value, other => value };` It... | ✅ |
| 672 | `match_value_gc.obs` | Other | A 'match' used as a value runs its select in the middle of the enclosing expression, so whatever... | ✅ |
| 673 | `math_bigint.obs` | Math | System.Math.BigInt: exact integer arithmetic at any size. EXTRA_LIBS: math Objeck's Int is 64-bit... | ✅ |
| 674 | `math_complex.obs` | Math | System.Math.Complex: complex numbers with Float parts. EXTRA_LIBS: math What is pinned here are t... | ✅ |
| 675 | `math_float_ops.obs` | Math | math float ops | ✅ |
| 676 | `math_log_exp.obs` | Math | math log exp | ✅ |
| 677 | `math_random_ops.obs` | Math | math random ops | ✅ |
| 678 | `math_rational.obs` | Math | System.Math.Rational: exact fractions, backed by BigInt. EXTRA_LIBS: math Backed by BigInt rather... | ✅ |
| 679 | `math_rounding.obs` | Math | math rounding | ✅ |
| 680 | `math_sqrt_ops.obs` | Math | math sqrt ops | ✅ |
| 681 | `math_trig_funcs.obs` | Math | math trig funcs | ✅ |
| 682 | `mcp_agent_deadline_test.obs` | MCP Server | The run's deadline reaches a tool that runs on the loop's own thread (API.Agent, API.MCP), agains... | ✅ |
| 683 | `mcp_agent_test.obs` | MCP Server | Hands declared functions and MCP servers to an agent (McpAgentTools), against a scripted model, s... | ✅ |
| 684 | `mcp_cancel_before_validation_test.obs` | MCP Server | A tools/call can be cancelled from the moment the server has it, not only once its arguments were... | ✅ |
| 685 | `mcp_core_test.obs` | MCP Server | The MCP library (API.MCP) with no process and no network beyond loopback: a server in the same pr... | ✅ |
| 686 | `mcp_debug_test.obs` | MCP Server | DEBUG VERSION of mcp_server_test.obs Identical to programs/regression/mcp_server_test.obs except:... | ✅ |
| 687 | `mcp_declare_test.obs` | MCP Server | Declaring MCP tools from types (McpToolSpec), the effect annotations, the per-tool counters, the... | ✅ |
| 688 | `mcp_http_sessionless_cancel_test.obs` | MCP Server | The streamable HTTP transport with sessions off (McpService->SetSessions(false)): one client cann... | ✅ |
| 689 | `mcp_res_prompt_tags_test.obs` | MCP Server | Functions marked with a resource line or a prompt line in their doc comment become MCP resources... | ✅ |
| 690 | `mcp_server_bad_config.obs` | MCP Server | McpServer->Serve with a configuration it cannot use. Serve used to read config->Get("http_port"),... | ✅ |
| 691 | `mcp_server_bound_port_test.obs` | MCP Server | McpServer->Serve with an "http_port" of 0: the system chooses the port and McpServer->GetBoundPor... | ✅ |
| 692 | `mcp_server_malformed_requests.obs` | MCP Server | McpRequestHandler->ProcessPost read client-supplied JSON without checks: root_json->Get("method")... | ✅ |
| 693 | `mcp_server_test.obs` | MCP Server | mcp server test | ✅ |
| 694 | `mcp_stdio_fixture.obs` | MCP Server | The server (and a client) that mcp_stdio_test.py drives over standard input and output. Run by th... | ✅ |
| 695 | `mcp_tool_dollar_test.obs` | MCP Server | A dollar sign in the doc comment of a function marked as a tool reaches the tool as written. The... | ✅ |
| 696 | `mcp_tool_tags_scope_test.obs` | MCP Server | Tagged functions whose types and defaults the compiler has to write back as source, and whose typ... | ✅ |
| 697 | `mcp_tool_tags_test.obs` | MCP Server | Functions marked with an '@tool' line in their doc comment become MCP tools: the compiler writes... | ✅ |
| 698 | `method_generics.obs` | Other | Method-level generics, phase 1 (#1242; docs/METHOD_GENERICS_DESIGN.md). A function or method decl... | ✅ |
| 699 | `method_generics_library.obs` | Other | Method-level generics in libraries (#1242, phase 2; docs/METHOD_GENERICS_DESIGN.md). A library's... | ✅ |
| 700 | `method_ref_sugar.obs` | Other | Method references without ceremony: `Class->Function` where a FuncRef is expected. The signature... | ✅ |
| 701 | `minor_gc_stress.obs` | Other | Regression for generational MINOR GC: old objects holding young references. 'keep' is an object a... | ✅ |
| 702 | `ml_activations.obs` | System.ML | Numerical correctness of the System.ML activation functions and scalers. Softmax normalized over... | ✅ |
| 703 | `ml_adaboost_test.obs` | System.ML | Regression tests for System.ML AdaBoost (overhaul phase 3): boosting over boolean decision stumps... | ✅ |
| 704 | `ml_api_test.obs` | System.ML | Regression tests for the System.ML estimator API consistency sweep (item 11): RandomForest Fit (r... | ✅ |
| 705 | `ml_base_rate_groups.obs` | System.ML | Metrics->PrecisionAtBaseRate, ShuffleLabels, RecallByGroup and UnseenGroups. PrecisionAtBaseRate... | ✅ |
| 706 | `ml_boost_accuracy.obs` | System.ML | System.ML.Boost against scikit-learn and XGBoost. The datasets are generated here with System.ML.... | ✅ |
| 707 | `ml_boost_hand.obs` | System.ML | System.ML.Boost on cases small enough to work out on paper, so the expected numbers are exact (th... | ✅ |
| 708 | `ml_boost_perf.obs` | System.ML | System.ML.Boost performance guard: 10,000 rows x 20 features. Fits depth-4 trees (learning rate 0... | ✅ |
| 709 | `ml_boost_persist.obs` | System.ML | System.ML.Boost model files and strings. Store/Load (a file) and Export/Import (hex text) must gi... | ✅ |
| 710 | `ml_boost_properties.obs` | System.ML | System.ML.Boost properties that must hold whatever the data: the training loss never goes up (squ... | ✅ |
| 711 | `ml_clip_encoder.obs` | System.ML | System.ML.Clip ClipEncoder end to end on a tiny ONNX "image tower", and the pair scoring that fee... | ✅ |
| 712 | `ml_clip_pairs.obs` | System.ML | System.ML.Clip FacePairs: the LFW pair-list reader, the CSV reader and the list operations. Needs... | ✅ |
| 713 | `ml_clip_preprocess.obs` | System.ML | System.ML.Clip ClipPreprocessor against Hugging Face's CLIPImageProcessor (Pillow). data/clip/gen... | ✅ |
| 714 | `ml_cnn_cuda.obs` | System.ML | System.ML.Cnn on the GPU, the optional CUDA backend: unit tests. Every test here compares the GPU... | ✅ |
| 715 | `ml_cnn_cuda_fallback.obs` | System.ML | System.ML.Cnn without a GPU: what a machine that has no CUDA library, no NVIDIA driver or no GPU... | ✅ |
| 716 | `ml_cnn_cuda_parity.obs` | System.ML | System.ML.Cnn on the GPU (the optional CUDA backend) against PyTorch and against the CPU engine.... | ✅ |
| 717 | `ml_cnn_dataset.obs` | System.ML | System.ML.Cnn ImageDataset, Augmentation and CnnTrainer. * the synthetic set is the same for one... | ✅ |
| 718 | `ml_cnn_edges.obs` | System.ML | System.ML.Cnn edge cases and exact-value checks. * parameter counts against the closed form (conv... | ✅ |
| 719 | `ml_cnn_gradcheck.obs` | System.ML | Finite-difference gradient checks of System.ML.Cnn: every layer type and whole networks. The chec... | ✅ |
| 720 | `ml_cnn_loader.obs` | System.ML | System.ML.Cnn image loading: the decoding worker threads and the prefetch of the next batch. * th... | ✅ |
| 721 | `ml_cnn_parity.obs` | System.ML | System.ML.Cnn against PyTorch. data/cnn_parity/*.txt are written by tools/ml/cnn_parity.py from P... | ✅ |
| 722 | `ml_cnn_parity64.obs` | System.ML | System.ML.Cnn against PyTorch in float64, at 64 x 64, over 20 Adam steps. The float32 parity test... | ✅ |
| 723 | `ml_cnn_train.obs` | System.ML | System.ML.Cnn learns: the baseline architecture (three Conv 3x3 + ReLU + MaxPool blocks with 16,... | ✅ |
| 724 | `ml_column_sum_fractions.obs` | System.ML | Matrix2D->SumColumn and Matrix2D->AverageColumn started their accumulator as an Int (`sum := 0;`)... | ✅ |
| 725 | `ml_cross_val_oof.obs` | System.ML | CrossValidation: out-of-fold scores, the fold row mapping, stratification by a key other than the... | ✅ |
| 726 | `ml_dbscan_test.obs` | System.ML | Regression tests for System.ML DBSCAN (overhaul phase 3): two dense blobs plus far-away outliers... | ✅ |
| 727 | `ml_decision_tree_classifier.obs` | System.ML | DecisionTreeClassifier: continuous splits chosen by Gini. DecisionTree takes Bool[,], so continuo... | ✅ |
| 728 | `ml_degenerate_ratios.obs` | System.ML | Degenerate inputs must not take the VM down. Float division by zero RAISES in Objeck rather than... | ✅ |
| 729 | `ml_embedding_classifier.obs` | System.ML | EmbeddingClassifier: a logistic or multi-layer head over fixed-size embeddings, trained by miniba... | ✅ |
| 730 | `ml_experiment_runner.obs` | System.ML | CrossValidation's experiment runner: ScoreModel, Split and Evaluate. Two seams made every experim... | ✅ |
| 731 | `ml_feature_scaler_stateful.obs` | System.ML | FeatureScaler's stateful Fit/Transform pair. The static StandardScaler standardizes whatever matr... | ✅ |
| 732 | `ml_forest_lab_params.obs` | System.ML | The random-forest parameters a lab sweeps, on DecisionTreeClassifier and RandomForestClassifier:... | ✅ |
| 733 | `ml_forest_score_auc.obs` | System.ML | RandomForest->PredictScore (votes_true / ntrees) and AUC against hand-computed values. ml_score_m... | ✅ |
| 734 | `ml_gbt_classifier.obs` | System.ML | GradientBoostedClassifier: boosting under logistic loss. The regression GradientBoostedTrees fits... | ✅ |
| 735 | `ml_gbt_test.obs` | System.ML | Regression tests for System.ML gradient boosting (overhaul phase 3 leftover): a RegressionTree le... | ✅ |
| 736 | `ml_gmm_test.obs` | System.ML | Regression tests for System.ML GaussianMixture (overhaul phase 3): EM on two well-separated blobs... | ✅ |
| 737 | `ml_input_validation.obs` | System.ML | Fit, Predict, Transform and Score on a Nil, an empty or a wrong-width matrix, or on targets that... | ✅ |
| 738 | `ml_kdtree_test.obs` | System.ML | Regression tests for System.ML KDTree (overhaul phase 3): for several queries and k values over a... | ✅ |
| 739 | `ml_kernel_svm_edges.obs` | System.ML | KernelSVM edge cases, parameter semantics and determinism. Bad input is refused with a reason (on... | ✅ |
| 740 | `ml_kernel_svm_geometry.obs` | System.ML | KernelSVM against known geometry, with the optimality (KKT) conditions checked after every fit fr... | ✅ |
| 741 | `ml_kernel_svm_reference.obs` | System.ML | KernelSVM against an independent reference, without Python. Generated by tools/ml/svm_reference/g... | ✅ |
| 742 | `ml_kmeans_group_order.obs` | System.ML | KMeansGrouping lists its groups in a defined order: KMeans->Group keeps the order of its labels (... | ✅ |
| 743 | `ml_kmeans_groups.obs` | System.ML | KMeans->Group returned empty groups, and GetDunnIndex then divided by zero. Group filed each reco... | ✅ |
| 744 | `ml_knn_exact.obs` | System.ML | System.ML KNearestNeighbors->Query keeps the best k rows in a heap instead of sorting every row.... | ✅ |
| 745 | `ml_knn_rounded_ties.obs` | System.ML | System.ML KNearestNeighbors->Query reports a distance (the square root of the squared sum), and r... | ✅ |
| 746 | `ml_lab_chart_svg.obs` | System.ML | System.ML.Lab.Chart: standalone SVG line, bar and loss-curve charts. Three kinds of check. Struct... | ✅ |
| 747 | `ml_lab_experiment.obs` | System.ML | System.ML.Lab.Experiment: a sweep over one parameter, with repeats. The Trial here returns predic... | ✅ |
| 748 | `ml_lab_labels_encoding.obs` | System.ML | System.ML.Lab.Labels, and the NSL-KDD encoding path through TableEncoder and FeatureScaler. The l... | ✅ |
| 749 | `ml_lab_non_finite.obs` | System.ML | System.ML.Lab non-finite values: a NaN, +Infinity or -Infinity in a ResultTable cell, deviation o... | ✅ |
| 750 | `ml_lab_resample.obs` | System.ML | System.ML.Lab.Resample: a training set at a requested positive ratio, with the test set left exac... | ✅ |
| 751 | `ml_lab_result_table.obs` | System.ML | System.ML.Lab.ResultTable: the table a sweep hands in, rendered four ways and read back from two... | ✅ |
| 752 | `ml_lab_result_table_formats.obs` | System.ML | System.ML.Lab.ResultTable number formats: scientific notation, a "<0.0001" form for tiny p-values... | ✅ |
| 753 | `ml_lab_split.obs` | System.ML | System.ML.Lab.Partition: a deterministic, stratified hold-out split. The counts are exact, not ap... | ✅ |
| 754 | `ml_lab_stopwatch.obs` | System.ML | System.ML.Lab.Stopwatch: wall-clock milliseconds that accumulate across Start/Stop pairs until Re... | ✅ |
| 755 | `ml_library_test.obs` | System.ML | ml library test | ✅ |
| 756 | `ml_linearclf_test.obs` | System.ML | Regression tests for the System.ML linear classifiers (overhaul phase 2): Perceptron (mistake-dri... | ✅ |
| 757 | `ml_load_corrupt_model.obs` | System.ML | A model file that exists but is not a model must make Load answer Nil, not end the program. Every... | ✅ |
| 758 | `ml_load_truncated_model.obs` | System.ML | A model file that is valid zlib but short, or that claims more than it holds, makes Load answer N... | ✅ |
| 759 | `ml_matrix_reader_columns.obs` | System.ML | MatrixReader's target_offset is the number of target columns, taken from the end of each row. Its... | ✅ |
| 760 | `ml_matrix_reader_line_endings.obs` | System.ML | MatrixReader reads rows through CsvTable, which used to split on CRLF unless it was handed anothe... | ✅ |
| 761 | `ml_matrix_shape_mismatch.obs` | System.ML | Matrix2D operations on operands they cannot combine must return Nil, not fault. The native matrix... | ✅ |
| 762 | `ml_nn_test.obs` | System.ML | Regression tests for the System.ML NeuralNetwork with hidden/output bias vectors (ML overhaul ite... | ✅ |
| 763 | `ml_one_class_svm.obs` | System.ML | OneClassSVM: the nu property, the optimality conditions, sign conventions and novelty detection.... | ✅ |
| 764 | `ml_one_class_svm_reference.obs` | System.ML | OneClassSVM against an independent reference, without Python. Generated by tools/ml/svm_reference... | ✅ |
| 765 | `ml_one_class_svm_tolerance.obs` | System.ML | OneClassSVM: why the flagged training fraction can exceed nu at the default tolerance, and that i... | ✅ |
| 766 | `ml_outlier_autoencoder.obs` | System.ML | System.ML DenseNetwork and AutoencoderDetector against an independent numpy reference. The refere... | ✅ |
| 767 | `ml_outlier_dbscan.obs` | System.ML | System.ML DbscanDetector: DBSCAN's noise as an OutlierDetector. The outliers must be exactly the... | ✅ |
| 768 | `ml_outlier_envelope.obs` | System.ML | System.ML EllipticEnvelope (FastMCD) against independent references. * Chi-square quantiles again... | ✅ |
| 769 | `ml_outlier_iforest.obs` | System.ML | System.ML IsolationForest. The average path length c(n) is checked exactly against the formula. A... | ✅ |
| 770 | `ml_outlier_lof.obs` | System.ML | System.ML LocalOutlierFactor against an independent brute-force reference. LOF is deterministic,... | ✅ |
| 771 | `ml_outlier_mcd.obs` | System.ML | System.ML EllipticEnvelope's MCD search against an independent reference, on 30 contaminated near... | ✅ |
| 772 | `ml_outlier_ocsvm.obs` | System.ML | System.ML OneClassSvmDetector: OneClassSVM through the OutlierDetector interface. The adapter mus... | ✅ |
| 773 | `ml_outlier_sweep.obs` | System.ML | System.ML ConfusionMatrix, Metrics->FalsePositiveRate and SensitivitySweep. ConfusionMatrix is ch... | ✅ |
| 774 | `ml_outlier_zscore.obs` | System.ML | System.ML ZScoreDetector against an independent reference. The expected scores are numpy's (scipy... | ✅ |
| 775 | `ml_paired_bootstrap.obs` | System.ML | ml paired bootstrap | ✅ |
| 776 | `ml_pca_gnb_test.obs` | System.ML | Regression tests for System.ML PCA (power-iteration decomposition: dominant diagonal direction re... | ✅ |
| 777 | `ml_phase1_test.obs` | System.ML | Regression tests for the System.ML correctness fixes (phase 1): seedable PRNG, DotSigmoid dimensi... | ✅ |
| 778 | `ml_random_forest_classifier.obs` | System.ML | RandomForestClassifier: bootstrapped, feature-sampled continuous trees, averaged. Two properties... | ✅ |
| 779 | `ml_regularized_test.obs` | System.ML | Regression tests for the System.ML regularized linear models (overhaul phase 2): RidgeRegression... | ✅ |
| 780 | `ml_report_build.obs` | System.ML | System.ML.Report Report and ResultTables: escaping, numbering, tables, figures, files and the deg... | ✅ |
| 781 | `ml_report_csv_manifest.obs` | System.ML | System.ML.Report: reading a CSV without ending the program, and the pieces a study report needs b... | ✅ |
| 782 | `ml_report_golden.obs` | System.ML | System.ML.Report golden files: the miniature assignment of programs/examples/ml/study_report.obs... | ✅ |
| 783 | `ml_report_replicates.obs` | System.ML | System.ML.Report Replicates: the statistics a hand-in reports, against numpy, scipy and statsmode... | ✅ |
| 784 | `ml_score_metrics.obs` | System.ML | Score-based metrics: RecallAtFpr, ThresholdAtFpr, AucRoc, AveragePrecision and the two curves. Me... | ✅ |
| 785 | `ml_sort_depth.obs` | System.ML | System.ML's two hand-written quicksorts: KDTree's SortByDim (sorts row indexes by one coordinate)... | ✅ |
| 786 | `ml_stratified_kfold.obs` | System.ML | CrossValidation->StratifiedKFold: folds that keep the class ratio, from a seed. The existing KFol... | ✅ |
| 787 | `ml_table_encoder.obs` | System.ML | TableEncoder: CsvTable to Float[,] with a vocabulary fixed at Fit. Two properties carry this test... | ✅ |
| 788 | `ml_threshold_bootstrap.obs` | System.ML | Metrics->AtThreshold and Metrics->Bootstrap, plus Percentile underneath them. AtThreshold is chec... | ✅ |
| 789 | `ml_timeseries_arima.obs` | System.ML | System.ML.TimeSeries: ARIMA(p, d, q) with optional seasonal differencing. Series with known param... | ✅ |
| 790 | `ml_timeseries_edges.obs` | System.ML | System.ML.TimeSeries: edge cases and invariances. Series shorter than the period, constant series... | ✅ |
| 791 | `ml_timeseries_eval.obs` | System.ML | System.ML.TimeSeries: accuracy metrics, rolling-origin cross-validation, interval coverage and au... | ✅ |
| 792 | `ml_timeseries_smoothing.obs` | System.ML | System.ML.TimeSeries: the baseline forecasters and exponential smoothing. Baselines are checked o... | ✅ |
| 793 | `ml_timeseries_transform.obs` | System.ML | System.ML.TimeSeries: the container, the transforms, autocorrelation and the stationarity test. H... | ✅ |
| 794 | `ml_trees_test.obs` | System.ML | Regression tests for the System.ML tree models: the real recursive DecisionTree (left/right child... | ✅ |
| 795 | `ml_vector_edges.obs` | System.ML | System.ML.VectorSearch VectorIndex edges that the reference-comparison test does not reach. Magni... | ✅ |
| 796 | `ml_vector_exact.obs` | System.ML | System.ML.VectorSearch VectorIndex: Search is exact. For cosine, dot and L2 the top-k ids and sco... | ✅ |
| 797 | `ml_vector_parallel.obs` | System.ML | System.ML.VectorSearch VectorIndex->SetThreads: a search split over several threads must return e... | ✅ |
| 798 | `ml_vector_parallel_full.obs` | System.ML | System.ML.VectorSearch VectorIndex->Search over the whole index (k = the number of vectors, and k... | ✅ |
| 799 | `ml_vector_persist.obs` | System.ML | System.ML.VectorSearch VectorIndex Store and Load. A saved index must load back to an index that... | ✅ |
| 800 | `ml_vector_validation.obs` | System.ML | System.ML.VectorSearch VectorIndex: bad arguments return a value and never end the program. Check... | ✅ |
| 801 | `ml_verify_metrics.obs` | System.ML | System.ML.Verification against independent numbers. cases.txt holds 14 sets of scores and same /... | ✅ |
| 802 | `named_arguments.obs` | Other | Named arguments, phase 1 (#1244; docs/NAMED_ARGUMENTS_DESIGN.md). `name: value` binds an argument... | ✅ |
| 803 | `named_arguments_library.obs` | Other | Named arguments to library methods (#1244, phase 2; docs/NAMED_ARGUMENTS_DESIGN.md). A library's... | ✅ |
| 804 | `native_gc_barrier_test.obs` | Other | A value returned by a native library must survive a collection. A C++ shared library returns a va... | ✅ |
| 805 | `native_gc_leak_test.obs` | Other | Objects a native library returns must be RECLAIMED, not merely reachable. native_gc_barrier_test.... | ✅ |
| 806 | `native_lib_gc_park.obs` | Other | Blocking calls in native libraries must park for garbage collection A stop-the-world collection w... | ✅ |
| 807 | `net_io_gc_park.obs` | Other | Blocking I/O must park for garbage collection: DTLS, HTTP/3, UDP, named pipes and Runtime->Comman... | ✅ |
| 808 | `net_resolve_failure.obs` | Other | TCPSocket->Resolve failure-path test Resolve() freed its addrinfo result on the FAILURE path, whe... | ✅ |
| 809 | `new_array_indexed_size.obs` | Other | Regression: `T->New[a[i]]` was compiled as a COPY of the array a, not as an array of a[i] element... | ✅ |
| 810 | `nil_coalesce_call_arguments.obs` | Other | Regression: words left on the operand stack by `??` chains and by statements with a parenthesised... | ✅ |
| 811 | `nil_coalesce_values.obs` | Other | Regression: `??` after a guarded call that yields a value type (#1172). `??` tested its left side... | ✅ |
| 812 | `nil_guard.obs` | Other | Nil-guard-and-bail: `x := E ?? return V;`, `?? break;`, `?? continue;` and the block form `x := E... | ✅ |
| 813 | `nil_safe_ops.obs` | Core Language | Nil-safe operators: '??' (nil-coalesce) and '?->' (nil-safe call). Both desugar onto existing int... | ✅ |
| 814 | `nil_safe_statement.obs` | Core Language | Regression: a `?->` call used as a statement (#1113). `t?->Down();` was emitted link by link as p... | ✅ |
| 815 | `nlp_api_sequence.obs` | System.NLP | ApiSequenceTokenizer: a vocabulary of API names built from training traces, and the encodings of... | ✅ |
| 816 | `nlp_bpe_tokenizer.obs` | System.NLP | BpeTokenizer: byte-level BPE (RoBERTa, GPT-2), id for id and offset for offset with Hugging Face'... | ✅ |
| 817 | `nlp_levenshtein_bounded.obs` | System.NLP | System.NLP TextSimilarity->LevenshteinDistance: the two-row version must equal the textbook full... | ✅ |
| 818 | `nlp_levenshtein_ceiling.obs` | System.NLP | System.NLP TextSimilarity->LevenshteinDistance with a ceiling: an effectively unbounded ceiling (... | ✅ |
| 819 | `nlp_onnx_embedder.obs` | System.NLP | BertEmbedder: sentence embeddings from a BERT-shaped ONNX model, checked against the model's clos... | ✅ |
| 820 | `nlp_onnx_layers.obs` | System.NLP | BertEmbedder layer pooling: the final layer (as always), the sum of the last four hidden layers a... | ✅ |
| 821 | `nlp_onnx_text_classifier.obs` | System.NLP | TextClassifier: a fine-tuned classifier opened from its model folder, checked against Python. EXT... | ✅ |
| 822 | `nlp_porter.obs` | System.NLP | System.NLP Porter stemmer (docs/NLP_DESIGN.md, section 4): the worked examples from Porter's 1980... | ✅ |
| 823 | `nlp_sentiment.obs` | System.NLP | System.NLP sentiment (docs/NLP_DESIGN.md, section 5): each VADER-style rule on its own (negation,... | ✅ |
| 824 | `nlp_spell_features.obs` | System.NLP | System.NLP SpellChecker and TextFeatures, with values worked out by hand. SpellChecker: a word li... | ✅ |
| 825 | `nlp_tfidf.obs` | System.NLP | System.NLP TF-IDF and similarity (docs/NLP_DESIGN.md, sections 6 and 7): a refit starts from an e... | ✅ |
| 826 | `nlp_tfidf_ngrams.obs` | System.NLP | System.NLP TF_IDF n-grams, min_df and max_features, checked against scikit-learn. The expected vo... | ✅ |
| 827 | `nlp_tokenize.obs` | System.NLP | System.NLP tokenization (docs/NLP_DESIGN.md, sections 2 and 3): contractions, numbers, hyphenated... | ✅ |
| 828 | `nlp_topics_bson.obs` | System.NLP | System.NLP.Topics BsonReader: the decoder, the streaming reader, field extraction, and what it do... | ✅ |
| 829 | `nlp_topics_bson_pymongo.obs` | System.NLP | System.NLP.Topics BsonReader against pymongo's bson package. The bytes below were produced by bso... | ✅ |
| 830 | `nlp_topics_dictionary.obs` | System.NLP | System.NLP.Topics text side: stopword lists, the tokenizer, TokenDictionary (counts, FilterExtrem... | ✅ |
| 831 | `nlp_topics_lda.obs` | System.NLP | System.NLP.Topics Lda: the model against independently computed values, planted-topic recovery, d... | ✅ |
| 832 | `nlp_topics_metrics.obs` | System.NLP | System.NLP.Topics TopicMetrics: u_mass, c_npmi and c_v coherence, topic diversity, overlap and st... | ✅ |
| 833 | `nlp_topics_plots.obs` | System.NLP | System.NLP.Topics TopicPlots: the data behind the static topic report and the SVG itself. Checked... | ✅ |
| 834 | `nlp_wordpiece.obs` | System.NLP | WordPieceTokenizer: BERT's BasicTokenizer and WordPiece, and the encodings a BERT-family model ta... | ✅ |
| 835 | `nlp_wordpiece_parity.obs` | System.NLP | WordPieceTokenizer gives the same ids as BERT's own tokenizer on a corpus: 234 texts, each in thr... | ✅ |
| 836 | `oauth_test.obs` | Networking | oauth test | ✅ |
| 837 | `obj_size_layout.obs` | Other | Object field layout must fit the object allocation exactly. The compiler records a class's instan... | ✅ |
| 838 | `odbc_prepared_reuse.obs` | ODBC | A Database.ODBC ParameterStatement runs as many times as its caller likes. The statement's column... | ✅ |
| 839 | `odbc_sqlite_test.obs` | ODBC | ODBC SQLite Integration Test Tests live database operations against an in-memory SQLite database:... | ✅ |
| 840 | `odbc_whole_values.obs` | ODBC | Database.ODBC reads and writes whole values, and says why a SELECT failed. Four defects in the OD... | ✅ |
| 841 | `ollama_api_test.obs` | Other | API.Ollama against recorded payloads, no server needed: chat tool calling (the tool_calls reply,... | ✅ |
| 842 | `ollama_parse_test.obs` | Other | Completion->ParseGenerateResponse: the response handling behind every Completion->Generate overlo... | ✅ |
| 843 | `onnx_concurrency.obs` | Other | One ONNX Runtime session used by several Objeck threads, beside a thread that makes garbage. EXTR... | ✅ |
| 844 | `onnx_concurrency_gpu.obs` | Other | The same on the GPU: several Objeck threads share one CUDA or TensorRT session. Skips without a G... | ✅ |
| 845 | `onnx_failed_session.obs` | Other | A failed ONNX session is detectable, and inference refuses instead of aborting. EXTRA_LIBS: model... | ✅ |
| 846 | `onnx_float16.obs` | Other | float16, bfloat16 and 8-bit element types in API.Onnx. EXTRA_LIBS: models,onnx,opencv,cipher,json... | ✅ |
| 847 | `onnx_generic_run.obs` | Other | The generic ONNX run entry point: its refusals, and the tensor bookkeeping around it. EXTRA_LIBS:... | ✅ |
| 848 | `onnx_gpu_options.obs` | Other | GPU provider options: the CUDA and TensorRT option sets, CUDA graphs and the engine cache. EXTRA_... | ✅ |
| 849 | `onnx_iobinding.obs` | Other | IO binding: DeviceTensor and BoundSession. EXTRA_LIBS: models,onnx,opencv,cipher,json A normal Se... | ✅ |
| 850 | `onnx_iobinding_gpu.obs` | Other | IO binding on the GPU: the checks of onnx_iobinding.obs, repeated on ep=cuda and ep=tensorrt, plu... | ✅ |
| 851 | `onnx_provider_select.obs` | Other | The ONNX execution provider is chosen per session with "ep", the runtime can be asked what it car... | ✅ |
| 852 | `onnx_runtime_test.obs` | Other | API.Onnx.OnnxRuntime->GetProviders() reaches the native ONNX Runtime. EXTRA_LIBS: models,onnx,ope... | ✅ |
| 853 | `onnx_session_options.obs` | Other | Session options in the config map, and the TensorRT provider's refusal. EXTRA_LIBS: models,onnx,o... | ✅ |
| 854 | `onnx_tiny_bert_model.obs` | Other | The tiny BERT-shaped test models under data/bert run on this platform's ONNX Runtime and compute... | ✅ |
| 855 | `openai_parse_test.obs` | Other | API.OpenAI response parsing, tested offline from recorded bodies that follow the public API refer... | ✅ |
| 856 | `openai_request_test.obs` | Other | API.OpenAI request bodies, built offline (no key, no network). Defect this guards: Responses.Resp... | ✅ |
| 857 | `openai_tool_calls_test.obs` | Other | API.OpenAI Chat Completions tool calls over the wire, against a local HTTPS server that answers w... | ✅ |
| 858 | `opencv_batch_unload.obs` | Other | opencv batch unload | ✅ |
| 859 | `opt_arith_identities.obs` | Other | Integer identities and multiply-by-a-power-of-two (-opt s2 and s3), checked at the edges. The com... | ✅ |
| 860 | `opt_dead_store_chain.obs` | Other | Regression: a dead store inside a chained assignment kept its value on the stack. seen := false;... | ✅ |
| 861 | `opt_dead_store_first_instr.obs` | Other | Regression: a dead store at a method's first instruction kept its value. Found by rosetta_atomic_... | ✅ |
| 862 | `opt_dead_store_liveness.obs` | Other | Dead-store elimination on the control-flow graph: what goes and what must stay. DeadStore used to... | ✅ |
| 863 | `opt_dead_store_side_effects.obs` | Other | Regression: dead-store elimination dropped side effects. Found by the differential fuzzer against... | ✅ |
| 864 | `opt_dead_store_stack_balance.obs` | Other | Regression: optimizer passes that left values on the operand stack. Found by the fuzzer against v... | ✅ |
| 865 | `opt_funcref_local_slots.obs` | Other | Optimizer slot numbering with a function-reference local. A func-ref local takes two slots, but t... | ✅ |
| 866 | `opt_inline_and_or_slots.obs` | Other | An object local of a method inlined at -opt s3 must stay a traced root. The inliner appends the c... | ✅ |
| 867 | `opt_inline_label_collision.obs` | Other | Two inlined bodies must not share a label space (#1037). A method's labels are numbered from its... | ✅ |
| 868 | `opt_inline_later_class.obs` | Other | Inlining must not depend on which class a callee is in. ItermediateOptimizer::Optimize used to op... | ✅ |
| 869 | `opt_inline_locals_zeroed.obs` | Other | An inlined call starts with its locals zeroed, as a call does (#1464). A local declared without a... | ✅ |
| 870 | `opt_int_division.obs` | Other | Integer division must give the same answer at every optimization level. Two -opt rewrites produce... | ✅ |
| 871 | `opt_licm_cfg.obs` | Other | Loop-invariant code motion on the control-flow graph: what moves and what must not. LICM moves a... | ✅ |
| 872 | `opt_licm_store_hoist.obs` | Other | Loop-invariant code motion must not hoist a store that did not always run first. The optimizer mo... | ✅ |
| 873 | `parser_gaps.obs` | Other | Regression: parse gaps found by the Rosetta batch-3 tasks (#1184). - An array literal ate the com... | ✅ |
| 874 | `pipe_closed_peer.obs` | Other | A write to a pipe whose other end has closed fails; it does not end the VM On POSIX a Pipe is a U... | ✅ |
| 875 | `plot_edge_cases.obs` | Other | System.Plot edge cases found by an independent review, each reproduced before it was fixed. The g... | ✅ |
| 876 | `plot_sdl.obs` | Other | System.Plot.SDL: the SDL2 backend, headless. SDL_VIDEODRIVER and SDL_AUDIODRIVER are set to "dumm... | ✅ |
| 877 | `plot_svg.obs` | Other | System.Plot: scatter, histogram, line and bar charts as standalone SVG. Four kinds of check. Stru... | ✅ |
| 878 | `plot_svg_more.obs` | Other | System.Plot, second set: heatmap, confusion matrix, box plot, ROC and precision-recall curves, te... | ✅ |
| 879 | `plot_view.obs` | Other | System.Plot interaction model: what a window does with the mouse, tested without one. Zoom and pa... | ✅ |
| 880 | `primitive_receiver_call_result.obs` | Other | Regression: a primitive method called on a CALL RESULT got its arguments reversed. Found by the R... | ✅ |
| 881 | `primitive_receiver_order.obs` | Other | Argument order for instance-style calls on primitives. Writing `v->Pow(10)` on a primitive does n... | ✅ |
| 882 | `primitive_receiver_resolution.obs` | Other | Calls on primitive receivers resolve to the $-class function that takes the receiver as parameter... | ✅ |
| 883 | `prompt_literal_test.obs` | Other | Prompt literals (docs/design/AGENT_SYNTAX.md, section 2): 'prompt "..."' with {name} and {name :... | ✅ |
| 884 | `query_aggregates.obs` | Query.Structured | Regression tests for Query.Structured (query.obl), which had none: - Average, Sum, Min, Max and C... | ✅ |
| 885 | `query_archives_in_sql.obs` | Query.Structured | Query.Structured: archives and workbooks named in SQL (docs/QUERY_DESIGN.md, "Archives and workbo... | ✅ |
| 886 | `query_bridges.obs` | Query.Structured | Query.Structured's bridges (docs/QUERY_DESIGN.md section 7, phase 4c): a table or a view out to C... | ✅ |
| 887 | `query_catalog_concurrent.obs` | Query.Structured | Query.Structured: one Catalog shared by several threads (docs/QUERY_DESIGN.md, "Concurrent reader... | ✅ |
| 888 | `query_differential.obs` | Query.Structured | Query.Structured differential test (docs/QUERY_DESIGN.md, "Testing"). Random tables (every column... | ✅ |
| 889 | `query_expr_concurrent.obs` | Query.Structured | Query.Structured: expressions under one Catalog shared by several threads (docs/QUERY_DESIGN.md,... | ✅ |
| 890 | `query_expr_differential.obs` | Query.Structured | Query.Structured expressions, differentially (docs/QUERY_DESIGN.md, "Expressions in every clause"... | ✅ |
| 891 | `query_expr_grouped.obs` | Query.Structured | Query.Structured: expressions in grouped queries (docs/QUERY_DESIGN.md, "Expressions in every cla... | ✅ |
| 892 | `query_expr_join.obs` | Query.Structured | Query.Structured: expressions in JOIN ... ON (docs/QUERY_DESIGN.md, "Expressions in every clause"... | ✅ |
| 893 | `query_expr_where.obs` | Query.Structured | Query.Structured: expressions in WHERE and ORDER BY (docs/QUERY_DESIGN.md, "Expressions in every... | ✅ |
| 894 | `query_expr_zone_maps.obs` | Query.Structured | Query.Structured expressions across row-group and batch boundaries (docs/QUERY_DESIGN.md, "Reduct... | ✅ |
| 895 | `query_file_rewritten.obs` | Query.Structured | Query.Structured: a file rewritten within the second it was read, to the same size, is read again... | ✅ |
| 896 | `query_index_basic.obs` | Query.Structured | Query.Structured indexes (docs/QUERY_DESIGN.md, section 5, phase 6): hash and sorted indexes on e... | ✅ |
| 897 | `query_index_concurrent.obs` | Query.Structured | Query.Structured: one table queried from several threads right after a write (docs/QUERY_DESIGN.m... | ✅ |
| 898 | `query_index_differential.obs` | Query.Structured | Query.Structured indexes, differentially (docs/QUERY_DESIGN.md, sections 5 and "Testing"). For a... | ✅ |
| 899 | `query_index_plan.obs` | Query.Structured | Query.Structured's rule-based planner (docs/QUERY_DESIGN.md, section 5, phase 6): the plan a view... | ✅ |
| 900 | `query_nan_compare.obs` | Query.Structured | NaN in Query.Structured conditions and sorts (found by query_index_differential). The comparison... | ✅ |
| 901 | `query_net_range.obs` | Query.Structured | Query.Structured reading an .otab file over HTTP (docs/QUERY_DESIGN.md, section 10, phase 5), aga... | ✅ |
| 902 | `query_net_read.obs` | Query.Structured | Query.Structured reading files over HTTP (docs/QUERY_DESIGN.md, section 10, phase 5), against an... | ✅ |
| 903 | `query_net_serve.obs` | Query.Structured | Query.Structured serving tables (docs/QUERY_DESIGN.md, section 10, phase 5), against an in-proces... | ✅ |
| 904 | `query_otab_gmt_groups.obs` | Query.Structured | Query.Structured's .otab file: a Date column whose row groups mix GMT and local dates. Each group... | ✅ |
| 905 | `query_persist_crash.obs` | Query.Structured | Query.Structured's .otab saves are crash safe (docs/OTAB_FORMAT.md, "Writing a file"): Save and A... | ✅ |
| 906 | `query_persist_indexes.obs` | Query.Structured | Query.Structured: index definitions stored in .otab files (docs/OTAB_FORMAT.md, format 2; docs/QU... | ✅ |
| 907 | `query_persist_lock.obs` | Query.Structured | Query.Structured's writer lock (QUERY_DESIGN.md, phase 3b): opening a table for writing takes 'pa... | ✅ |
| 908 | `query_persist_log.obs` | Query.Structured | Query.Structured's durable tables (docs/OTAB_FORMAT.md, "The change log"; QUERY_DESIGN.md, phase... | ✅ |
| 909 | `query_persist_roundtrip.obs` | Query.Structured | Query.Structured's .otab file (docs/QUERY_DESIGN.md, section 9, phase 3): a table with every colu... | ✅ |
| 910 | `query_select_expressions.obs` | Query.Structured | Query.Structured: arithmetic in the select list (docs/QUERY_DESIGN.md, section 4, "Expressions in... | ✅ |
| 911 | `query_serialize_generic.obs` | Query.Structured | Query.Structured tables through the language's own Serializer and Deserializer (docs/QUERY_DESIGN... | ✅ |
| 912 | `query_server_concurrent.obs` | Query.Structured | Query.Structured's TableServer answering requests at once (docs/QUERY_DESIGN.md, "A server that a... | ✅ |
| 913 | `query_source_archive.obs` | Query.Structured | Query.Structured ArchiveSource (docs/QUERY_DESIGN.md, section 6, phase 7): a zip, a tar and a gzi... | ✅ |
| 914 | `query_source_csv.obs` | Query.Structured | Query.Structured CsvSource (docs/QUERY_DESIGN.md, section 6): whole-column typing, empty cells, d... | ✅ |
| 915 | `query_source_files_in_sql.obs` | Query.Structured | Query.Structured files named in SQL (docs/QUERY_DESIGN.md, section 6, phase 4): a quoted path in... | ✅ |
| 916 | `query_source_filesystem.obs` | Query.Structured | Query.Structured FilesystemSource and Finder (docs/QUERY_DESIGN.md, section 6): a flat and a recu... | ✅ |
| 917 | `query_source_gzip.obs` | Query.Structured | Query.Structured reads gzipped sources (docs/QUERY_DESIGN.md, section 6, phase 2): a '.gz' CSV, J... | ✅ |
| 918 | `query_source_http.obs` | Query.Structured | Query.Structured HttpJsonSource (docs/QUERY_DESIGN.md, sections 6 and 10, phase 5), against an in... | ✅ |
| 919 | `query_source_json.obs` | Query.Structured | Query.Structured JsonSource (docs/QUERY_DESIGN.md, section 6, phase 4): an array of objects as a... | ✅ |
| 920 | `query_source_ml.obs` | Query.Structured | Query.Structured's System.ML source, MlResultSource (docs/QUERY_DESIGN.md section 6, tier 2, quer... | ✅ |
| 921 | `query_source_objects.obs` | Query.Structured | Query.Structured ObjectSource (docs/QUERY_DESIGN.md, section 6): the program's own objects as a t... | ✅ |
| 922 | `query_source_odbc.obs` | Query.Structured | Query.Structured's ODBC source, OdbcSource (docs/QUERY_DESIGN.md section 6, tier 2, query_odbc.ob... | ✅ |
| 923 | `query_source_text.obs` | Query.Structured | Query.Structured TextSource and JsonLinesSource (docs/QUERY_DESIGN.md, section 6, phase 2): a log... | ✅ |
| 924 | `query_source_tree.obs` | Query.Structured | Query.Structured over a directory tree (docs/QUERY_DESIGN.md, section 6, phase 4): the recursive... | ✅ |
| 925 | `query_source_vision.obs` | Query.Structured | Query.Structured's vision source, VisionSource (docs/QUERY_DESIGN.md section 6, tier 2, query_vis... | ✅ |
| 926 | `query_source_xlsx.obs` | Query.Structured | Query.Structured XlsxSource (docs/QUERY_DESIGN.md, section 6, phase 7): worksheets of an .xlsx wo... | ✅ |
| 927 | `query_source_xml.obs` | Query.Structured | Query.Structured XmlSource (docs/QUERY_DESIGN.md, section 6, phase 4): row elements found at any... | ✅ |
| 928 | `query_sql_relational.obs` | Query.Structured | Query.Structured's relational SQL (docs/QUERY_DESIGN.md, sections 4 and 6, phase 4): GROUP BY wit... | ✅ |
| 929 | `query_storage.obs` | Query.Structured | Query.Structured column storage (docs/QUERY_DESIGN.md, section 1, phase 1): typing and promotion,... | ✅ |
| 930 | `query_thread_errors.obs` | Query.Structured | Query.Structured: GetError reports the calling thread's last error (docs/QUERY_DESIGN.md, "Concur... | ✅ |
| 931 | `query_unknown_column.obs` | Query.Structured | Query.Structured: a name that is not a column is an error in every clause. The defect: 'select re... | ✅ |
| 932 | `query_url_cache_processes.obs` | Query.Structured | Query.Structured's URL download cache shared by several processes (docs/QUERY_DESIGN.md, "One cac... | ✅ |
| 933 | `query_views.obs` | Query.Structured | Query.Structured predicates, views and prepared statements (docs/QUERY_DESIGN.md, sections 2 and... | ✅ |
| 934 | `query_zone_map_wide_ints.obs` | Query.Structured | Zone maps keep each row group's minimum and maximum as Floats. An Int above 2^53 rounds when conv... | ✅ |
| 935 | `rag_adapters_ollama_test.obs` | Other | The RAG kit's Ollama adapters (RagOllamaEmbedder, RagOllamaGenerator) against a fake Ollama serve... | ✅ |
| 936 | `rag_chunking_test.obs` | Other | The RAG kit's chunker (System.ML.Rag.RagChunker): windows by size with overlap, whole paragraphs... | ✅ |
| 937 | `rag_index_test.obs` | Other | The RAG kit's toy embedder, index and persistence (System.ML.Rag). RagHashEmbedder is a hashed ba... | ✅ |
| 938 | `rag_pipeline_test.obs` | Other | The RAG kit's prompt builder, answer and pipeline (System.ML.Rag), end to end with the toy embedd... | ✅ |
| 939 | `recovered_errors.obs` | Other | Reading the error a ?-> region recovered from (#1247; docs/RECOVERED_ERRORS_DESIGN.md). ?-> turns... | ✅ |
| 940 | `ref_parameters.obs` | Other | Reference parameters (#1243; docs/REFERENCE_PARAMETERS_DESIGN.md). function : Swap(ref a : Int, r... | ✅ |
| 941 | `reflection_signatures.obs` | Other | Regression: reflection over method signatures (#1175). - DataType->GetType() returned -1000..-994... | ✅ |
| 942 | `regex_bench.obs` | Regex | regex bench | ✅ |
| 943 | `regex_classes.obs` | Regex | Query.RegEx: character classes (several ranges, negation, punctuation, escapes, literal '-' and '... | ✅ |
| 944 | `regex_dfa_test.obs` | Regex | regex dfa test | ✅ |
| 945 | `regex_lazy_dfa.obs` | Regex | Query.RegEx: the lazily built DFA that finds where a match ends. Covers a pattern that needs more... | ✅ |
| 946 | `regex_repeat_bounds.obs` | Regex | Query.RegEx: the repeat bounds {least} and {least,most}. - a{99999999999} expanded the program at... | ✅ |
| 947 | `regex_replace_escapes.obs` | Regex | Query.RegEx: ReplaceAll matches against the ORIGINAL input (a replacement can not create or remov... | ✅ |
| 948 | `regex_threads.obs` | Regex | Query.RegEx shared by several threads: one RegEx per pattern, used at the same time by every thre... | ✅ |
| 949 | `rosetta_additive_primes.obs` | Other | Rosetta Code: Additive primes https://rosettacode.org/wiki/Additive_primes A prime whose digit su... | ✅ |
| 950 | `rosetta_align_columns.obs` | Other | Rosetta Code: Align columns https://rosettacode.org/wiki/Align_columns Claimed in #1137 Splits th... | ✅ |
| 951 | `rosetta_anonymous_recursion.obs` | Other | Rosetta Code: Anonymous recursion https://rosettacode.org/wiki/Anonymous_recursion Claimed in #11... | ✅ |
| 952 | `rosetta_append_a_record_to_the_end_of_a_text_file.obs` | Other | Rosetta Code: Append a record to the end of a text file https://rosettacode.org/wiki/Append_a_rec... | ✅ |
| 953 | `rosetta_arithmetic_complex.obs` | Other | Rosetta Code: Arithmetic/Complex https://rosettacode.org/wiki/Arithmetic/Complex Claimed in #1164... | ✅ |
| 954 | `rosetta_associative_array_merging.obs` | Other | Rosetta Code: Associative array/Merging https://rosettacode.org/wiki/Associative_array/Merging Cl... | ✅ |
| 955 | `rosetta_atomic_updates.obs` | Other | Rosetta Code: Atomic updates https://rosettacode.org/wiki/Atomic_updates EXTRA_LIBS: gen_collect... | ✅ |
| 956 | `rosetta_balanced_ternary.obs` | Other | Rosetta Code: Balanced ternary https://rosettacode.org/wiki/Balanced_ternary Digits are -1, 0 and... | ✅ |
| 957 | `rosetta_bell_numbers.obs` | Other | Rosetta Code: Bell numbers https://rosettacode.org/wiki/Bell_numbers The number of ways to partit... | ✅ |
| 958 | `rosetta_bernoulli_numbers.obs` | Other | Rosetta Code: Bernoulli numbers https://rosettacode.org/wiki/Bernoulli_numbers Claimed in #1166 E... | ✅ |
| 959 | `rosetta_brace_expansion.obs` | Other | Rosetta Code: Brace expansion https://rosettacode.org/wiki/Brace_expansion Claimed in #1143 Shell... | ✅ |
| 960 | `rosetta_camel_case_and_snake_case.obs` | Other | Rosetta Code: Camel case and snake case https://rosettacode.org/wiki/Camel_case_and_snake_case Cl... | ✅ |
| 961 | `rosetta_cartesian_product.obs` | Other | Rosetta Code: Cartesian product of two or more lists https://rosettacode.org/wiki/Cartesian_produ... | ✅ |
| 962 | `rosetta_catalan_numbers.obs` | Other | Rosetta Code: Catalan numbers https://rosettacode.org/wiki/Catalan_numbers C(n) = C(2n,n)/(n+1).... | ✅ |
| 963 | `rosetta_checkpoint_synchronization.obs` | Other | Rosetta Code: Checkpoint synchronization https://rosettacode.org/wiki/Checkpoint_synchronization... | ✅ |
| 964 | `rosetta_church_numerals.obs` | Other | Rosetta Code: Church numerals https://rosettacode.org/wiki/Church_numerals Claimed in #1122 A Chu... | ✅ |
| 965 | `rosetta_circular_primes.obs` | Other | Rosetta Code: Circular primes https://rosettacode.org/wiki/Circular_primes Every rotation of the... | ✅ |
| 966 | `rosetta_collapsible.obs` | Other | Rosetta Code: Determine if a string is collapsible https://rosettacode.org/wiki/Determine_if_a_st... | ✅ |
| 967 | `rosetta_combinations.obs` | Other | Rosetta Code: Combinations https://rosettacode.org/wiki/Combinations The k-subsets of 0..n-1 in a... | ✅ |
| 968 | `rosetta_convert_seconds_to_compound_duration.obs` | Other | Rosetta Code: Convert seconds to compound duration https://rosettacode.org/wiki/Convert_seconds_t... | ✅ |
| 969 | `rosetta_count_substring.obs` | Other | Rosetta Code: Count occurrences of a substring https://rosettacode.org/wiki/Count_occurrences_of_... | ✅ |
| 970 | `rosetta_cuban_primes.obs` | Other | Rosetta Code: Cuban primes https://rosettacode.org/wiki/Cuban_primes Primes of the form (n+1)^3 -... | ✅ |
| 971 | `rosetta_currying.obs` | Other | Rosetta Code: Currying https://rosettacode.org/wiki/Currying Claimed in #1118 A three-argument ad... | ✅ |
| 972 | `rosetta_date_manipulation.obs` | Other | Rosetta Code: Date manipulation https://rosettacode.org/wiki/Date_manipulation Claimed in #1157 "... | ✅ |
| 973 | `rosetta_day_of_the_week.obs` | Other | Rosetta Code: Day of the week https://rosettacode.org/wiki/Day_of_the_week Claimed in #1154 The y... | ✅ |
| 974 | `rosetta_deepcopy.obs` | Other | Rosetta Code: Deepcopy https://rosettacode.org/wiki/Deepcopy Claimed in #1134 EXTRA_LIBS: gen_col... | ✅ |
| 975 | `rosetta_detect_division_by_zero.obs` | Other | Rosetta Code: Detect division by zero https://rosettacode.org/wiki/Detect_division_by_zero Claime... | ✅ |
| 976 | `rosetta_digital_root.obs` | Other | Rosetta Code: Digital root https://rosettacode.org/wiki/Digital_root The additive persistence is... | ✅ |
| 977 | `rosetta_emirp_primes.obs` | Other | Rosetta Code: Emirp primes https://rosettacode.org/wiki/Emirp_primes A prime whose digit reversal... | ✅ |
| 978 | `rosetta_enforced_immutability.obs` | Other | Rosetta Code: Enforced immutability https://rosettacode.org/wiki/Enforced_immutability Claimed in... | ✅ |
| 979 | `rosetta_esthetic.obs` | Other | Rosetta Code: Esthetic numbers https://rosettacode.org/wiki/Esthetic_numbers Adjacent digits of a... | ✅ |
| 980 | `rosetta_exceptions.obs` | Other | Rosetta Code: Exceptions https://rosettacode.org/wiki/Exceptions Claimed in #1123 Define an excep... | ✅ |
| 981 | `rosetta_exceptions_catch_an_exception_thrown_in_a_nested_call.obs` | Other | Rosetta Code: Exceptions/Catch an exception thrown in a nested call https://rosettacode.org/wiki/... | ✅ |
| 982 | `rosetta_extreme_floating_point_values.obs` | Other | Rosetta Code: Extreme floating point values https://rosettacode.org/wiki/Extreme_floating_point_v... | ✅ |
| 983 | `rosetta_fast_fourier_transform.obs` | Other | Rosetta Code: Fast Fourier transform https://rosettacode.org/wiki/Fast_Fourier_transform Claimed... | ✅ |
| 984 | `rosetta_flatten_a_list.obs` | Other | Rosetta Code: Flatten a list https://rosettacode.org/wiki/Flatten_a_list Arbitrarily nested lists... | ✅ |
| 985 | `rosetta_formatted_numeric_output.obs` | Other | Rosetta Code: Formatted numeric output https://rosettacode.org/wiki/Formatted_numeric_output Clai... | ✅ |
| 986 | `rosetta_fusc_sequence.obs` | Other | Rosetta Code: Fusc sequence https://rosettacode.org/wiki/Fusc_sequence fusc(2n) = fusc(n) and fus... | ✅ |
| 987 | `rosetta_gapful.obs` | Other | Rosetta Code: Gapful numbers https://rosettacode.org/wiki/Gapful_numbers A gapful number is divis... | ✅ |
| 988 | `rosetta_gaussian_elimination.obs` | Other | Rosetta Code: Gaussian elimination https://rosettacode.org/wiki/Gaussian_elimination Claimed in #... | ✅ |
| 989 | `rosetta_generic_swap.obs` | Other | Rosetta Code: Generic swap https://rosettacode.org/wiki/Generic_swap Claimed in #1131 Objeck pass... | ✅ |
| 990 | `rosetta_gray_code.obs` | Other | Rosetta Code: Gray code https://rosettacode.org/wiki/Gray_code Encode with n XOR (n >> 1) and dec... | ✅ |
| 991 | `rosetta_hailstone.obs` | Other | Rosetta Code: Hailstone sequence https://rosettacode.org/wiki/Hailstone_sequence The sequence for... | ✅ |
| 992 | `rosetta_hash_join.obs` | Other | Rosetta Code: Hash join https://rosettacode.org/wiki/Hash_join Claimed in #1151 EXTRA_LIBS: gen_c... | ✅ |
| 993 | `rosetta_hex_dump.obs` | Other | Rosetta Code: Hex dump https://rosettacode.org/wiki/Hex_dump Claimed in #1158 hexdump -C's canoni... | ✅ |
| 994 | `rosetta_huffman_coding.obs` | Other | Rosetta Code: Huffman coding https://rosettacode.org/wiki/Huffman_coding Claimed in #1152 EXTRA_L... | ✅ |
| 995 | `rosetta_infinity.obs` | Other | Rosetta Code: Infinity https://rosettacode.org/wiki/Infinity Claimed in #1129 Float is an IEEE do... | ✅ |
| 996 | `rosetta_integer_overflow.obs` | Other | Rosetta Code: Integer overflow https://rosettacode.org/wiki/Integer_overflow Claimed in #1125 Obj... | ✅ |
| 997 | `rosetta_isqrt.obs` | Other | Rosetta Code: Isqrt (integer square root) of X https://rosettacode.org/wiki/Isqrt_(integer_square... | ✅ |
| 998 | `rosetta_jacobsthal_numbers.obs` | Other | Rosetta Code: Jacobsthal numbers https://rosettacode.org/wiki/Jacobsthal_numbers J(n) = J(n-1) +... | ✅ |
| 999 | `rosetta_kaprekar.obs` | Other | Rosetta Code: Kaprekar numbers https://rosettacode.org/wiki/Kaprekar_numbers A Kaprekar number's... | ✅ |
| 1000 | `rosetta_largest_proper_divisor.obs` | Other | Rosetta Code: Largest proper divisor of n https://rosettacode.org/wiki/Largest_proper_divisor_of_... | ✅ |
| 1001 | `rosetta_last_friday_of_each_month.obs` | Other | Rosetta Code: Last Friday of each month https://rosettacode.org/wiki/Last_Friday_of_each_month Cl... | ✅ |
| 1002 | `rosetta_leonardo_numbers.obs` | Other | Rosetta Code: Leonardo numbers https://rosettacode.org/wiki/Leonardo_numbers L(n) = L(n-1) + L(n-... | ✅ |
| 1003 | `rosetta_longest_common_subsequence.obs` | Other | Rosetta Code: Longest common subsequence https://rosettacode.org/wiki/Longest_common_subsequence... | ✅ |
| 1004 | `rosetta_look_and_say.obs` | Other | Rosetta Code: Look-and-say sequence https://rosettacode.org/wiki/Look-and-say_sequence Each term... | ✅ |
| 1005 | `rosetta_mertens.obs` | Other | Rosetta Code: Mertens function https://rosettacode.org/wiki/Mertens_function The running sum of t... | ✅ |
| 1006 | `rosetta_metered_concurrency.obs` | Other | Rosetta Code: Metered concurrency https://rosettacode.org/wiki/Metered_concurrency Claimed in #11... | ✅ |
| 1007 | `rosetta_mobius.obs` | Other | Rosetta Code: Möbius function https://rosettacode.org/wiki/M%C3%B6bius_function 0 when a square d... | ✅ |
| 1008 | `rosetta_modular_exponentiation.obs` | Other | Rosetta Code: Modular exponentiation https://rosettacode.org/wiki/Modular_exponentiation EXTRA_LI... | ✅ |
| 1009 | `rosetta_modular_inverse.obs` | Other | Rosetta Code: Modular inverse https://rosettacode.org/wiki/Modular_inverse The extended Euclidean... | ✅ |
| 1010 | `rosetta_motzkin_numbers.obs` | Other | Rosetta Code: Motzkin numbers https://rosettacode.org/wiki/Motzkin_numbers M(n) = M(n-1)*(2n+1)/(... | ✅ |
| 1011 | `rosetta_munchausen.obs` | Other | Rosetta Code: Munchausen numbers https://rosettacode.org/wiki/Munchausen_numbers A Munchausen num... | ✅ |
| 1012 | `rosetta_natural_sorting.obs` | Other | Rosetta Code: Natural sorting https://rosettacode.org/wiki/Natural_sorting Claimed in #1139 The t... | ✅ |
| 1013 | `rosetta_negative_base_numbers.obs` | Other | Rosetta Code: Negative base numbers https://rosettacode.org/wiki/Negative_base_numbers A negative... | ✅ |
| 1014 | `rosetta_optional_parameters.obs` | Other | Rosetta Code: Optional parameters https://rosettacode.org/wiki/Optional_parameters Claimed in #11... | ✅ |
| 1015 | `rosetta_partial_function_application.obs` | Other | Rosetta Code: Partial function application https://rosettacode.org/wiki/Partial_function_applicat... | ✅ |
| 1016 | `rosetta_pascals_triangle.obs` | Other | Rosetta Code: Pascal's triangle https://rosettacode.org/wiki/Pascal%27s_triangle Each entry is th... | ✅ |
| 1017 | `rosetta_pell_numbers.obs` | Other | Rosetta Code: Pell numbers https://rosettacode.org/wiki/Pell_numbers P(n) = 2*P(n-1) + P(n-2). Th... | ✅ |
| 1018 | `rosetta_permutations.obs` | Other | Rosetta Code: Permutations https://rosettacode.org/wiki/Permutations All n! orderings, in lexicog... | ✅ |
| 1019 | `rosetta_pernicious.obs` | Other | Rosetta Code: Pernicious numbers https://rosettacode.org/wiki/Pernicious_numbers A pernicious num... | ✅ |
| 1020 | `rosetta_population_count.obs` | Other | Rosetta Code: Population count https://rosettacode.org/wiki/Population_count The count of set bit... | ✅ |
| 1021 | `rosetta_power_set.obs` | Other | Rosetta Code: Power set https://rosettacode.org/wiki/Power_set All 2^n subsets, enumerated by tre... | ✅ |
| 1022 | `rosetta_priority_queue.obs` | Other | Rosetta Code: Priority queue https://rosettacode.org/wiki/Priority_queue Claimed in #1146 EXTRA_L... | ✅ |
| 1023 | `rosetta_range_expansion.obs` | Other | Rosetta Code: Range expansion https://rosettacode.org/wiki/Range_expansion Claimed in #1144 Expan... | ✅ |
| 1024 | `rosetta_read_a_specific_line_from_a_file.obs` | Other | Rosetta Code: Read a specific line from a file https://rosettacode.org/wiki/Read_a_specific_line_... | ✅ |
| 1025 | `rosetta_reflection_list_methods.obs` | Other | Rosetta Code: Reflection/List methods https://rosettacode.org/wiki/Reflection/List_methods Claime... | ✅ |
| 1026 | `rosetta_remove_lines_from_a_file.obs` | Other | Rosetta Code: Remove lines from a file https://rosettacode.org/wiki/Remove_lines_from_a_file Clai... | ✅ |
| 1027 | `rosetta_same_characters.obs` | Other | Rosetta Code: Determine if a string has all the same characters https://rosettacode.org/wiki/Dete... | ✅ |
| 1028 | `rosetta_set.obs` | Other | Rosetta Code: Set https://rosettacode.org/wiki/Set Claimed in #1148 EXTRA_LIBS: gen_collect Creat... | ✅ |
| 1029 | `rosetta_sieve.obs` | Other | Rosetta Code: Sieve of Eratosthenes https://rosettacode.org/wiki/Sieve_of_Eratosthenes The sieve... | ✅ |
| 1030 | `rosetta_square_but_not_cube.obs` | Other | Rosetta Code: Square but not cube https://rosettacode.org/wiki/Square_but_not_cube The squares th... | ✅ |
| 1031 | `rosetta_squeezable.obs` | Other | Rosetta Code: Determine if a string is squeezable https://rosettacode.org/wiki/Determine_if_a_str... | ✅ |
| 1032 | `rosetta_stern_brocot.obs` | Other | Rosetta Code: Stern-Brocot sequence https://rosettacode.org/wiki/Stern-Brocot_sequence Built by a... | ✅ |
| 1033 | `rosetta_symmetric_difference.obs` | Other | Rosetta Code: Symmetric difference https://rosettacode.org/wiki/Symmetric_difference Claimed in #... | ✅ |
| 1034 | `rosetta_tau_function.obs` | Other | Rosetta Code: Tau function https://rosettacode.org/wiki/Tau_function The count of a number's divi... | ✅ |
| 1035 | `rosetta_tokenize_a_string_with_escaping.obs` | Other | Rosetta Code: Tokenize a string with escaping https://rosettacode.org/wiki/Tokenize_a_string_with... | ✅ |
| 1036 | `rosetta_topological_sort.obs` | Other | Rosetta Code: Topological sort https://rosettacode.org/wiki/Topological_sort Claimed in #1147 EXT... | ✅ |
| 1037 | `rosetta_totient.obs` | Other | Rosetta Code: Totient function https://rosettacode.org/wiki/Totient_function Euler's totient, cou... | ✅ |
| 1038 | `rosetta_truncatable_primes.obs` | Other | Rosetta Code: Truncatable primes https://rosettacode.org/wiki/Truncatable_primes Left- and right-... | ✅ |
| 1039 | `rosetta_twos_complement.obs` | Other | Rosetta Code: Two's complement https://rosettacode.org/wiki/Two%27s_complement Claimed in #1126 T... | ✅ |
| 1040 | `rosetta_unique_characters.obs` | Other | Rosetta Code: Determine if a string has all unique characters https://rosettacode.org/wiki/Determ... | ✅ |
| 1041 | `rosetta_utf8_encode_and_decode.obs` | Other | Rosetta Code: UTF-8 encode and decode https://rosettacode.org/wiki/UTF-8_encode_and_decode Claime... | ✅ |
| 1042 | `rosetta_van_eck.obs` | Other | Rosetta Code: Van Eck sequence https://rosettacode.org/wiki/Van_Eck_sequence Each term is the gap... | ✅ |
| 1043 | `rosetta_variadic_function.obs` | Other | Rosetta Code: Variadic function https://rosettacode.org/wiki/Variadic_function Claimed in #1132 O... | ✅ |
| 1044 | `rosetta_word_wrap.obs` | Other | Rosetta Code: Word wrap https://rosettacode.org/wiki/Word_wrap Claimed in #1138 The minimum-lengt... | ✅ |
| 1045 | `rosetta_y_combinator.obs` | Other | Rosetta Code: Y combinator https://rosettacode.org/wiki/Y_combinator Claimed in #1120 The applica... | ✅ |
| 1046 | `rosetta_zeckendorf.obs` | Other | Rosetta Code: Zeckendorf number representation https://rosettacode.org/wiki/Zeckendorf_number_rep... | ✅ |
| 1047 | `rosetta_zero_to_the_zero_power.obs` | Other | Rosetta Code: Zero to the zero power https://rosettacode.org/wiki/Zero_to_the_zero_power Claimed... | ✅ |
| 1048 | `rss_fetch_failure.obs` | Other | Web.RSS: a feed that cannot be fetched leaves the reader not-OK; it does not raise. The defect: R... | ✅ |
| 1049 | `runtime_feature_test.obs` | Other | Regression tests for the "runtime.feature.*" properties, which report which optional protocol eng... | ✅ |
| 1050 | `runtime_gc_stats.obs` | Other | The runtime.* GC statistics must stay inside their own stated ranges. runtime.gc.nursery.occupanc... | ✅ |
| 1051 | `sdl2_bindings.obs` | Other | Regression test for the Game.SDL2 bindings added to close the 2026-10 gap survey: proves each new... | ✅ |
| 1052 | `sdl_game_sprite_clips.obs` | Other | Game.Framework: AnimatedImageSprite->AddClips cuts frames of the requested height. The defect: Ad... | ✅ |
| 1053 | `select_dispatch_test.obs` | Control Flow | Single-case, linear (2-5 cases), jump-table (dense >=6), and binary-tree (sparse) paths | ✅ |
| 1054 | `select_label_constants.obs` | Control Flow | The label forms a select accepts: each is a constant of the same kind as the value. The compiler... | ✅ |
| 1055 | `select_label_order.obs` | Control Flow | Select statements of 1, 2, 3, 4, 5 and 7 sparse cases compile to the same jump labels on every pl... | ✅ |
| 1056 | `select_string_expressions.obs` | Control Flow | A select on a string takes any string expression, not only a variable or a literal. The compiler... | ✅ |
| 1057 | `select_value.obs` | Control Flow | 'select' used as a value, a table of values: `x := select(subject) { label a, b: value label c: v... | ✅ |
| 1058 | `self_typeof_as_chain.obs` | Other | `@self->TypeOf(T)` and `@self->As(T)->M()` (found adding Base->ToString, #1248). EmitVariable ret... | ✅ |
| 1059 | `serial_nil_array_element.obs` | Other | A Nil element inside a serialized object array must come back as Nil in that array, and must not... | ✅ |
| 1060 | `serialize_float_array_type.obs` | Other | A Float[] field read back by the Deserializer is a Float[] to the VM. ObjectDeserializer::Deseria... | ✅ |
| 1061 | `serializer_bool_array.obs` | Other | Serializer->Write(Bool[]) and Deserializer->ReadBoolArray(). A Bool[] is a byte array, one byte p... | ✅ |
| 1062 | `serializer_multidim_arrays.obs` | Other | Regression: the Serializer dropped the tail of multi-dimensional arrays (#1174). An array's heade... | ✅ |
| 1063 | `server_bind_address.obs` | Other | A server can listen on one address instead of every interface TCPSocketServer->New(port) binds th... | ✅ |
| 1064 | `server_listen_error.obs` | Other | A server that cannot listen reports why When a server's port was already taken, Listen failed but... | ✅ |
| 1065 | `server_port_readback.obs` | Other | A server bound to port 0 reports the port the system chose The regression tests used to listen on... | ✅ |
| 1066 | `signal_dispatch.obs` | Other | Signal handlers registered with Runtime->SetSignal run on a VM thread, not inside the operating s... | ✅ |
| 1067 | `socket_accept_class.obs` | Other | An accepted socket is the kind of socket its server makes The VM caches the class ids it allocate... | ✅ |
| 1068 | `socket_accept_closed_server.obs` | Other | Accept() on a TCPSocketServer that is not open answers Nil; it used to end the process The VM's a... | ✅ |
| 1069 | `socket_count_range.obs` | Other | A socket read or write whose count does not fit the socket call is refused, not cut down and done... | ✅ |
| 1070 | `socket_graceful_close_peer_eof.obs` | Other | CloseGracefully() does not keep a peer that reads to the end waiting CloseGracefully() waited for... | ✅ |
| 1071 | `socket_graceful_close_test.obs` | Other | TCPSocket->CloseGracefully() must not lose the data it just wrote (#669). The shape this guards i... | ✅ |
| 1072 | `socket_server_stop_exit_time.obs` | Other | Stopping a server ends its accept thread, so the program leaves at once Closing a listening socke... | ✅ |
| 1073 | `sort_primitive_arrays.obs` | Other | Primitive array sorting: Int, Float, Char and Byte. Int->Sort and its Byte, Char and Float counte... | ✅ |
| 1074 | `spawn_before_after.obs` | Other | The 'spawn' example of docs/FEATURES.md, kept compiling and running: the same work written the wa... | ✅ |
| 1075 | `spawn_gc_stress.obs` | Other | 'spawn' under collection: threads started by 'spawn { ... }' read what they captured while every... | ✅ |
| 1076 | `spawn_thread_body.obs` | Other | 'spawn { ... }': a block that runs on a new thread; the expression is the started System.Concurre... | ✅ |
| 1077 | `static_files_test.obs` | Other | static files test | ✅ |
| 1078 | `stats_corr_ols.obs` | Other | System.Stats Pearson correlation (p value and Fisher interval from scipy.stats.pearsonr) and the... | ✅ |
| 1079 | `stats_degenerate.obs` | Other | System.Stats with bad input: Nil, a single value, zero variance, mismatched lengths, NaN and infi... | ✅ |
| 1080 | `stats_dist.obs` | Other | System.Stats distributions against scipy: Student t (p-value, CDF, quantile) at small and large d... | ✅ |
| 1081 | `stats_ttest.obs` | Other | System.Stats t-tests against scipy.stats.ttest_ind (equal and unequal variance), ttest_rel and tt... | ✅ |
| 1082 | `string_concat_chained_call.obs` | Strings | Regression: '+' concatenation with a chained method call appended the wrong overload. Found by th... | ✅ |
| 1083 | `string_concat_nesting.obs` | Strings | Nested string concatenation. The compiler lowers a concatenation to "allocate a System.String, st... | ✅ |
| 1084 | `string_concat_receiver_and_interp_index.obs` | Strings | Regression: a call on a parenthesised concatenation, and an indexed array in {$...}. Found by the... | ✅ |
| 1085 | `string_dollar_escape.obs` | Strings | The escape \$ in a string is a dollar sign. The scanner accepted it and the escape was not read,... | ✅ |
| 1086 | `string_edge_cases.obs` | Strings | Regression: String library edge cases (#1177), checked against Python. - Split(String) completed... | ✅ |
| 1087 | `string_find_ops.obs` | Strings | string find ops | ✅ |
| 1088 | `string_format_ops.obs` | Strings | Verifies String->Format() positional substitution. | ✅ |
| 1089 | `string_interp_concat.obs` | Strings | An interpolated string as the LEFT operand of a concatenation. "{$a}" + "{$b}" printed AAB. The c... | ✅ |
| 1090 | `string_literal_receiver_nested_call.obs` | Strings | A method call whose receiver is already on the stack when its arguments are emitted -- a string l... | ✅ |
| 1091 | `string_number_conv.obs` | Strings | string number conv | ✅ |
| 1092 | `string_replace_ops.obs` | Strings | string replace ops | ✅ |
| 1093 | `string_split_ops.obs` | Strings | string split ops | ✅ |
| 1094 | `string_wrap_center_split.obs` | Strings | String additions from #1186: SplitWhitespace, Split(delim, escape), Center, Wrap and WrapLines, a... | ✅ |
| 1095 | `stw_watchdog.obs` | Other | Fixture for the stop-the-world watchdog (OBJECK_STW_WATCHDOG_MS), driven by run_vm_flag_tests.py... | ✅ |
| 1096 | `sys_env_command_vars.obs` | Other | Runtime->CommandOutput(command, variables) runs the command with the variables set and puts the e... | ✅ |
| 1097 | `task_scope.obs` | Other | Regression for a structured-concurrency nursery (TaskScope) built purely on the existing System.C... | ✅ |
| 1098 | `tco_receiver.obs` | Other | Tail-call optimization must respect the receiver. TCO used to fire on matching class-id and metho... | ✅ |
| 1099 | `telemetry_contention.obs` | Other | System.Telemetry ContentionRunner with scripted workers: K threads, each calling a function refer... | ✅ |
| 1100 | `telemetry_profile.obs` | Other | System.Telemetry profiles: the recorder and its handles, aggregation by model, device and batch,... | ✅ |
| 1101 | `telemetry_sampler.obs` | Other | System.Telemetry resource samplers and the energy estimator, with no GPU and no network: - the nv... | ✅ |
| 1102 | `telemetry_sketch.obs` | Other | System.Telemetry LatencySketch: a quantile estimate must be within the stated relative error of t... | ✅ |
| 1103 | `telemetry_stats.obs` | Other | System.Telemetry SeriesStats and SampleRing against numbers worked out by hand: count, minimum, m... | ✅ |
| 1104 | `thread_accept_exit_test.obs` | Other | A thread parked in accept() must not take the VM down when Main returns (#681). The shape: one th... | ✅ |
| 1105 | `thread_exit_before_start.obs` | Other | Main may return while threads it has just started have not begun to run. The shape is the smalles... | ✅ |
| 1106 | `thread_fault_isolation.obs` | Other | Thread-fault isolation: an uncaught runtime error in a spawned thread ends THAT THREAD, not the p... | ✅ |
| 1107 | `thread_fault_join_many.obs` | Other | Thread-fault isolation: the fault of every ended, not yet joined thread is kept until its Join, h... | ✅ |
| 1108 | `thread_join_handles.obs` | Other | thread join handles | ✅ |
| 1109 | `thread_raise_exit.obs` | Other | Threads that caught an error end while Main returns An Error a thread raises (or the one GetLastE... | ✅ |
| 1110 | `time_strings_code_points.obs` | Other | Regression: Date strings (#1179, library half) and code points above U+FFFF (#1185). - Date->GetT... | ✅ |
| 1111 | `time_thread_safe.obs` | Other | Dates and file times read from several threads at once. Eight threads each move the same date (1... | ✅ |
| 1112 | `time_timer_wall_clock.obs` | Other | System.Time.Timer measures wall-clock time on every platform. The VM implemented the timer traps... | ✅ |
| 1113 | `tls_accept_deadline.obs` | Other | SetDeadline holds on an accepted TLS connection TCPSecureSocket->SetDeadline(ms) stores the deadl... | ✅ |
| 1114 | `tls_accept_recv_timeout.obs` | Other | A read on an accepted TLS connection does not wait for a silent peer for good A connection accept... | ✅ |
| 1115 | `tls_client_recv_timeout.obs` | Other | A TLS client socket times out on a peer that goes quiet; it does not block forever A plain TCP cl... | ✅ |
| 1116 | `tls_concurrent_connections.obs` | Other | TLS connections made at the same time, from several threads, all work A TLS server gives each con... | ✅ |
| 1117 | `tls_connect_gc_park.obs` | Other | TLS and HTTP/2 connects must park for garbage collection A stop-the-world collection waits until... | ✅ |
| 1118 | `tls_connection_cap.obs` | Other | tls connection cap | ✅ |
| 1119 | `tls_errors_and_verify.obs` | Other | TLS failures are reported, and verification cannot be switched off per socket TCPSecureSocket->Ge... | ✅ |
| 1120 | `tls_ip_san.obs` | Other | A client connecting by IP address verifies a certificate that names it A certificate names an IP... | ✅ |
| 1121 | `tls_server_cert_names.obs` | Other | TCPSecureSocketServer->GetIssuer and GetSubject report the server's own certificate The socket an... | ✅ |
| 1122 | `tls_stalled_handshake.obs` | Other | tls stalled handshake | ✅ |
| 1123 | `tls_verify_test.obs` | Other | TLS certificate verification must REFUSE. https_persistence_test proves the happy path (a pinned... | ✅ |
| 1124 | `tls_write_byte_result.obs` | Other | TCPSecureSocket->WriteByte and DTLSSocket->WriteByte say whether they wrote Both are documented "... | ✅ |
| 1125 | `trap_args_leave_stack.obs` | Other | A trap consumes the stack words its bytecode declares (#1457). A TRAP or TRAP_RTRN instruction na... | ✅ |
| 1126 | `trap_array_barrier_test.obs` | Other | A String[] returned by a VM trap must survive a collection. Every trap that returns an array of o... | ✅ |
| 1127 | `trap_array_mt_barrier_test.obs` | Other | A trap-returned array must survive ANOTHER THREAD's allocation. trap_array_barrier_test.obs cover... | ✅ |
| 1128 | `try_blocks.obs` | Exceptions | try / on / finally blocks (#1247, phases 2 and 3; docs/ERROR_HANDLING_DESIGN.md). try { ... } on(... | ✅ |
| 1129 | `try_chain_inline_compare.obs` | Exceptions | A recovered '?->' value compares the same way inline as it does through a local. 1067. The chain... | ✅ |
| 1130 | `try_otherwise.obs` | Exceptions | Try/Otherwise Error Handling Test Tests the Try() and Otherwise() intrinsic methods for error han... | ✅ |
| 1131 | `try_recovers_bounds_mid_args.obs` | Exceptions | Regression: a recovered array-bounds error corrupted the operand stack when the caller had alread... | ✅ |
| 1132 | `try_recovers_cast.obs` | Exceptions | Try() -- and '?->', which is Try() on the chain -- recovers from an invalid object cast, whether... | ✅ |
| 1133 | `try_recovers_depth.obs` | Exceptions | Try() -- and '?->', which is Try() on the chain -- recovers from a full call stack, as it does fr... | ✅ |
| 1134 | `try_recovers_jit_call.obs` | Exceptions | Try() -- and '?->', which is Try() on the chain -- recovers when a COMPILED method calls a COMPIL... | ✅ |
| 1135 | `try_recovery_stack_balance.obs` | Exceptions | Regression: `?->` recovery left the operand stack unbalanced, and `?->` after a call result or a... | ✅ |
| 1136 | `type_test_binding.obs` | Other | The binding type test: `if(value is Type name) { ... }` runs the block when the value is a `Type`... | ✅ |
| 1137 | `typeof_nil_is_false.obs` | Other | `value->TypeOf(Type)` on a Nil value is false, in the interpreter and under the JIT alike. The in... | ✅ |
| 1138 | `udp_departed_client.obs` | Other | A UDP server outlives a departed client; a client hears its peer's refusal A reply to a client th... | ✅ |
| 1139 | `udp_failover.obs` | Other | A UDP client tries each address its name resolves to UDPSocket->New used the first address a name... | ✅ |
| 1140 | `udp_host_names.obs` | Other | A UDP socket takes a host name or an IPv6 address, and a UDP server hears both UDPSocket->New(add... | ✅ |
| 1141 | `unary_minus_parenthesised.obs` | Other | Regression: unary minus on a parenthesised operand (#1099). `-(x)`, `-(F())` and `-(3 + 4)` repor... | ✅ |
| 1142 | `unsigned_literals.obs` | Other | Unsigned integer literals: the 'u'/'U' suffix, and hex/binary read as bit patterns. The suffix ch... | ✅ |
| 1143 | `unsigned_ops.obs` | Other | The '>>>' operator and the unsigned helpers on Int. Objeck stores every integer in a signed 64-bi... | ✅ |
| 1144 | `url_encode_utf8.obs` | Other | url encode utf8 | ✅ |
| 1145 | `using_block.obs` | Other | using(w := E) { ... } (docs/design/USING_BLOCK.md): w->Close() runs when the body ends and before... | ✅ |
| 1146 | `using_block_errors.obs` | Other | using(w := E) { ... } is exception safe (docs/design/USING_BLOCK.md): when an error unwinds past... | ✅ |
| 1147 | `verifier_andor_slot_closure.obs` | Other | Load-time verifier false refusal: the and/or slot reused for a closure and a literal. The compile... | ✅ |
| 1148 | `virtual_dispatch_threads.obs` | Other | Virtual calls from several threads at once resolve to the right override. A class caches the over... | ✅ |
| 1149 | `virtual_first_call_threads.obs` | Other | Threads making the first virtual calls on a class at the same moment A class keeps, per virtual d... | ✅ |
| 1150 | `vm_call_depth_limit.obs` | Other | Fixture for run_vm_flag_tests.py section 16 (--call-depth and OBJECK_CALL_DEPTH). With arguments,... | ✅ |
| 1151 | `vm_callback_depth.obs` | Other | A call back into Objeck from inside the VM's C++ runs on the thread's own call stack, bounded lik... | ✅ |
| 1152 | `vm_error_exit.obs` | Other (neg) | A program that dies inside the VM must leave obr with a non-zero exit status. Execute (core/vm/vm... | ✅ |
| 1153 | `vm_error_exit_with_threads.obs` | Other (neg) | A C++ exception that leaves the MAIN thread's interpreter while other threads are running must en... | ✅ |
| 1154 | `vm_exit_flush.obs` | Other | Output printed just before the VM ends the process must reach the reader. Driven by run_vm_flag_t... | ✅ |
| 1155 | `vm_gc_verify_inject.obs` | Other | Fixture for the heap verifier (OBJECK_GC_VERIFY, core/vm/arch/memory_verify.cpp). On its own it i... | ✅ |
| 1156 | `vm_jit_equiv.obs` | Other | The interpreter and the JIT must agree. This program is run twice by run_vm_flag_tests.py -- once... | ✅ |
| 1157 | `vm_lib_path_native.obs` | Other | Fixture for run_vm_flag_tests.py: --lib-path must reach the VM's native-library loader. SHA256 is... | ✅ |
| 1158 | `vm_locale_wide.obs` | Other | Fixture for run_vm_flag_tests.py: obr must run, and write wide characters as UTF-8, under a local... | ✅ |
| 1159 | `vm_parse_ranges.obs` | Other | A float-formatting precision that is not an int is refused where it is set, with an error a ?-> h... | ✅ |
| 1160 | `vm_set_locale_refused.obs` | Other | Runtime->SetLocale with a name the system cannot supply. The VM switched the C library's locale a... | ✅ |
| 1161 | `vm_set_property_first.obs` | Other | A program whose first property access is a set still gets the runtime's own properties. The runti... | ✅ |
| 1162 | `vm_set_property_overwrite.obs` | Other | A runtime property set twice reads back the second value. StackProgram::SetProperty stored with s... | ✅ |
| 1163 | `vm_thread_error_exit.obs` | Other (neg) | A C++ exception that escapes the interpreter on a SPAWNED thread must end the program the way one... | ✅ |
| 1164 | `vm_thread_fault_modes.obs` | Other | Fixture for run_vm_flag_tests.py (check_thread_fault_isolation): what an error nothing catches do... | ✅ |
| 1165 | `vm_thread_stack_pages.obs` | Other | Fixture for run_vm_flag_tests.py section 19: transparent huge pages stay off the stacks of the th... | ✅ |
| 1166 | `vm_trace_fn_param_format.obs` | Other (neg) | The stack trace names each method the way its source declares it, including function-typed parame... | ✅ |
| 1167 | `vm_unicode_args_env.obs` | Other | Fixture for run_vm_flag_tests.py (section 14): arguments, environment variables and command outpu... | ✅ |
| 1168 | `vm_write_char_buffer.obs` | Other | Console->WriteBuffer(Char[]) wrote the buffer twice-encoded, and ignored num. The trap (STD_OUT_C... | ✅ |
| 1169 | `web_downloader_groups.obs` | Other | WebDownloader->Download fetches every URL it is given, however many groups it needs. The download... | ✅ |
| 1170 | `web_server_test.obs` | Other | Web.Server end-to-end coverage. Every method on Web.Server.Request and Response used to call a na... | ✅ |
| 1171 | `websocket_handshake.obs` | Networking | The WebSocket client checks the reply to its opening handshake (RFC 6455 section 4.1) and the fra... | ✅ |
| 1172 | `websocket_test.obs` | Networking | websocket test | ✅ |
| 1173 | `xml_attribute_order.obs` | XML | XmlElement serializes its attributes in the order they were added, and a parsed element keeps doc... | ✅ |
| 1174 | `xml_build_ops.obs` | XML | xml build ops | ✅ |
| 1175 | `xml_encoding_ops.obs` | XML | Unit tests for the 2026-06 Data.XML improvements: truncated/garbage input is rejected (previously... | ✅ |
| 1176 | `xml_parse_ops.obs` | XML | xml parse ops | ✅ |
| 1177 | `xml_path_position.obs` | XML | Data.XML: in a FindElements path, '[n]' is the n-th match (1-based), as in XPath. The defect: the... | ✅ |
| 1178 | `xml_self_closing_root.obs` | XML | Data.XML: a document whose root element is self-closing parses, and its attributes read. The defe... | ✅ |

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
