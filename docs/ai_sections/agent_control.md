## Agent run control (cancel, budgets, isolated tools, trace)

`API.Agent` runs a model-and-tools loop. Left alone, the loop has no way to be stopped, a tool's time limit is only reported after the tool returns, and nothing bounds a run in steps, time, tokens or money. This section adds all of that. Everything is opt-in: an agent that sets none of it behaves exactly as before, and no existing interface or constructor changed.

| You want | You use |
|---|---|
| to stop a run from another thread | `AgentCancel`, `agent->SetCancel(token)` |
| a total deadline, or a cap on steps, model calls, tool calls, tokens or cost | `AgentBudget`, `agent->SetBudget(budget)` |
| a hung tool to stop costing the run | `AgentTool->Mode->BOUNDED_THREAD` |
| to see what happened, step by step | `agent->SetTrace(sink)`, `AgentTraceBuffer` |

Compile with the same libraries as before: `-lib agent,net,json,cipher,misc`.

### Cancellation

`AgentCancel` is a thread-safe token. Any thread calls `Cancel()` (or `Cancel("reason")`); the loop checks it before every step and between tool calls, and passes it to handlers that take one. A token can be linked to a parent (`AgentCancel->New(parent)`): it counts as cancelled when its parent is, so one token can stop a group of agents.

```ruby
cancel := AgentCancel->New();
agent->SetCancel(cancel);
# from another thread, an observer, a signal handler:
cancel->Cancel("user pressed stop");
result := agent->Run("...");
result->GetStopReason()->PrintLine();   # cancelled
```

A cancelled run ends with the stop reason `cancelled`. The calls of the last reply that did not run are answered as `skipped`, so the conversation stays valid and the same agent can run again with a fresh token. A handler that wants to stop early implements `AgentCancelableHandler` and polls the token it is handed; the token also becomes cancelled when the run's deadline passes, so a cooperative tool stops for either.

```ruby
class Crawl implements AgentCancelableHandler {
  method : public : Run(arguments : JsonElement, cancel : AgentCancel) ~ String {
    while(<>cancel->IsCancelled() & HasMore()) {
      Step();
    };
    return Summary();
  }
}
tool := AgentTool->New("crawl", "Crawls", schema, Crawl->New());
```

### What a blocked model call does

A cancel is seen between model calls. To also cut a call that is blocked inside the backend there are two routes.

1. **A deadline-aware backend.** `AgentDeadlineAware` is a one-method extension of `ChatBackend` (`SetDeadlineMs`). Before every model call the loop tells such a backend how long it may take, the smaller of the budget's per-call limit and the time left to the run's deadline, and the backend returns by itself (a Nil reply with `GetLastError` set) when the time passes. The loop then reports `deadline`, not `error`. `ScriptedBackend` implements it, with `SetDelayMs` to make a call slow for tests. The four provider backends do not implement it yet: the HTTP clients have only a per-read timeout, and a total-deadline setter for them is being added separately. When it lands, each adapter implements `AgentDeadlineAware` by passing the milliseconds to that setter; until then a provider call can be interrupted only by the second route.
2. **`agent->SetBoundedModelCalls(true)`.** The call runs on a worker thread and the loop waits for it at most the budget's limit, and returns at a cancellation or the run's deadline. The call is then abandoned (see below) and its late reply is dropped. It works with any backend.

### Budgets

`AgentBudget` limits one `Run`. Every limit is off (0) until set, and the first one reached ends the run.

| Setter | Ends the run with | Checked |
|---|---|---|
| `SetMaxSteps(n)` | `budget_steps` | before a model call, when `n` replies have been had |
| `SetMaxModelCalls(n)` | `budget_model_calls` | before a model call (retries count) |
| `SetMaxToolCalls(n)` | `budget_tool_calls` | before each tool call; the call over the limit is `skipped` |
| `SetMaxTokens(n)` | `budget_tokens` | after each reply, from the tokens the backend reports |
| `SetMaxCost(x)` with `SetPrice(in, out)` or `SetPrices(table, model)` | `budget_cost` | after each reply; prices are currency per million tokens |
| `SetMaxWallMs(ms)` | `deadline` | everywhere the loop waits: before steps and tool calls, during a model call, during a tool in a worker or a child |
| `SetModelCallMs(ms)` | an `error` (or a retry) | one model call's own limit |

