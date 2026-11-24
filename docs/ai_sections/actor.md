## Actors and supervision (isolation, mailboxes, restart)

`System.Actor` is the substrate for multi-agent programs: a running agent, a tool server or a worker is an **actor**, a thread with a private **mailbox** and a user object that handles one message at a time. Actors never share state; they send each other messages. When one fails, its **supervisor** restarts it by a stated policy, and when restarts come too fast the supervisor itself fails and its own supervisor decides what happens next. This is the Erlang/OTP model on the stock `Thread`, `ThreadMutex` and `Condition`, written in Objeck with no VM change. Compile with `-lib gen_collect,concurrent,actor` (`concurrent` is needed only for `ActorScope`, `StartIn` and `TaskScope`, but the library links it). Agents as actors are a separate library, `actor_agents` (below), so `actor` itself links nothing heavy.

Every class has a distinctive name (`Mailbox`, `Mail`, `ActorBehavior`, `ChildSpec`, ...) because the compiler resolves another library's generic `Vector<X>` by its short name; a library that called its message class `Message` or `Tool` would break any program that links a client with the same name.

### The limit to know first

An uncaught VM fault on any thread ends the whole process. Supervision can therefore only recover from failures that **surface as errors the actor can catch**. The actor's receive loop wraps `Init`, every `Handle` and `Terminate` in `try`/`on(e : Error)`, so these become the exit reason `FAILED` instead of killing the program:

| What the handler does | Caught as |
|---|---|
| `Runtime->Raise(Error->New("..."))` or a subclass | `FAILED`, with the message |
| divide by zero, index out of bounds, a `Nil` dereference | `FAILED` ("Divide by zero", "Index out of bounds", "Attempting to dereference a 'Nil' memory instance") |
| a bad cast, a failed `Runtime->Assert` | `FAILED` |
| unbounded recursion (the call-depth limit, `--call-depth`) | `FAILED` ("call stack bounds have been exceeded!") |
| a failing library call that raises | `FAILED` |
| returning `false` from `Handle` | `NORMAL` |

What it **cannot** catch: memory exhaustion (the VM aborts), a crash inside a native library or the VM itself, `Runtime->Exit`, a signal, and anything that bypasses the error path. Those end the process, with every actor in it. A handler that blocks forever or loops forever is not a failure the loop can see, and **cannot be interrupted** (see "What is not guaranteed"). Treat actors as isolation of *state and errors*, not as protection from a VM fault.

### The smallest actor

```ruby
use System.Actor;

class Counter implements ActorBehavior {
  @n : Int;

  New() {
  }

  method : public : Handle(me : Actor, mail : Mail) ~ Bool {
    if(mail->GetKind() = Mail->Kind->CALL) {
      mail->Answer(IntRef->New(@n));          # answer a question
    }
    else {
      @n += mail->GetPayload()->As(IntRef)->Get();
    };
    return true;                                # false ends the actor with the reason NORMAL
  }
}

class Demo {
  function : Main(args : String[]) ~ Nil {
    counter := Actor->New("counter", Counter->New())->Start();
    counter->Send(IntRef->New(2));
    counter->Send(IntRef->New(3));
    answer := counter->Call(Nil, 1000);         # waits up to 1000 ms
    answer->GetValue()->As(IntRef)->Get()->PrintLine();   # 5
    counter->Stop();
    counter->Join()->ToString()->PrintLine();   # stopped
  }
}
```

`ActorBehavior` has one method, `Handle`. A behavior that also implements `ActorLifecycle` gets `Init` (before the first message; raising fails the actor) and `Terminate` (when the actor ends, whatever the reason). The behavior object's fields are the actor's state; only the actor's thread touches them, so they need no lock. A `Mail` has a kind: `DATA` (a payload), `CALL` (a question), `DOWN` (a watched actor ended) or `EXIT` (a linked actor ended, delivered to an actor that traps exits).

### Mailbox

`Mailbox` is a thread-safe FIFO between threads, usable on its own. Items are never `Nil`.

