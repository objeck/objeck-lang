# JSON, XML and regular expressions

Three libraries for reading structured text: `Data.JSON` (the `json` library), `Data.XML`
(`xml`) and `Query.RegEx` (`regex`). This page shows each in a few lines, then what they do when
the input is wrong, which changed in v2026.10.3.

```text
obc -src program.obs -lib json,xml,regex
```

The shipped examples go further: [`json_14.obs`](../../programs/deploy/json_14.obs) (nested
documents, `FindElements` paths, a streaming parser for large files),
[`xml_15.obs`](../../programs/deploy/xml_15.obs),
[`csv_16.obs`](../../programs/deploy/csv_16.obs) and
[`regex_17.obs`](../../programs/deploy/regex_17.obs).

## The basics

<!-- code: programs/examples/guides/data_basics.obs -->
```ruby
use Data.JSON;
use Data.XML;
use Query.RegEx;

class DataBasics {
  function : Main(args : String[]) ~ Nil {
    Json();
    Xml();
    Regex();
  }

  function : Json() ~ Nil {
    "-- JSON --"->PrintLine();

    # TextToElement parses text and returns the root, or Nil if it is not JSON
    root := JsonParser->TextToElement("{\"city\": \"Oslo\", \"temps\": [-3.5, 0.0, 4.25], \"snow\": true}");
    city := root->Get("city")->GetString();
    snow := root->Get("snow")->GetBool();
    temps := root->Get("temps");
    "{$city}: snow {$snow}, {$(temps->Size())} readings, first {$(temps->Get(0)->GetFloat())}"->PrintLine();

    # build a document and write it as text (object fields come out sorted)
    doc := JsonElement->New(JsonElement->JsonType->OBJECT);
    doc->Insert("name", "Ada");
    doc->Insert("age", 36);
    languages := JsonElement->New(JsonElement->JsonType->ARRAY);
    languages->Add("en");
    languages->Add("fr");
    doc->Insert("languages", languages);
    doc->ToString()->PrintLine();
  }

  function : Xml() ~ Nil {
    "-- XML --"->PrintLine();

    text := "<library><book year=\"1984\"><title>Neuromancer</title></book>";
    text += "<book year=\"1992\"><title>Snow Crash</title></book></library>";
    parser := XmlParser->New(text);
    if(<>parser->Parse()) {
      "parse error: {$(parser->GetError())}"->PrintLine();
      return;
    };

    # a path names each level from the root; every match comes back
    titles := parser->FindElements("/library/book/title");
    each(i : titles) {
      "  title: {$(titles->Get(i)->GetContent())}"->PrintLine();
    };

    # an attribute is read from its element
    books := parser->GetRoot()->GetChildren("book");
    year := books->Get(1)->GetAttribute("year")->GetValue();
    "second book is from {$year}"->PrintLine();
  }

  function : Regex() ~ Nil {
    "-- RegEx --"->PrintLine();

    # a backslash in a pattern is written twice inside an Objeck string
    pattern := RegEx->New("(\\d{4})-(\\d{2})-(\\d{2})");
    text := "released 2026-10-31, patched 2026-11-02";
    found := pattern->Find(text);
    each(i : found) {
      match := found->Get(i);
      "  {$(match->GetValue())} -> month {$(match->GetGroup(2))}"->PrintLine();
    };

    # replace every match
    digits := RegEx->New("\\d");
    hidden := digits->ReplaceAll("call 555-0199", "#");
    "  {$hidden}"->PrintLine();
  }
}
```

```text
obc -src data_basics.obs -lib json,xml,regex
obr data_basics.obe
```

```text
-- JSON --
Oslo: snow true, 3 readings, first -3.500000
{"age":36,"languages":["en","fr"],"name":"Ada"}
-- XML --
  title: Neuromancer
  title: Snow Crash
second book is from 1992
-- RegEx --
  2026-10-31 -> month 10
  2026-11-02 -> month 11
  call ###-####
```

Things to notice:

- `JsonParser->TextToElement(text)` is the short way to parse; `Get(name)` finds a field of an
  object, `Get(index)` an element of an array, and `GetString()`, `GetInt()`, `GetFloat()` and
  `GetBool()` read the value. Building is `Insert(name, value)` on an object and `Add(value)` on
  an array.
- XML paths name each level from the root, and `FindElements` returns every match.
- Inside an Objeck string a backslash is written twice: the regular expression `\d` is typed
  `"\\d"`.
- A quote character inside `{$...}` is a compile error (`A quote inside '{$...}' ends the string
  early`); compute the value into a local and interpolate the local.

## When the input is wrong

Real input is missing, empty, cut off or mistyped. In v2026.10.3 none of the cases below ends
the program; each gives an answer you can test.