```ruby
prices := AgentPrices->New()->Add("my-model", 3.0, 15.0);
budget := AgentBudget->New()->SetMaxWallMs(60000)->SetMaxToolCalls(20)->SetMaxCost(0.50)->SetPrices(prices, "my-model");
agent->SetBudget(budget);
```

Budgets compose with the cancel token: whichever fires first decides the stop reason. Tokens and cost count only what the backend reports; a backend that reports none never trips them. A reply that pushes the run over its token or cost limit and asks for tools ends the run before those tools run; a final answer is kept (`completed`), because the work is done and discarding it would waste what was paid for. `SetModelRetries(n, delay_ms)` retries a failed model call; each retry is a model call for the budget, and a cancellation cuts the wait short.

### Stop reasons

`AgentResult->GetStopReason()` says why a run ended, in one vocabulary.

| Stop reason | Meaning |
|---|---|
| `completed` | the model gave a final answer |
| `cancelled` | the token was cancelled |
| `deadline` | the wall-clock budget passed |
| `budget_steps` | the step limit: the budget's, or `SetMaxSteps` (whose `GetStatus()` is still `max_steps`) |
| `budget_model_calls`, `budget_tool_calls`, `budget_tokens`, `budget_cost` | the matching budget |
| `error` | the backend failed (retries included), or the reply was cut off or filtered |

`GetStatus()` returns the same word for the new reasons, so `IsCompleted()` is true only for `completed`. `GetModelCalls()`, `GetElapsedMs()`, `GetTotalTokens()` and `GetCost()` say what the run used, and `ToJson()` carries `stop_reason`.

### Tool modes

| Mode | The handler runs | A tool over its limit, or a cancel |
|---|---|---|
| `IN_THREAD` (the default) | on the loop's thread | reported as a timeout only after the handler returns; a cancel is seen after it returns |
| `BOUNDED_THREAD` | on a worker thread | the loop stops waiting at the limit, the deadline or the cancel and abandons the worker |

Set the default with `agent->SetToolMode(mode)` and a tool's own with `tool->SetMode(mode)`. The limit is the tool's `SetTimeout` or the agent's `SetToolTimeout`.

**An abandoned worker is a leak.** A thread cannot be killed. A `BOUNDED_THREAD` handler that never returns keeps its thread, and whatever it holds, until the process exits. What the mode guarantees is that the run does not wait for it and that nothing it does later reaches the transcript: each call has its own result slot, and an abandoned worker's write to it is dropped. A handler that touches shared state still touches it; hand such a tool a token (`AgentCancelableHandler`). The model-call worker gets a copy of the conversation, so a late backend cannot read a conversation the caller has moved on with.

### Trace

`agent->SetTrace(sink)` delivers one `AgentEvent` per step of the run to an `AgentTraceSink`; an `AgentObserver` that also implements `AgentTraceSink` receives them without a separate call. The events, in order of a run:

| Event | Fields |
|---|---|
| `run_start` | backend, max_steps, tool_mode |
| `budget_check` (only with a budget) | steps, model_calls, tool_calls, tokens, cost, elapsed_ms, exhausted |
| `model_start` | attempt, limit_ms, bounded |
| `model_end` | ok, ms, input_tokens, output_tokens, tool_calls, or error |
| `model_retry` | attempt, after_error |
| `tool_start` | id, tool, mode, limit_ms, arguments |
| `tool_end` | id, tool, status, ms, chars |
| `halt` | reason (a cancel, a deadline or a budget was noticed) |
| `run_end` | stop_reason, steps, model_calls, tool_calls, tokens, cost, elapsed_ms |

Each event also has `seq` (from 1), `ms` (since the run began) and `step`. `AgentTraceBuffer` keeps them in memory (`GetEvents()`, `Kinds()`, `Count(kind)`, `First(kind)`); an `AgentEvent`'s `ToString()` is one JSON object, so a program can write its own file from a custom `AgentTraceSink`. Every event is passed through the library's key redaction before it is delivered, the same as the transcript: registered keys and key-shaped tokens become `[REDACTED]`, and a tool's arguments are cut at 500 characters.

