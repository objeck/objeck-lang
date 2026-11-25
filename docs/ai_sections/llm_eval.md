## LLM Evaluation

`System.LLM.Eval` measures how well a language model writes programs, and in general evaluates any model on tasks whose answer can be checked. It sends each task of a suite to the model N times, takes the code block out of the answer, compiles it with the real compiler (`obc`), runs the program under a timeout and compares what it printed with the expected output. Every attempt records whether it compiled, ran and was correct, the first compiler error (normalized into a category), and the tokens and latency when the client exposes them. The results become a compile rate, pass@k, the most common error categories and, for two models, a paired-bootstrap comparison with a plain verdict.

It is a research harness, not part of the runtime API: the source is `tools/llm_eval/llm_eval.obs` and there is no `llm_eval.obl`; compile it together with your program, as `obc -src yourprogram.obs,tools/llm_eval/llm_eval.obs -lib ml,csv,json,gen_collect` (it uses `Metrics->PairedBootstrapDifference` from `ml`). The library is pure Objeck and needs no network and no key: the tests drive it with scripted models. The adapters for OpenAI, Anthropic, Gemini and Ollama are in `tools/llm_eval`, one file per provider, because the four clients cannot be linked into one program (two of them define a `Tool` class).

### Try it without a model

A `ScriptedModel` answers from a script, so the whole path (extract, compile, run, check, aggregate) can be run without a key. The second answer below does not compile.

**Compile:** `obc -src demo.obs,tools/llm_eval/llm_eval.obs -lib ml,csv,json,gen_collect`