<!-- code: programs/examples/guides/data_bad_input.obs -->
```ruby
use Data.JSON;
use Data.XML;
use Query.RegEx;
use System.IO.Filesystem;

class DataBadInput {
  function : Main(args : String[]) ~ Nil {
    Json();
    Xml();
    Regex();
  }

  function : Json() ~ Nil {
    "-- JSON --"->PrintLine();

    # a file that does not exist: ReadFile gives Nil
    text := FileReader->ReadFile("no_such_file.json");
    "reading a missing file gives Nil: {$(text = Nil)}"->PrintLine();

    parser := JsonParser->New(text);
    "parsing Nil succeeds: {$(parser->Parse())}"->PrintLine();

    # text that is not JSON
    root := JsonParser->TextToElement("{\"name\": \"Ada\",");
    "a document cut off halfway is Nil: {$(root = Nil)}"->PrintLine();

    # the usual pattern: test, then use
    root := JsonParser->TextToElement("{\"name\": \"Ada\", \"age\": 36}");
    if(root <> Nil & root->Has("name")) {
      name := root->Get("name")->GetString();
      "name: {$name}"->PrintLine();
    };
    has_email := root->Has("email");
    "has an 'email' field: {$has_email}"->PrintLine();
  }

  function : Xml() ~ Nil {
    "-- XML --"->PrintLine();

    # tags that do not match: Parse() is false and GetError says why
    parser := XmlParser->New("<orders><order id=\"1\"></orders>");
    "parsing mismatched tags succeeds: {$(parser->Parse())}"->PrintLine();
    "error: {$(parser->GetError())}"->PrintLine();

    # text with no XML in it at all: FindElements after the failed Parse used
    # to dereference the missing root; it is an empty vector now
    parser := XmlParser->New("this is not XML");
    parsed := parser->Parse();
    found := parser->FindElements("/orders/order");
    "parsing plain text succeeds: {$parsed}, elements found: {$(found->Size())}"->PrintLine();

    parser := XmlParser->New("<orders><order id=\"1\"/><order id=\"2\"/></orders>");
    if(parser->Parse()) {
      found := parser->FindElements("/orders/order");
      "elements found in a good document: {$(found->Size())}"->PrintLine();
    };

    nothing : String := Nil;
    parser := XmlParser->New(nothing);
    "parsing Nil succeeds: {$(parser->Parse())}"->PrintLine();
  }

  function : Regex() ~ Nil {
    "-- RegEx --"->PrintLine();

    # a repeat count is at most 1000; this one used to fill the heap
    huge := RegEx->New("a{99999999999}");
    "a{99999999999} is OK: {$(huge->IsOk())}"->PrintLine();
    matched := huge->MatchExact("aaa");
    "  and matches 'aaa': {$matched}"->PrintLine();

    backwards := RegEx->New("a{2,1}");
    "a{2,1} is OK: {$(backwards->IsOk())}"->PrintLine();

    fine := RegEx->New("a{2,4}");
    "a{2,4} is OK: {$(fine->IsOk())}"->PrintLine();
    three := fine->MatchExact("aaa");
    five := fine->MatchExact("aaaaa");
    "  matches 'aaa': {$three}, matches 'aaaaa': {$five}"->PrintLine();

    # Nil as the pattern, and Nil as the text
    nothing : String := Nil;
    none := RegEx->New(nothing);
    "a Nil pattern is OK: {$(none->IsOk())}"->PrintLine();
    word := RegEx->New("\\w+");
    "matching Nil text: {$(word->MatchExact(nothing))}"->PrintLine();
    "finding in Nil text gives Nil: {$(word->FindFirst(nothing) = Nil)}"->PrintLine();
  }
}
```

```text
obc -src data_bad_input.obs -lib json,xml,regex
obr data_bad_input.obe
```

```text
-- JSON --
reading a missing file gives Nil: true
parsing Nil succeeds: false
a document cut off halfway is Nil: true
name: Ada
has an 'email' field: false
-- XML --
parsing mismatched tags succeeds: false
error: Mismatch tag names...
parsing plain text succeeds: false, elements found: 0
elements found in a good document: 2
parsing Nil succeeds: false
-- RegEx --
a{99999999999} is OK: false
  and matches 'aaa': false
a{2,1} is OK: false
a{2,4} is OK: true
  matches 'aaa': true, matches 'aaaaa': false
a Nil pattern is OK: false
matching Nil text: false
finding in Nil text gives Nil: true
```

What each line is telling you:

- **A missing file.** `FileReader->ReadFile` returns `Nil` for a file it cannot read. A
  `JsonParser` or `XmlParser` built from `Nil` (a `String`, `Char[]` or `Byte[]`) is a parser
  over empty text: `Parse()` is `false`. Before 10.3 this ended the program in `Trim`.
- **Bad JSON.** `TextToElement` returns `Nil`. Test it, then use it; `Has(name)` tells you
  whether a field is there before you `Get` it.
- **Bad XML.** `Parse()` is `false` and `GetError()` says why. `FindElements` on text that did
  not parse is an empty vector (it dereferenced the missing root before 10.3). Note that for a
  document that is broken partway (the mismatched tags above) the parser may keep what it
  read, so check `Parse()` and do not rely on `FindElements` being empty.
- **Bad patterns.** A regular expression whose repeat count is above 1000 (`a{99999999999}` used
  to fill the heap while compiling and never returned), whose low bound is above its high bound
  (`a{2,1}`), or that expands past 100,000 instructions is not OK: `IsOk()` is `false` and it
  matches nothing. A `Nil` pattern is not OK either.
- **Nil text.** `MatchExact`, `Match`, `Find`, `FindFirst`, `ReplaceFirst` and `ReplaceAll` on a
  `Nil` string match nothing (`FindFirst` and the replace methods return `Nil`). A `Nil`
  replacement is the empty string.

Two more 10.3 fixes in the same family, for programs that use them: the JSON-RPC client sends a
fresh request id each call and rejects a reply that echoes another id, and a JSON-RPC request
without `params` (allowed by the specification) reaches the handler as an empty object instead
of ending the server.

## Where next

- [HTTP client and web server](http.md): fetch the JSON you just learned to parse.
- [Query.Structured](query.md): `from 'data.json'` reads a JSON file as a table, and the same
  works for CSV and XML.
- The full list of fixes is in the [CHANGELOG](../../CHANGELOG.md).
