# Less ceremony: six small language additions

Six additions in v2026.11.4 remove boilerplate without adding a new concept. Each one is a spelling of
code you could already write, and the compiler turns it into exactly that code, so nothing runs
differently and a program that does not use them compiles to the same bytes as before. The three
programs below were compiled and run; the output printed is what they printed.

```text
obc -src sugar_functions.obs
obr sugar_functions.obe
```

## Functions: a function by name, and a lambda that uses its object

Where a function value is wanted (`FuncRef`, `Func2Ref`, `Func3Ref`, `Func4Ref`), a static function
can be named by itself. Before, you wrote its whole signature; now the compiler reads it from the
function:

```ruby
# before
Func2Ref->New(SugarFunctions->Double(IntRef) ~ IntRef)<IntRef, IntRef>
# after
SugarFunctions->Double
```

A lambda written in an instance method may now read and write the object's `@fields` and name
`@self`. The lambda keeps the object it was made in, and a field is read and written **through that
object**, not copied: a write inside the lambda is a write to the field, and the method sees it.

<!-- code: programs/examples/guides/sugar_functions.obs -->
```ruby
#~
# compile: obc -src sugar_functions.obs
# run:     obr sugar_functions.obe
#
# Two ways to pass behavior around with less ceremony: a function named by itself
# where a FuncRef is expected, and a lambda that uses the fields of the object it
# was made in.
~#
class SugarFunctions {
	@count : Int;

	New() {
		@count := 0;
	}

	method : public : Bump() ~ IntRef {
		@count += 10;
		return IntRef->New(@count);
	}

	# a lambda in an instance method: @count is the object's field, read and written live
	method : public : Counter() ~ FuncRef<IntRef> {
		return \() => { @count += 1; return IntRef->New(@count); };
	}

	# @self names the object, so an instance method is called through it
	method : public : BumpLater() ~ FuncRef<IntRef> {
		return \() => @self->Bump();
	}

	function : Double(x : IntRef) ~ IntRef {
		return IntRef->New(x->Get() * 2);
	}

	function : Apply(f : Func2Ref<IntRef, IntRef>, n : Int) ~ Int {
		return f->Get()(IntRef->New(n))->Get();
	}

	function : Main(args : String[]) ~ Nil {
		# a function by name: no Func2Ref->New(.. ~ ..)<..> around it
		"Apply(Double, 21) = {$(Apply(SugarFunctions->Double, 21))}"->PrintLine();

		s := SugarFunctions->New();
		next := s->Counter();
		"counter: {$(next()->Get())} {$(next()->Get())}"->PrintLine();

		later := s->BumpLater();
		"bump through @self: {$(later()->Get())}"->PrintLine();
	}
}
```

```text
Apply(Double, 21) = 42
counter: 1 2
bump through @self: 12
```

`bump through @self` prints 12 because the counter lambda had already taken the field to 2, and
`Bump` adds 10. Limits: a lambda directly in an instance method (not inside another lambda, and not in
a function), an instance method is called as `@self->Bump()` and never as a bare `Bump()`, and the
function named by itself must be a single non-virtual static function over plain classes (an
overloaded name, `Int`, arrays and generics need the full signature, as before).

## Classes: accessors and constructors written by a modifier

`@x : get : T` writes the getter, `@x : get set : T` the getter and the setter. The names follow
what the libraries already do: `Get` plus the field in PascalCase (`input_tokens` is `GetInputTokens`),
`Is` plus the name for a `Bool` (a leading `is_` or `has_` is the prefix itself), and `Set` plus the
name for the setter. A doc comment before the field documents its accessors.

In a constructor, a parameter written `@name` where the class has already declared the field `@name`
fills it, so `New(@a : Int, @b : Int) {}` is the whole constructor. Defaults and named arguments work as
for any parameter. Before this, such a parameter shadowed the field and the field stayed 0.

<!-- code: programs/examples/guides/sugar_classes.obs -->
```ruby
#~
# compile: obc -src sugar_classes.obs
# run:     obr sugar_classes.obe
#
# Less boilerplate in a class: accessors written by a modifier, and constructor
# parameters that fill the fields they are named for.
~#
class Usage {
	@input_tokens : get : Int;
	@model : get set : String;
	@is_error : get set : Bool;

	New(@input_tokens : Int, @model : String, @is_error : Bool := false) {}

	function : Main(args : String[]) ~ Nil {
		u := Usage->New(120, "small");
		"{$(u->GetModel())}: {$(u->GetInputTokens())} tokens, error={$(u->IsError())}"->PrintLine();

		u->SetModel("large");
		u->SetError(true);
		"{$(u->GetModel())}: error={$(u->IsError())}"->PrintLine();

		# named arguments reach the promoted parameters
		v := Usage->New(is_error: true, model: "tiny", input_tokens: 3);
		"{$(v->GetModel())}: {$(v->GetInputTokens())} tokens, error={$(v->IsError())}"->PrintLine();
	}
}
```

```text
small: 120 tokens, error=false
large: error=true
tiny: 3 tokens, error=true
```

## Flow: collection literals and the nil guard

An array literal takes the type of the variable it is assigned to when that is a `Vector`,
`CompareVector`, `IntVector` or `FloatVector`: `v : Vector<IntRef> := [a, b]` is
`Vector->New([a, b])<IntRef>`. Numbers for a `Vector<IntRef>` are boxed (`[1, 2, 3]`). It does not type a call argument; a map has its own literal, `{key: value, ...}`.

`x := E ?? return V;` (also `?? break;` and `?? continue;`) leaves when `E` is `Nil` and otherwise
binds `x`. It is `x := E;` followed by `if(x = Nil) { return V; }`. A block form,
`x := E ?? { ...; return };`, runs a block that must end in `return`, `break`, `continue` or
`Runtime->Exit(..)`. The value form `a ?? b` is unchanged.

<!-- code: programs/examples/guides/sugar_flow.obs -->
```ruby
#~
# compile: obc -src sugar_flow.obs
# run:     obr sugar_flow.obe
#
# Collections written as literals, and a lookup that bails out when it finds nothing.
~#
use Collection;

class SugarFlow {
	function : Find(v : Vector<IntRef>, i : Int) ~ IntRef {
		if(i < v->Size()) {
			return v->Get(i);
		};
		return Nil;
	}

	function : Describe(v : Vector<IntRef>, i : Int) ~ String {
		x := Find(v, i) ?? return "no item {$i}";
		return "item {$i} is {$(x->Get())}";
	}

	function : Main(args : String[]) ~ Nil {
		# literals typed by the variable they are assigned to
		v : Vector<IntRef> := [IntRef->New(10), IntRef->New(20)];
		nums : IntVector := [4, 5, 6, 7];
		names : Vector<String> := ["ada", "grace"];
		"{$(v->Size())} refs, {$(nums->Size())} ints, first name {$(names->Get(0))}"->PrintLine();

		Describe(v, 1)->PrintLine();
		Describe(v, 5)->PrintLine();

		# the block form runs a block, which must leave
		y := Find(v, 0) ?? {
			"never printed"->PrintLine();
			return;
		};
		"found {$(y->Get())}"->PrintLine();
	}
}
```

```text
2 refs, 4 ints, first name ada
item 1 is 20
no item 5
found 10
```

## More

- [`docs/FEATURES.md`](../FEATURES.md): each feature with its rules and the test that covers it.
- [`CHANGELOG.md`](../../CHANGELOG.md): the v2026.11.4 entries.