| | |
|---|---|
| `Mailbox->New()` | unbounded |
| `Mailbox->New(capacity, policy)` | bounded; `policy` is `BLOCK`, `DROP_OLDEST` or `REJECT` |
| `Send(item)`, `Send(item, timeout)` | returns `OK`, `DROPPED_OLDEST`, `FULL`, `TIMED_OUT`, `CLOSED` or `INVALID` |
| `SendUrgent(item)` | ignores capacity and policy (used for exit notices); fails only when closed |
| `Receive()`, `Receive(timeout)`, `TryReceive()` | `Nil` on timeout or when closed and empty (`IsDrained()` tells which) |
| `ReceiveMatching(matcher, timeout)` | selective receive: the oldest item a `MailMatcher` accepts; the others stay queued in order |
| `Close()` | later sends return `CLOSED`, blocked senders and receivers wake, queued items stay receivable |

Policies when a bounded mailbox is full: `BLOCK` makes the sender wait (up to the timeout, `-1` forever), `DROP_OLDEST` evicts the oldest queued item to make room (the send reports `DROPPED_OLDEST`), `REJECT` refuses the new item (`FULL`). Blocked senders are admitted in arrival order, and blocked receivers are served in arrival order, so no thread starves behind newer arrivals. The selective receive runs your matcher with no lock held, on a snapshot, so it may see an item more than once and it does not queue behind plain receivers.

```ruby
box := Mailbox->New(2, Mailbox->Policy->REJECT);
box->Send("a");
box->Send("b");
box->Send("c")->PrintLine();            # 2, FULL
box->Receive(100)->As(String)->PrintLine();   # a
```

### Actors, exit reasons, monitors, links

`Actor->New(name, behavior)` (or with your own `Mailbox` to bound the queue) then `Start()`. `Stop()` is graceful: the mailbox stops taking mail, the actor handles what is already queued, and ends `STOPPED`. `Kill()` is **cooperative**: queued mail is thrown away (waiting calls fail at once), and the actor ends `KILLED` as soon as its current handler returns. A handler that is blocked cannot be interrupted by either. `Join()` and `Join(timeout)` wait for the end and return the `ExitReason`, or `Nil` on timeout.

| Reason | When |
|---|---|
| `NORMAL` | `Handle` returned `false` |
| `STOPPED` | `Stop()` and the queue drained |
| `FAILED` | `Init`, `Handle` or `Terminate` raised or faulted (catchably), or a linked actor failed; `GetError()` has the message |
| `KILLED` | `Kill()` |

`IsOrderly()` is true for `NORMAL` and `STOPPED`; `IsAbnormal()` for `FAILED` and `KILLED`.

* **Monitor.** `target->Monitor(mailbox)` (or `Monitor(actor)`): when `target` ends, a `DOWN` mail naming it and the reason is put in that mailbox, past its capacity. Monitoring an actor that has ended reports at once. `Demonitor` removes it.
* **Link.** `a->Link(b)`: if either ends abnormally the other is told and fails with it, unless it called `SetTrapExits(true)`, in which case it receives an `EXIT` mail and carries on. An orderly end is not passed on.

### Request and reply

`actor->Call(payload, timeout)` queues a `CALL` mail and waits for `mail->Answer(value)`. It returns a `CallResult`: `OK` with the value, `TIMED_OUT`, `DEAD` (the actor was not taking mail, or ended before answering) or `FAILED` (the handler failed on this call). Each call has a correlation id, unique per actor (`GetId()`), and the actor may keep the `Mail` and answer later. If the caller has given up, a late `Answer` returns `false`, is dropped and is counted (`GetLateAnswers()`); it never reaches an unrelated caller. A call to an actor that fails, is killed or evicts the mail returns at once, not after the timeout. Do not call an actor from its own handler, or in a cycle: the cycle waits until the timeout.

### Supervisors

```ruby
use System.Actor;

sup := Supervisor->New("team", SupervisorStrategy->Kind->ONE_FOR_ONE, 3, 10);   # 3 restarts per 10 s
sup->AddChild(ChildSpec->New("fetcher", FetcherFactory->New(), ChildSpec->Restart->PERMANENT, 1000));
sup->AddChild(ChildSpec->New("summarizer", SummarizerFactory->New(), ChildSpec->Restart->TRANSIENT, 1000));
sup->Start();                       # children start in order; each is ready before the next starts
fetcher := sup->GetChild("fetcher");
sup->Stop();                        # children stop last-started first
sup->Join()->ToString()->PrintLine();
```