```ruby
use System.LLM.Eval, System.IO.Filesystem, Collection;

class Demo {
    function : Main(args : String[]) ~ Nil {
        tasks := Vector->New()<EvalTask>;
        tasks->AddBack(EvalTask->New("answer", "demo", 1, "", "exact", 10.0, "Print 42.", Nil, Nil, "42", Nil));

        model := ScriptedModel->New("scripted");
        model->Add("answer", "```objeck\nclass A {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t\"42\"->PrintLine();\n\t}\n}\n```");
        model->Add("answer", "```objeck\nclass B {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tmissing->PrintLine();\n\t}\n}\n```");

        work := "{$Runtime->GetTempDir()}{$Directory->GetSlash()}llm_eval_demo";
        sandbox := Sandbox->New(work);
        results := EvalRunner->New(model, tasks, 2, sandbox)->Run();
        "compile rate {$EvalText->Percent(results->GetCompileRate())}"->PrintLine();
        "pass@1 {$EvalText->Percent(results->GetPassAtK(1))}, pass@2 {$EvalText->Percent(results->GetPassAtK(2))}"->PrintLine();

        errors := results->GetErrorCategories();
        each(i : errors) {
            "{$errors->Get(i)->GetLabel()}: {$errors->Get(i)->GetCount()} ({$errors->Get(i)->GetExample()})"->PrintLine();
        };

        sandbox->Close();
    }
}
```

Output:

```
compile rate 50.0%
pass@1 50.0%, pass@2 100.0%
undefined_variable: 1 (Undefined variable: '_')
```

The attempts of a task are numbered from 0; a `ScriptedModel` hands its answers for a task out in turn. A `Sandbox` is the work directory: sources, executables and the watchdog script live there, and `Close()` removes it.

### The task suite

`tools/llm_eval/tasks` holds 40 tasks of graded difficulty (1 to 5), one `<id>.task` file each:

| Area | Tasks |
|------|-------|
| basics | `hello_world`, `sum_loop`, `fizzbuzz`, `string_reverse`, `vowel_count`, `stdin_sum` |
| collections | `vector_stats`, `word_count`, `sort_desc`, `set_unique`, `stack_lifo` |
| classes | `bank_account`, `shapes_inheritance`, `interface_speak`, `static_counter`, `enum_select` |
| generics | `generic_box`, `generic_max`, `generic_pair_swap` |
| lambdas | `lambda_map`, `funcref_apply`, `lambda_filter` |
| exceptions | `try_divide`, `custom_error`, `index_error` |
| file_io | `file_roundtrip`, `file_sum`, `file_delete` |
| json | `json_parse_field`, `json_build`, `json_array_sum` |
| recursion | `factorial`, `fibonacci`, `hanoi_moves` |
| quirks | `tilde_return`, `nil_handling`, `assign_vs_equal`, `int_array`, `bundle_use`, `matrix_2d` |

The quirks are what models trip on in Objeck: the `~` before a return type, `Nil`, `:=` for assignment against `=` for comparison, `Int[]` arrays, and bundles with `use`.

A task file is a header of `key: value` lines and sections introduced by a line that starts with `--- `:

```
id: stdin_sum
category: basics
difficulty: 3
libs: gen_collect,json
checker: exact
timeout: 10
--- prompt
Write an Objeck program that reads from standard input. The first line holds a count N ...
--- input
4
10
20
--- expected
55
--- reference
class StdinSum { ... }
```

| Field | Meaning |
|-------|---------|
| `id` | unique id (required) |
| `category`, `difficulty` | what the report slices by (defaults `general`, 1) |
| `libs` | the `-lib` list the answer is compiled with; the prompt tells the model which libraries are linked |
| `checker` | `exact` (default), `lines`, `numeric` or `contains` |
| `timeout` | seconds the program may run (default 10) |
| `--- prompt` | the task statement (required) |
| `--- starter` | code the answer starts from (optional) |
| `--- input` | hidden standard input for the program (optional) |
| `--- expected` | the output the program must print (required) |
| `--- reference` | a solution known to pass (optional, but every task of the suite has one) |

The checkers: `exact` compares line by line ignoring line-end style, blanks at the end of a line and blank lines at the end of the output; `lines` is the same lines in any order; `numeric` compares whitespace-separated tokens, numbers within 1e-6 of each other; `contains` wants every non-blank expected line to appear in the output.

**Every reference is checked.** The harness tests `llm_eval_references_a` and `llm_eval_references_b` (run by `python3 tools/llm_eval/run_tests.py <deploy bin dir>`, which compiles each test with `llm_eval.obs`) compile and run all 40 references with the real compiler and require the expected output, so a task that fails for every model is not a broken task. To check them by hand:

```
obc -src tools/llm_eval/llm_eval.obs,tools/llm_eval/verify_references.obs -lib ml,csv,json,gen_collect -dest verify_references.obe
obr verify_references.obe tools/llm_eval/tasks
```

### Run it against a real model

`tools/llm_eval/eval.obs` is the ready-to-run program. A provider is one adapter file compiled in with it. The key is read by the provider's client (an environment variable or a `*_api_key.dat` file in the working directory), never taken from the command line, never printed and never stored in the results.

**Compile and run (Anthropic):**

```
obc -src tools/llm_eval/llm_eval.obs,tools/llm_eval/eval.obs,tools/llm_eval/adapter_anthropic.obs -lib ml,csv,json,gen_collect,net,cipher,misc,anthropic -dest eval_anthropic.obe
export ANTHROPIC_API_KEY=...        # or create anthropic_api_key.dat
obr eval_anthropic.obe --tasks tools/llm_eval/tasks --model claude-sonnet-5-5 --n 5 --k 1,5 --out claude
```

The other providers differ in the adapter file, the `-lib` list and the key:

| Provider | Adapter | Add to `-lib` | Key |
|----------|---------|---------------|-----|
| Anthropic | `adapter_anthropic.obs` | `net,cipher,misc,anthropic` | `ANTHROPIC_API_KEY` or `anthropic_api_key.dat` |
| OpenAI | `adapter_openai.obs` | `net,net_server,cipher,misc,openai` | `OPENAI_API_KEY` or `openai_api_key.dat` |
| Gemini | `adapter_gemini.obs` | `net,net_server,cipher,misc,gemini` | `GEMINI_API_KEY` or `gemini_api_key.dat` |
| Ollama | `adapter_ollama.obs` | `net,cipher,misc,ollama` | none (a local server) |
| any command line | `adapter_command.obs` | nothing more | none |

Flags of `eval.obs` (an adapter adds `--model` and `--temperature`; the command adapter adds `--command`, `--name` and `--call-timeout`):

| Flag | Meaning |
|------|---------|
| `--tasks DIR` | task directory (default `tasks`) |
| `--n N` | attempts per task (default 1); pass@k needs n of at least k, and sampling needs a temperature above 0 to differ between attempts |
| `--k 1,5` | the k values to report pass@k for |
| `--ids a,b`, `--categories x,y` | run only these tasks |
| `--out PREFIX` | write `PREFIX.md`, `PREFIX.json` and `PREFIX_results.json` |
| `--baseline FILE` | an earlier `_results.json`; the report then compares this run (B) with it (A) |
| `--system-file FILE` | replace the system prompt, for example with a language primer |
| `--max-tokens N` | most tokens an answer may use (default 4096) |
| `--workdir DIR`, `--keep-files` | where programs are built, and keep them |
| `--list` | print the selected tasks and exit |

The run costs `tasks x n` model calls and prints the count first. Any tool with a command line can be evaluated without an adapter: the prompt (system prompt, blank line, user prompt) is fed to the command's standard input and its output is the answer:

```
obc -src tools/llm_eval/llm_eval.obs,tools/llm_eval/eval.obs,tools/llm_eval/adapter_command.obs -lib ml,csv,json,gen_collect -dest eval_command.obe
obr eval_command.obe --command "ollama run llama3.2" --name llama3.2-cli --n 3
```

### Compare two models

Run `eval.obs` for each model, then compare the two `_results.json` files with `compare.obs`:

```
obc -src tools/llm_eval/llm_eval.obs,tools/llm_eval/compare.obs -lib ml,csv,json,gen_collect -dest compare.obe
obr compare.obe claude_results.json gpt_results.json --out claude_vs_gpt
```

`ModelComparison->Of(a, b, reps, seed)` pairs attempt j of a task for model A with attempt j for model B and gives `Metrics->PairedBootstrapDifference` those pairs, each scored 1 when correct. The interval is the one of B minus A, so a positive difference means B is better. `GetVerdict()` turns it into a sentence:

```ruby
use System.LLM.Eval, Collection;