```ruby
agent->SetTrace(AgentTraceBuffer->New());
```

### Using it

A bounded, cancellable, traced agent:

```ruby
agent := Agent->New(backend, registry);
agent->SetCancel(cancel);
agent->SetBudget(AgentBudget->New()->SetMaxWallMs(120000)->SetMaxToolCalls(30)->SetMaxTokens(200000));
agent->SetToolMode(AgentTool->Mode->BOUNDED_THREAD);
agent->SetBoundedModelCalls(true);
trace := AgentTraceBuffer->New();
agent->SetTrace(trace);
result := agent->Run(prompt);
if(<>result->IsCompleted()) {
  "stopped: {$result->GetStopReason()}: {$result->GetError()}"->PrintLine();
};
```

### Scoped limits (`AgentScope`)

`AgentScope` applies limits to every agent run inside a block of code, so a program does not have to thread a budget and a token through each agent it creates.

```ruby
scope := AgentScope->New()->SetMaxSteps(10)->SetMaxTokens(5000)->SetMaxToolCalls(20)->SetTrace(true);
AgentScope->Run(scope, Work() ~ Nil);      # Work() runs agents; they share the limits
"{$scope->GetStepsUsed()} steps, halt: {$scope->GetHalt()}"->PrintLine();
```

`programs/deploy/agent_scope_45.obs` (scripted model, no key) gives three questions a scope of two steps; the second and third are not started:

```text
scope: 2 steps allowed
question 1: completed (2 steps)
question 2: budget_steps (0 steps)
question 3: budget_steps (0 steps)
agent scope: 3 runs, 2 steps, 30 tokens, 1 tool calls, halt=budget_steps, events=10
outside the block: no scope (true)
```

- **Totals, not per run.** What a run used (steps = model replies, tokens, tool calls, time inside runs) is subtracted; the next run gets what is left. A run that finds nothing left is not started: it returns a result halted with `budget_steps`, `budget_tokens`, `budget_tool_calls` or `deadline`, and the model is not called.
- **An agent's own budget still applies**, and the smaller limit wins. A scope never loosens a limit.
- **Nesting.** A scope run inside another is limited by both; every scope in the chain is checked and charged, so an inner scope can only tighten. Its cancel token is a child of the outer one.
- **Cancel.** `scope->Cancel("reason")` from any thread stops the running agent at its next checkpoint and every later run in the scope (`cancelled`). The agent's own token is honoured too.
- **Exit.** The scope is left (and the agent's budget, token and trace restored) on a normal return, an early return and an exception, which goes on to the caller; the cleanup is a `finally` inside the library. `SetTrace(true)` collects every run's events in `scope->GetTrace()`; `SetPrintTrace(true)` prints a one-line summary on exit.
- **Threads and actors.** The scope applies on the thread that entered it and nowhere else: a new thread or actor does not inherit it. Give a worker `scope->Child()` and have it call `AgentScope->Run(child, ...)`; the child's runs are charged to the parent as well, and parallel children can together pass the parent's limit by the runs already in flight. Call `AgentScope->Init()` once at start up if several threads may enter their first scope at the same instant. A scope cannot be entered by two threads at once (`Child()` is for that).
- A model reply is checked after it arrives, so a run can pass the token limit by the size of one reply. The body is a function reference (`Name() ~ Nil`); share state with it through static fields.

The behavior is tested in `programs/regression/agent_control_test.obs` on a scripted model, with no key and no network: cancellation before the first step, between steps and calls, and during a slow model call; each budget and its stop reason; a hung tool in `BOUNDED_THREAD` with a bounded wait and an abandoned worker that finishes later without touching the transcript; the trace events and key redaction; and retries.

What is not covered: the provider backends' own deadline (pending the HTTP total-deadline setter, see above), and a fault that kills a thread outside any `try`, which still ends the whole process. There is no process-isolated tool mode yet (`AgentTool->Mode` is only `IN_THREAD` or `BOUNDED_THREAD`), so a program that must survive such a fault can run with `--thread-faults=isolate`, or put the tool behind its own process.