A `ChildSpec` is an id, a `BehaviorFactory` (a **new** behavior at every start, so a restart begins with clean state), a restart policy and a shutdown timeout.

| Strategy | When a child ends and is to be restarted |
|---|---|
| `ONE_FOR_ONE` | only that child restarts |
| `ONE_FOR_ALL` | the other children stop (last started first), then all restart in start order |
| `REST_FOR_ONE` | the children started after it stop (last first), then it and they restart in order |

| Restart policy | |
|---|---|
| `PERMANENT` | always restarted, even after a normal exit |
| `TRANSIENT` | restarted after `FAILED` or `KILLED`, not after `NORMAL` or `STOPPED` (the child stays listed, stopped) |
| `TEMPORARY` | never restarted; forgotten when it ends |

**Restart intensity.** `Supervisor->New(name, strategy, max_restarts, period_seconds)`: a restart that would be more than `max_restarts` within `period_seconds` fails the supervisor instead (exit reason `FAILED`, "restart intensity exceeded ..."), after it stopped its children. That is the escalation: put the supervisor under another one (`ChildSpec->New("inner", inner->AsFactory(), ...)`) and the parent restarts the whole subtree by its own policy and intensity. A child that cannot be started again (the factory returns `Nil` or `Init` raises) fails the supervisor too.

**Shutdown.** Stopping a child sends `Stop`, waits the child's shutdown timeout, then `Kill`s it and waits a short grace (100 ms). A child that still has not ended is **abandoned**: the supervisor goes on, `GetAbandonedCount()` counts it, and its thread keeps running until its handler returns.

**Dynamic children.** `StartChild(spec)` (false for a duplicate id), `TerminateChild(id)`, `Children()` (`ChildInfo`: id, running actor, restarts), `GetChild(id)`, `GetRestartCount()`. These are calls to the supervisor, so they return when it has dealt with every child ending that was queued before them, which makes them a barrier in tests.

### Registry, ActorSystem, ActorScope

* `Registry`: name to actor. `Register` is atomic (of eight threads registering one name, exactly one wins); an entry whose actor has ended counts as free. `Whereis`, `Unregister`, `Send(name, ...)`, `Call(name, ...)`, `Names()`.
* `ActorSystem`: a registry plus the actors spawned through it. `Spawn(name, behavior)` creates, registers and starts; `Shutdown(timeout)` stops them last spawned first, killing any that do not stop in time and returning how many.
* `ActorScope(scope, shutdown_ms)` ties actors to a `TaskScope` from `concurrent.obl`: `Start(actor)` runs the actor as a task of the scope, and `End()` stops them all (last started first, then a kill after the timeout) and then joins the scope. Put `End()` in a `leaving` block next to the scope so no actor outlives the block.

### Agents as actors

The library `actor_agents` (`-lib gen_collect,concurrent,actor,actor_agents,agent,net,json,cipher,misc`) wraps `API.Agent->Run` as an actor, using only agent's public API. An `AgentBuilder` makes a fresh `Agent` (backend, tools, limits) for each start, so a supervisor restart starts a new conversation. `AgentActorFactory->New(builder, fail_on_error)` is the child factory.

* A prompt is a `String` payload or an `AgentJob(prompt, reply_mailbox)`. A `Call` returns the `AgentResult` as its value; an `AgentJob` delivers it to the mailbox as a data `Mail` whose payload is the result; a plain `String` sent with `Send(prompt, sender, timeout)` is answered to the sender's mailbox.
* The conversation carries over between prompts until the actor restarts.
* With `fail_on_error`, a run that does not complete (a model error, the step limit) is delivered first and then fails the actor, so its supervisor restarts it with a fresh agent; one agent's failure does not touch the others in the team.
* An agent run is one blocking call: `Stop` waits for the run in progress and `Kill` cannot end it. Give agents a shutdown timeout that fits a model call, and set a step limit and a tool timeout on the `Agent`.

The library is blocked in the hosted playground with the agent and model-client libraries; `actor` is not (it uses only threads).

### What is not guaranteed

Read this before relying on supervision.