class CompareDemo {
    function : Main(args : String[]) ~ Nil {
        a := ModelResults->Load("claude_results.json");
        b := ModelResults->Load("gpt_results.json");
        comparison := ModelComparison->Of(a, b, 2000, 1);
        comparison->GetVerdict()->PrintLine();
    }
}
```

```
model-b is better than model-a: +33.3 points of pass rate, 95% interval [21.7, 46.7] excludes 0.
```

The other two verdicts are `<model> is better than <model>` the other way round, and `No significant difference: ... includes 0.` Only tasks both runs were asked, and attempts whose call worked, are paired. The pairs are attempts, not tasks: with several attempts per task the interval is a little narrower than a resampling of whole tasks would give, so ask for a clear gap before believing a small one.

### Reading the report

`EvalReport->Markdown` writes, in order:

- **Summary**: one row per model with the tasks, attempts per task, compile rate, pass@k for each requested k, the number of failed API calls, the mean latency of a call and the tokens in and out (`n/a` when the client does not say: the OpenAI and Gemini clients do not expose them).
- **Pass@1 by task category**: the mean over a category's tasks of the share of attempts that were correct.
- **Compile errors**: the categories of the first compiler error of each attempt that did not compile, with a count and an example message. The categories are `undefined_class`, `undefined_variable`, `undefined_method`, `duplicate_definition`, `type_mismatch`, `generics`, `missing_return`, `virtual_methods`, `scope`, `syntax`, `array`, `string_interpolation` and `other`. Quoted names are replaced by `'_'` so messages that differ only in a name group together.
- **Outcomes**: how many attempts ended in `pass`, `wrong_output`, `runtime_error`, `timeout`, `compile_error`, `no_code` (the answer had no code block) or `model_error`.
- **Per task**: correct out of counted attempts, a column per model.
- **Comparison**: when two models are given, the verdict, both pass rates and the number of pairs.

The compile rate is the share of answers the compiler accepted. pass@k is the unbiased estimator of Chen et al. (2021), `1 - C(n-c, k) / C(n, k)`, averaged over tasks, where n is the counted attempts of a task and c the correct ones; a task with fewer than k counted attempts is left out of pass@k. Attempts whose model call failed (`model_error`) are left out of every rate and counted in the API errors column. `PassAtK->Estimate(n, c, k)` is available on its own:

```ruby
use System.LLM.Eval;

