# Objeck Language Features

Comprehensive guide to Objeck's language features with examples.

## Table of Contents
- [Object-Oriented Programming](#oop)
- [Functional Programming](#functional)
- [Strings & Formatting](#strings)
- [Platform Support](#platform)

---

<a name="oop"></a>
## Object-Oriented Programming

### Inheritance
```ruby
class Triangle from Shape {
  New() {
    Parent();
  }
}
```

### Interfaces
```ruby
class Triangle from Shape implements Color {
  New() {
    Parent();
  }

  method : public : GetRgb() ~ Int {
    return 0xadd8e6;
  }
}

interface Color {
  method : virtual : public : GetRgb() ~ Int;
}
```

### Type Inference
```ruby
value := "Hello World!";
value->Size()->PrintLine();
```

### Anonymous Classes
```ruby
interface Greetings {
  method : virtual : public : SayHi() ~ Nil;
}

class Hello {
  function : Main(args : String[]) ~ Nil {
    hey := Base->New() implements Greetings {
      New() {}

      method : public : SayHi() ~ Nil {
        "Hey..."->PrintLine();
      }
    };
  }
}
```

### Reflection
```ruby
klass := "Hello World!"->GetClass();
klass->GetName()->PrintLine();
klass->GetMethodNumber()->PrintLine();
```

### Dependency Injection
```ruby
value := Class->Instance("System.String")->As(String);
value += "510";
value->PrintLine();
```

### Generics
```ruby
map := Collection.Map->New()<IntRef, String>;
map->Insert(415, "San Francisco");
map->Insert(510, "Oakland");
map->Insert(408, "Sunnyvale");
map->ToString()->PrintLine();
```

#### Type-parameter bounds
A type parameter can require an interface bound (`T : Bound`); a concrete argument
must implement it. Multiple bounds combine with `&`, and a bound may be generic
(F-bounded, e.g. self-comparable):
```ruby
# single bound
class SortedBox<T : Compare> { ... }

# compound bound: T must satisfy BOTH interfaces
class Entry<T : Compare & Stringify> { ... }

# F-bounded: T must be comparable to itself
class Sorter<T : Compare<T>> { ... }
```

#### Variance (`out` / `in`)
Declaration-site variance relaxes the default invariance. A covariant parameter
(`out T`) lets `Producer<Dog>` be used where `Producer<Animal>` is expected; a
contravariant parameter (`in T`) lets `Consumer<Animal>` be used where
`Consumer<Dog>` is expected. Omitting the modifier keeps the safe, invariant
default.
```ruby
class Animal {}
class Dog from Animal { New() { Parent(); } }

class Producer<out T> { @v : T; New() {}  method : public : Get() ~ T { return @v; } }

class Test {
  function : Use(p : Producer<Animal>) ~ Nil {}
  function : Main(args : String[]) ~ Nil {
    pd := Producer->New()<Dog>;
    Use(pd);   # accepted: Producer<Dog> is a Producer<Animal>
  }
}
```

### Type Boxing
```ruby
list := Collection.List->New()<IntRef>;
list->AddBack(17);
list->AddFront(4);
(list->Back() + list->Front())->PrintLine();
```

### Static Import
```ruby
use function Int;

class Test {
  function : Main(args : String[]) ~ Nil {
    Abs(-256)->Sqrt()->PrintLine();
  }
}
```

### Serialization
```ruby
serializer := System.IO.Serializer->New();
serializer->Write(map);
serializer->Write("Fin.");
bytes := serializer->Serialize();
bytes->Size()->PrintLine();
```

---

<a name="functional"></a>
## Functional Programming

### Closures and Lambda Expressions
```ruby
funcs := Vector->New()<FuncRef<IntRef>>;
each(i : 10) {
  funcs->AddBack(FuncRef->New(\() ~ IntRef : ()
    => i->Factorial() * funcs->Size())<IntRef>);
};

each(i : funcs) {
  value := funcs->Get(i)<FuncRef>;
  func := value->Get();
  func()->Get()->PrintLine();
};
```

### First-Class Functions
```ruby
@f : static : (Int) ~ Int;
@g : static : (Int) ~ Int;

function : Main(args : String[]) ~ Nil {
  compose := Composer(F(Int) ~ Int, G(Int) ~ Int);
  compose(13)->PrintLine();
}

function : F(a : Int) ~ Int {
  return a + 14;
}

function : G(a : Int) ~ Int {
  return a + 15;
}

function : native : Compose(x : Int) ~ Int {
  return @f(@g(x));
}

function : Composer(f : (Int) ~ Int, g : (Int) ~ Int) ~ (Int) ~ Int {
  @f := f;
  @g := g;
  return Compose(Int) ~ Int;
}
```

---

<a name="strings"></a>
## Strings & Formatting

### Interpolation
Embed a value in a string literal with `{$...}`. The braces may hold any
expression — a bare variable, a field, an arithmetic / comparison / logical
expression, an array access, or a method call:
```ruby
name := "World";
i := 41;
"Hello, {$name}!"->PrintLine();             # Hello, World!
"next = {$i + 1}"->PrintLine();             # next = 42
"even? {$i % 2 = 0}"->PrintLine();          # even? false
"upper = {$name->ToUpper()}"->PrintLine();  # upper = WORLD
```

### Format specifiers
Append `:spec` to an interpolated expression for precision, width, alignment, or
radix (Python / .NET–style):

| Spec | Example | Result |
|------|---------|--------|
| `.N` — precision | `"{$pi:.2}"` | `3.14` |
| `N` — min width (right-justified) | `"[{$n:5}]"` | `[   42]` |
| `0N` — zero-pad | `"{$n:05}"` | `00042` |
| `<N` — left-justify | `"[{$s:<6}]"` | `[hi    ]` |
| `>N` — right-justify | `"[{$s:>6}]"` | `[    hi]` |
| `x` — hexadecimal | `"{$v:x}"` | `0xff` |
| `b` — binary | `"{$v:b}"` | `11111111` |
| width + precision | `"[{$pi:8.2}]"` | `[    3.14]` |

```ruby
pi := 3.14159; n := 42; v := 255;
"pi  = {$pi:.2}"->PrintLine();   # pi  = 3.14
"id  = {$n:05}"->PrintLine();    # id  = 00042
"hex = {$v:x}"->PrintLine();     # hex = 0xff
```

### String->Format
For a positional template — handy when the format string is reused or built at
runtime — use `String->Format`. Placeholders `{0}`, `{1}`, … are replaced by the
matching argument's `ToString`; `{{` and `}}` produce literal braces:
```ruby
String->Format("{0} = {1}", "x", "y")->PrintLine();      # x = y
String->Format("{1} before {0}", "a", "b")->PrintLine(); # b before a
```
String and object arguments are passed directly; primitive values are boxed with
their holder type (`IntRef`, `FloatRef`, …):
```ruby
use System;
String->Format("n = {0}", IntRef->New(42))->PrintLine(); # n = 42
```
> For mixing primitive values, string interpolation (`"{$x}"` / `"{$x:.2}"`) is
> usually more convenient than `Format`.

### Building strings manually
`String` also provides `Append`, `Pad` / `PadTo`, `Justify`, and `Truncate` for
incremental construction:
```ruby
buffer := String->New();
buffer->Append("count=");
buffer->Append(7);
buffer->ToString()->PrintLine();           # count=7
"42"->PadTo(5, '0', true)->PrintLine();     # 00042
```

---

<a name="platform"></a>
## Platform Support

### Unicode
```ruby
"Καλημέρα κόσμε"->PrintLine();
"こんにちは 世界"->PrintLine();
```

### File System
```ruby
content := System.IO.Filesystem.FileReader->ReadFile(filename);
content->Size()->PrintLine();
System.IO.Filesystem.File->Size(filename)->PrintLine();
```

### Sockets
```ruby
socket->WriteString("GET / HTTP/1.1\nHost:google.com\nUser Agent: Mozilla/5.0 (compatible)\nConnection: Close\n\n");
line := socket->ReadString();
while(line <> Nil & line->Size() > 0) {
  line->PrintLine();
  line := socket->ReadString();
};
socket->Close();
```

### Named Pipes
```ruby
pipe := System.IO.Pipe->New("foobar", Pipe->Mode->CREATE);
if(pipe->Connect()) {
  pipe->ReadLine()->PrintLine();
  pipe->WriteString("Hi Ya!");
  pipe->Close();
};
```

### Threading with Mutexes
```ruby
class CalculateThread from Thread {
  ...
  @inc_mutex : static : ThreadMutex;

  New() {
    @inc_mutex := ThreadMutex->New("inc_mutex");
  }

  method : public : Run(param : System.Base) ~ Nil {
    Compute();
  }

  method : native : Compute() ~ Nil {
    y : Int;

    while(true) {
      critical(@inc_mutex) {
        y := @current_line;
        @current_line+=1;
      };
      ...
    };
  }
}
```

### Structured Concurrency

A `TaskScope` (nursery) spawns closures as worker `Thread`s. Wrapped in a
`leaving { scope->JoinAll(); }` block it can never exit until every child has joined —
the structured-concurrency invariant (no task outlives its scope), on any exit path
(normal, early return, or unwind). Library: `System.Concurrency` (link
`-lib gen_collect,concurrent`).

```ruby
use System.Concurrency;

scope := TaskScope->New();
leaving { scope->JoinAll(); }              # joins all children on every exit path
a := scope->Spawn(FuncRef->New(\() ~ System.Base : () => { return Work(1); })<System.Base>);
b := scope->Spawn(FuncRef->New(\() ~ System.Base : () => { return Work(2); })<System.Base>);
# after the block: a->GetResult(), b->GetResult(); scope->Cancel() for boundary cancel
```

`TaskScope`: `Spawn(FuncRef<System.Base>)~Task`, `JoinAll()`, `Cancel()`, `IsCancelled()`,
`Size()`. `Task`: `GetResult()~System.Base`, `IsDone()~Bool`, `DidRun()~Bool`. Each `Task`
is a real `Thread`, so mutator register/unregister and the GC stop-the-world contract are
handled for free. Cancellation is cooperative and **boundary-only** — a lambda body can
call static functions but not instance methods, so it can't poll the scope; `Task.Run`
checks cancellation before invoking the body.

### Runtime Diagnostics

`System.Concurrency.Monitor` gives typed access to live VM/GC/thread statistics (thin
wrappers over `System.Runtime->GetProperty("runtime.*")`); all are lock-free reads that do
not affect GC behavior.

```ruby
use System.Concurrency;

Monitor->MajorCollections()->PrintLine();   # GC count
Monitor->LastPauseMicros()->PrintLine();    # last stop-the-world pause (us)
Monitor->ActiveThreads()->PrintLine();      # registered mutator threads
Monitor->MemoryOverhead()->PrintLine();     # RSS - live heap (fragmentation gauge)
```

Or read keys directly: `System.Runtime->GetProperty(key)->ToInt()`.

| key | meaning |
|---|---|
| `runtime.memory.used` / `.allocated` / `.max` / `.overhead` | process RSS / live heap / GC threshold / RSS−heap |
| `runtime.gc.minor` / `.major` / `.total` | collection counts |
| `runtime.gc.pause.last_us` / `.max_us` / `.avg_us` | stop-the-world pause times (µs) |
| `runtime.gc.nursery.used` / `.occupancy_permille` | young-generation fill |
| `runtime.gc.promoted.last` / `.total` / `runtime.gc.old.bytes` | promotion rate / old-gen size |
| `runtime.gc.remembered` / `.contention` | cross-gen writes since minor GC / collector-lock contention |
| `runtime.alloc.since_gc` | bytes allocated since the last collection |
| `runtime.threads.active` / `.parked` / `.running` | mutator threads (`.parked` includes STW/blocked) |
| `runtime.gc.stw` | 1 while a collection is stopping the world |
| `runtime.cpu.count` / `.time` / `runtime.uptime_ms` | logical cores / process CPU ms / uptime |

### Date/Time Handling
```ruby
yesterday := System.Time.Date->New();
yesterday->AddDays(-1);
yesterday->ToString()->PrintLine();
```

---

## More Information

- [API Documentation](https://www.objeck.org)
- [Code Examples](https://github.com/objeck/objeck-lang/tree/master/programs/examples)
- [Back to README](../README.md)