* **Isolation is of state and catchable errors only.** An uncaught VM fault, an out-of-memory abort, a crash in a native library, `Runtime->Exit` and signals end the whole process. Fault-injection measurements of the runtime show that a thread fault outside any `try` kills every thread; this library catches what `try` can catch and nothing else.
* **A blocked or looping handler cannot be interrupted.** `Stop` and `Kill` are requests: a handler stuck in a call that never returns keeps its thread and the actor stays alive. A supervisor then abandons it after the shutdown timeout and the kill grace, and the thread leaks until the handler returns. There is no preemption and no per-handler time limit; a handler that must be bounded has to bound itself (a timeout on its own I/O, a poll of a flag).
* **No hang detection.** A handler that is alive but stuck is not "failed" and is not restarted.
* **Delivery is in-process, at most once, and unacknowledged.** A message sent to an actor that fails before it handles the message is lost (counted in `GetDiscardedCount()`), including mail queued behind the failing one. `DROP_OLDEST` and `REJECT` mailboxes lose mail by design. Nothing is retried. Ordering is FIFO per sender-to-mailbox pair; there is no ordering between different senders, and `DOWN` and `EXIT` notices are queued behind what is already waiting (they ignore the capacity, not the order).
* **Fairness is arrival order at the mailbox, not scheduling.** Blocked senders and receivers are served in the order they started waiting; the VM's thread scheduler decides when each runs.
* **A restart loses state.** The new incarnation is built by the factory; anything the old behavior held is gone unless it was stored elsewhere. Messages queued for the failed incarnation are discarded; the mailbox is new too, so a reference to the old `Actor` is dead after a restart. Look the child up again (`GetChild`, `Registry`).
* **`Terminate` is not run when the thread never gets to run it** (the process ended, or a `Kill` hit a handler that never returned).
* **`Kill` does not run the actor's pending messages**, and it does not roll back what the interrupted handler had done.
* **A call is not a lock-free guarantee against deadlock.** Two actors that `Call` each other wait until the timeout; so does an actor that calls itself.
* **A restart intensity count is per supervisor**, kept in memory, and a window of zero seconds forgets every restart.
* **`ActorScope` and `leaving`:** a `leaving` block does not run when an error unwinds its method (use `try` and `finally`), and a task that a cancelled `TaskScope` skips never runs its actor.
* **Memory:** every actor is an operating-system thread with a stack; thousands of actors are thousands of threads. There is no work-stealing scheduler.
* **Timeouts use the VM's steady clock** (milliseconds since the program started) and are lower bounds: a wait may last longer under load, never shorter.

### API summary

| Class | What it is |
|---|---|
| `Mailbox` | bounded or unbounded queue; `Send`, `SendUrgent`, `Receive`, `TryReceive`, `ReceiveMatching`, `Close`, counters |
| `MailMatcher` | interface for a selective receive |
| `Mail` | the envelope: `GetKind`, `GetPayload`, `GetSender`, `GetSource`, `GetReason`, `Answer`, `Fail` |
| `ActorBehavior`, `ActorLifecycle` | what an actor does (`Handle`), and optional `Init`/`Terminate` |
| `Actor` | `Start`, `StartIn`, `Send`, `Call`, `Stop`, `Kill`, `Join`, `Monitor`, `Link`, `SetTrapExits`, counters |
| `ExitReason` | `NORMAL`, `STOPPED`, `FAILED`, `KILLED`; `GetError`, `IsOrderly`, `IsAbnormal` |
| `CallResult` | `OK`, `TIMED_OUT`, `DEAD`, `FAILED`; `GetValue`, `GetError`, `GetId` |
| `Supervisor`, `SupervisorBehavior`, `SupervisorFactory`, `SupervisorStrategy` | supervision |
| `ChildSpec`, `ChildInfo`, `BehaviorFactory` | children |
| `Registry`, `ActorSystem`, `ActorScope` | naming, grouping, structured-concurrency tie |
| `AgentActorBehavior`, `AgentActorFactory`, `AgentBuilder`, `AgentJob` | in `actor_agents` |

Tests: `actor_mailbox_test`, `actor_mailbox_stress_test` (eight producers, one consumer, GC-safety of messages across threads), `actor_core_test`, `actor_supervisor_test`, `actor_agents_test`, all deterministic (scripted handlers, latches, no sleeps for correctness). Example: `programs/examples/actor_supervision_demo.obs`.