class Estimate {
    function : Main(args : String[]) ~ Nil {
        PassAtK->Estimate(10, 3, 5)->PrintLine();   # 1 - C(7,5) / C(10,5) = 0.916667
    }
}
```

The JSON results file has one object per attempt (task, category, attempt, outcome, compiled, ran, correct, error category and line, tokens, latency, and the program and its output unless `SetKeepCode(false)`). `ModelResults->Load(path)` reads it back.

### API

| Class | Purpose |
|-------|---------|
| `EvalTask` | one task; `Parse(text)`, `BuildPrompt()`, getters for every field |
| `TaskSuite` | `Load(dir)`, `Select(tasks, ids, categories)`, `Part(tasks, part, parts)`, `GetLastError()` |
| `ModelAdapter` | the interface a model implements: `GetName()`, `Complete(request)` |
| `ModelRequest`, `ModelReply` | what a call sends (system and user prompt, task id, attempt, token limit) and returns (text, tokens, or `ModelReply->Failure(error)`) |
| `ScriptedModel`, `ReferenceModel`, `CommandModel` | a model from canned answers, from the tasks' own references, or from a command line |
| `Sandbox` | the work directory and the watchdog: `Compile`, `Run`, `Execute(command, stdin, seconds)`, `Close()` |
| `CodeExtractor` | `Best(answer)` picks the program (the longest block with `Main`), `Blocks(answer)` lists them |
| `ErrorCategory` | `FirstError(output)`, `Normalize(message)`, `Categorize(message)` |
| `OutputChecker` | `Matches(kind, expected, actual)` |
| `EvalRunner` | `New(model, tasks, per_task, sandbox)`, `Run()`, `RunAttempt(task, i)`, `SetSystemPrompt`, `SetMaxTokens`, `SetCompileTimeout`, `SetProgress`, `SetKeepCode` |
| `Attempt` | one attempt: outcome, compiled, ran, correct, error category and line, tokens, latency, code, output |
| `ModelResults` | the attempts of a model: `GetCompileRate()`, `GetPassAtK(k)`, `GetErrorCategories()`, `GetCategoryRates()`, `GetOutcomes()`, `GetTaskTallies()`, tokens, latency, `Save(path)`, `Load(path)` |
| `PassAtK` | `Estimate(n, c, k)` |
| `ModelComparison` | `Of(a, b, reps, seed)`, `GetInterval()`, `GetVerdict()`, `GetRateA()`, `GetRateB()` |
| `EvalReport` | `Markdown(results, ks, comparison, title)`, `Json(results, comparison)`, `Save(prefix, ...)` |
| `EvalArgs` | `--flag value` parsing for programs that drive the harness |
| `EvalText` | number and text helpers (`Fixed`, `Percent`, `Clip`, `Cell`) |

To evaluate a model the library has no adapter for, implement `ModelAdapter`: `Complete` must not throw and must return `ModelReply->Failure(reason)` when the call fails.

### Limits and caveats

- **Process control.** A program runs through a small watchdog script written into the work directory: a shell script on Linux and macOS, PowerShell on Windows. A program past its time is killed (on Windows with its child processes), and what it prints is capped (the file size limit on POSIX, 8 MB on Windows), so a program that prints without end cannot fill the disk. The programs are not otherwise sandboxed: they run with your permissions, can read and write files and use the network. Evaluate untrusted models in a container or a throwaway account.
- **Sequential.** Attempts run one after another; the model call is usually the slow part.
- **Stdout only.** A task is judged by the text on standard output. Tasks that write files write them in the work directory, which is the program's current directory.
- **No code block, no program.** An answer without a fenced code block is `no_code`; there is no fallback that guesses the program from prose. If an answer has several blocks, the longest one that defines `Main` is used.
- **Pairs are attempts.** The comparison's interval treats the attempts of a task as independent instances (see above).
- **Tokens.** Only the Anthropic and Ollama clients report token counts; the others show `n/a`.
- **Compile time.** The compile of each answer is a real `obc` run, about 0.1 to 0.5 s; compiling and running the 40 references takes about 6 s on Linux.
- **Provider adapters are not run in the tests.** They need a key or a server; their code is compiled but not exercised against the live services by the regression suite. The library, the checkers, the watchdog, `CommandModel` and the report are.
