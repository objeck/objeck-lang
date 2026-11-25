## Retrieval-augmented generation (System.ML.Rag)

A language model answers from what it was trained on. Retrieval-augmented generation (RAG) answers from your documents instead: split the documents into chunks, embed the chunks, keep the vectors in an index, and for each question find the chunks closest to it, put them in the prompt as numbered sources, and ask the model to answer from them and cite them. The kit is glue over parts that already exist: `VectorIndex` for the vectors (see "Local vector search" above), the BERT, CLIP, Ollama, OpenAI and Gemini embedders, and the OpenAI, Anthropic, Gemini and Ollama chat clients.

Two libraries:

| Library | Holds | `-lib` |
|---|---|---|
| `rag` | the chunker, the `RagEmbedder` and `RagGenerator` interfaces, a hashing toy embedder and a stub generator for offline use, `RagIndex`, the prompt builder, `RagAnswer`, `RagPipeline` | `rag,ml_vector,gen_collect` |
| `rag_adapters` | adapters over `BertEmbedder`, the CLIP image tower, Ollama, OpenAI and Gemini embeddings, and Ollama, OpenAI, Anthropic and Gemini chat | `rag_adapters,rag,ml_vector,gen_collect,net,net_server,json,cipher,misc,openai,gemini,ollama,anthropic,models,opencv,onnx,nlp,nlp_onnx,ml_verify,ml_clip`, or `-lib @rag` for the whole set |

`rag` needs only `gen_collect` and `ml_vector`, so a program that brings its own embedder and generator, or uses the offline pair, links nothing else. Nothing in either library ends the program on bad input or a failed call: a call that cannot do what it was asked returns `Nil`, `false` or `-1`, and the object keeps the reason, read with `GetError()` (`""` when there is nothing to report).

### Quick start, offline

`RagHashEmbedder` (a hashed bag of words) and `RagStubGenerator` (it answers with the sentence of source [1] that shares the most words with the question, and cites it) need no key, no model and no network, so a whole pipeline runs anywhere:

```ruby
use System.ML.Rag;

class Quickstart {
  function : Main(args : String[]) ~ Nil {
    pipeline := RagPipeline->New(RagHashEmbedder->New(512), RagStubGenerator->New());
    pipeline->AddText("faq.txt", "Objeck is an object-oriented language. It compiles to bytecode.\n\nThe virtual machine has a generational garbage collector.");
    pipeline->AddText("net.txt", "The net library speaks HTTP and TLS.");

    answer := pipeline->Answer("What kind of garbage collector is there?");
    answer->GetText()->PrintLine();

    source := answer->GetCitedSources()->Get(0)->GetChunk();
    "from {$source->GetSource()} at offset {$source->GetOffset()}"->PrintLine();
  }
}
```

Compile with `obc -src quickstart.obs -lib rag,ml_vector,gen_collect`. It prints:

```
The virtual machine has a generational garbage collector. [1]
from faq.txt at offset 0
```

The toy embedder matches shared words, not meaning ("car" and "automobile" are unrelated to it), so it demonstrates the plumbing and is what the tests use. For real retrieval swap in an adapter (below); nothing else in the program changes.

### Chunking

`RagChunker->New(size, overlap, mode)` cuts a text into `RagChunk` pieces; each chunk has an id (`source#n`), the source name, its offset in the source text and its text. Sizes and offsets count characters, not bytes, so an accented letter is one.

| Mode (`RagSplit`) | What a chunk is |
|---|---|
| `SIZE` (the default of `RagChunker->New(size, overlap)`) | a window of `size` characters; neighbours share `overlap` characters; the last window ends at the end of the text, and there is never an extra window that the one before already holds |
| `PARAGRAPH` | whole paragraphs (split at a blank line, CRLF too) packed up to `size` characters, so a chunk never starts or ends inside one |
| `SENTENCE` | whole sentences (ended by `.`, `!` or `?` and a space or the end; `3.14` is not an end) packed up to `size` characters |

A single paragraph or sentence longer than `size` is cut by `SIZE`, with the overlap. A text that is empty or only whitespace gives no chunks, and a chunker built with a size below 1 or an overlap outside 0 to size - 1 is not valid (`IsValid()`, `GetError()`) and returns no chunks.

```ruby
use System.ML.Rag;
use Collection;

class Chunking {
  function : Show(title : String, chunks : Vector<RagChunk>) ~ Nil {
    "{$title}: {$chunks->Size()} chunks"->PrintLine();
    each(i : chunks) {
      chunk := chunks->Get(i);
      "  {$chunk->GetId()} offset {$chunk->GetOffset()} length {$chunk->GetLength()}"->PrintLine();
    };
  }

  function : Main(args : String[]) ~ Nil {
    text := "0123456789";

    # windows of 5 characters, no overlap: 0-5 and 5-10
    Show("size 5", RagChunker->New(5, 0)->Split("digits", text));

    # windows of 5 with an overlap of 2: 0-5, 3-8, 6-10
    Show("size 5, overlap 2", RagChunker->New(5, 2)->Split("digits", text));

    # whole paragraphs and sentences, packed up to 40 characters
    prose := "First sentence. Second one!\n\nA new paragraph starts here. It has two sentences.";
    Show("paragraphs", RagChunker->New(40, 0, RagSplit->PARAGRAPH)->Split("prose", prose));
    Show("sentences", RagChunker->New(40, 0, RagSplit->SENTENCE)->Split("prose", prose));
  }
}
```

```
size 5: 2 chunks
  digits#0 offset 0 length 5
  digits#1 offset 5 length 5
size 5, overlap 2: 3 chunks
  digits#0 offset 0 length 5
  digits#1 offset 3 length 5
  digits#2 offset 6 length 4
paragraphs: 3 chunks
  prose#0 offset 0 length 27
  prose#1 offset 29 length 40
  prose#2 offset 69 length 10
sentences: 3 chunks
  prose#0 offset 0 length 27
  prose#1 offset 29 length 28
  prose#2 offset 58 length 21
```

(The second paragraph is 50 characters, longer than 40, so the paragraph chunker cut it by size.) `RagChunker->Split` uses the chunker's mode; `SplitBySize`, `SplitParagraphs` and `SplitSentences` pick one whatever the mode.

### The index, and saving it

`RagIndex` is a `VectorIndex` that also remembers the chunk behind every id, so a search returns `RagMatch` objects (the chunk, its score, its rank from 1) and a whole index is one file. The three metrics are `VectorMetric`'s: `COSINE` and `DOT` score higher-is-closer, `L2` is a distance. Adding a chunk whose id is already present replaces it.

```ruby
use System.ML.Rag;
use System.ML.VectorSearch;
use Collection;

class OwnEmbedder {
  function : Main(args : String[]) ~ Nil {
    embedder := RagHashEmbedder->New(512);
    docs := ["The compiler turns source into bytecode.", "Rivers run to the sea.", "The virtual machine runs bytecode."];

    # embed the chunks yourself and keep them with where they came from
    index := RagIndex->New(embedder->GetDimension(), VectorMetric->COSINE);
    vectors := embedder->Embed(docs);
    each(i : docs) {
      row := Float->New[embedder->GetDimension()];
      each(j : row) {
        row[j] := vectors[i, j];
      };
      index->Add(RagChunk->New("doc#{$i}", "notes.txt", i * 100, docs[i]), row);
    };

    # a question is embedded the same way
    query := embedder->Embed(["which machine runs bytecode?"]);
    q := Float->New[embedder->GetDimension()];
    each(j : q) {
      q[j] := query[0, j];
    };

    found := index->Search(q, 2);
    each(i : found) {
      match := found->Get(i);
      "{$match->GetRank()}. {$match->GetChunk()->GetText()}  ({$match->GetScore()})"->PrintLine();
    };

    # one file holds the vectors and the chunks
    index->Store("notes.rix");
    loaded := RagIndex->Load("notes.rix");
    "{$loaded->Size()} chunks after loading"->PrintLine();
    System.IO.Filesystem.File->Delete("notes.rix");
  }
}
```

```
1. The virtual machine runs bytecode.  (0.866025)
2. The compiler turns source into bytecode.  (0.258199)
3 chunks after loading
```

`Store` writes a 12-byte header (the tag `ORAG`, a format byte, three zero bytes, a CRC32 of the body) and then the body through `System.IO.Serializer`: the dimension, the metric and, for every chunk, its id, source, offset, text and vector. `Load` is a function that returns `Nil` for a missing, truncated, damaged, foreign (a `VectorIndex` file, say) or newer-format file, checking the tag and the CRC32 before it decodes anything. A loaded index goes into a pipeline with `pipeline->SetIndex(index)`, after which `AddText` keeps adding to it. `RagIndex` also has `Remove(id)`, `RemoveSource(source)`, `Contains`, `GetChunk`, `GetVector`, `GetChunks` and `SetThreads` (a parallel scan of a large index, see `VectorIndex`).

### The prompt and the answer

`RagPromptBuilder` turns the question and the retrieved matches into the prompt. It has this shape (the sources are only an illustration):

```
Answer the question using only the numbered sources below. Cite the sources you use as [n], for example [1] or [2]. If the sources do not contain the answer, say that you do not know.

Sources:
[1] The virtual machine has a generational garbage collector.
[2] Objeck is an object-oriented language. It compiles to bytecode.

Question: What kind of garbage collector is there?
Answer:
```

Line breaks inside a chunk become spaces so each source stays on one line. `SetInstruction` changes the opening text and `SetMaxContext(chars)` caps the characters of source text: the closest sources are kept whole, in order, until the next would pass the cap (the first is always kept). The builder is `pipeline->GetPromptBuilder()`.

`pipeline->Answer(question)` returns a `RagAnswer`: `GetText()`, `GetSources()` (the sources the prompt numbered; source n is element n - 1), `GetPrompt()`, and `GetCitations()` / `GetCitedSources()`, which read `[n]` and `[n, m]` out of the text and return each valid number once in order of first appearance, so a program can show which passages an answer rests on and notice one that cites nothing. When nothing is retrieved (an empty index, or every match under `SetMinScore`) the generator is not called: the answer is the no-answer text (`SetNoAnswerText`) with no sources.

### Plugging in models

The two extension points are small interfaces; a class may implement either:

```ruby
use System.ML.Rag;

# an embedder of your own: any class with these three methods plugs in
class LengthEmbedder implements RagEmbedder {
  New() {
  }

  method : public : GetDimension() ~ Int {
    return 2;
  }

  method : public : GetError() ~ String {
    return "";
  }

  method : public : Embed(texts : String[]) ~ Float[,] {
    out := Float->New[texts->Size(), 2];
    each(i : texts) {
      length := texts[i]->Size();
      out[i, 0] := length->As(Float);
      out[i, 1] := 1.0;
    };
    return out;
  }
}

# and a generator of your own
class ShoutGenerator implements RagGenerator {
  New() {
  }

  method : public : GetError() ~ String {
    return "";
  }

  method : public : Generate(prompt : String) ~ String {
    return "PROMPT HAS {$prompt->Size()} CHARACTERS";
  }
}

class Plug {
  function : Main(args : String[]) ~ Nil {
    pipeline := RagPipeline->New(LengthEmbedder->New(), ShoutGenerator->New());
    pipeline->AddText("a.txt", "A short text.");
    pipeline->Answer("What is this?")->GetText()->PrintLine();
  }
}
```

```
PROMPT HAS 244 CHARACTERS
```

An embedder returns one row per text (`Nil` and a `GetError()` reason on failure), the same vector for the same text, and rows of one length throughout (`GetDimension()` may be 0 until the first call). Questions and chunks must be embedded by the same model: vectors from different models are not comparable even when their lengths match.

The adapters in `rag_adapters`:

| Class | Wraps | Notes |
|---|---|---|
| `RagBertEmbedder(BertEmbedder)` | `System.NLP.Onnx.BertEmbedder` | local sentence embeddings from an ONNX model; the model stays open and owned by the caller |
| `RagClipImageEmbedder(ClipEncoder)` | `System.ML.Clip.ClipEncoder` | the "text" is an image file path, so the index holds pictures and a search is by example image; the CLIP text tower is not part of the CLIP support, so a written query cannot find pictures |
| `RagOllamaEmbedder(model)` | `API.Ollama.Model->Embed` | a batch to one `/api/embed` request |
| `RagOpenAIEmbedder(model, key)` | `API.OpenAI.Embedding->Create` | one request per text; key `Nil` uses `EndPoint->GetApiKey()` |
| `RagGeminiEmbedder(model, key)` | `API.Google.Gemini.Model->BatchEmbedContent` | model name with its `models/` prefix |
| `RagOllamaGenerator(model)` | `API.Ollama.Completion->Generate` | `SetOptions` for temperature and the rest |
| `RagOpenAIGenerator(model, key)` | `API.OpenAI.Chat.Completion->Complete` | the prompt is one user message |
| `RagAnthropicGenerator(model, key)` | `API.Anthropic.Chat->Send` | a fresh single-turn chat per call; `SetMaxTokens` (default 1024) |
| `RagGeminiGenerator(model, key)` | `API.Google.Gemini.Model->GenerateContent` | the first candidate's text |

Hosts and keys are the clients' own: `API.Ollama.EndPoint->SetHost(...)`, `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, the Gemini key. A failed call returns `Nil` and the adapter's `GetError()` carries the client's own reason. Mixing is fine: embed locally with BERT and generate with Claude. With a local Ollama server:

```ruby
use System.ML.Rag;

class Local {
  function : Main(args : String[]) ~ Nil {
    # a local server: `ollama pull nomic-embed-text` and `ollama pull llama3.2` first
    embedder := RagOllamaEmbedder->New("nomic-embed-text");
    generator := RagOllamaGenerator->New("llama3.2");

    pipeline := RagPipeline->New(embedder, generator);
    if(pipeline->AddText("faq.txt", "Objeck is an object-oriented language.") < 0) {
      "could not index: {$pipeline->GetError()}"->PrintLine();
      return;
    };

    answer := pipeline->Answer("What is Objeck?");
    if(answer = Nil) {
      "could not answer: {$pipeline->GetError()}"->PrintLine();
      return;
    };
    answer->GetText()->PrintLine();
  }
}
```

Compile with `-lib @rag`. This one needs a running Ollama server with the two models pulled, so unlike the examples above it was compiled but not run against a model; with no server it prints `could not index: embedder: ...` and ends. The Ollama adapters are tested against a fake server on loopback (`rag_adapters_ollama_test`), which also runs a whole pipeline through them and checks that it answers exactly like the local one. The OpenAI, Gemini, Anthropic, BERT and CLIP adapters compile and are thin wrappers, but are not exercised against a live service or a model in the tests.

### The pipeline

`RagPipeline->New(embedder, generator)` splits by paragraph (500 characters, overlap 50 where a long paragraph is cut), ranks by cosine similarity and retrieves 4 chunks; `RagPipeline->New(embedder, generator, chunker, metric)` sets the chunker and metric. The index is made from the first embedding, so its dimension is whatever the embedder returns.

| Call | What it does |
|---|---|
| `AddText(source, text)` | split, embed in batches (`SetBatchSize`, 16), index; returns the number of chunks, or -1. Adding a source again first removes its old chunks, so a document is updated, not duplicated |
| `AddChunks(chunks)` | the same for chunks you cut yourself |
| `Retrieve(question)` | the top-k matches (`SetTopK`, 4), closest first; empty when nothing is indexed; `Nil` on failure |
| `Answer(question)` | retrieve, build the prompt, generate; a `RagAnswer`, or `Nil` with the failing step in `GetError()` (`embedder: ...`, `generator: ...`, `index: ...`, `chunker: ...`) |
| `SetMinScore(score)` | drop matches below a floor (cosine and dot only) so an uncovered question reaches no generator |
| `SetIndex(index)`, `GetIndex()` | carry on from a loaded index, or save the one built |
| `SetChunker`, `SetPromptBuilder`, `SetNoAnswerText` | replace the parts |

### API reference

| Class | Purpose |
|---|---|
| `RagChunk` | `GetId`, `GetSource`, `GetOffset`, `GetText`, `GetLength` |
| `RagChunker` | `New(size, overlap)`, `New(size, overlap, mode)`, `Split`, `SplitBySize`, `SplitParagraphs`, `SplitSentences`, `IsValid`, `GetError`, `GetSize`, `GetOverlap`, `GetMode` |
| `RagSplit` | enum `SIZE`, `PARAGRAPH`, `SENTENCE` |
| `RagEmbedder` | interface: `GetDimension() ~ Int`, `Embed(texts : String[]) ~ Float[,]`, `GetError() ~ String` |
| `RagGenerator` | interface: `Generate(prompt : String) ~ String`, `GetError() ~ String` |
| `RagHashEmbedder` | the toy embedder: `New()` (256 buckets), `New(dim)`, `Words(text)` |
| `RagStubGenerator` | the offline generator: `GetLastPrompt`, `GetCalls`, `SetFail` |
| `RagMatch` | `GetChunk`, `GetScore`, `GetRank` |
| `RagIndex` | `New(dim, metric)`, `Add`, `Remove`, `RemoveSource`, `Search(query, k)`, `Size`, `Contains`, `GetChunk`, `GetVector`, `GetChunks`, `SetThreads`, `Store(file)`, `function Load(file)`, `IsValid`, `GetError`, `GetDimension`, `GetMetric` |
| `RagPromptBuilder` | `Build(question, matches)`, `Select(matches)`, `SetInstruction`, `SetMaxContext` |
| `RagAnswer` | `GetQuestion`, `GetText`, `GetSources`, `GetPrompt`, `GetCitations`, `GetCitedSources` |
| `RagPipeline` | as in the table above |

### Limits and caveats

- **Retrieval is only as good as the embedder.** The hashing embedder matches words; it has no notion of synonyms, stemming or meaning. Use a BERT or hosted embedder for real questions.
- **Citations are asked for, not enforced.** The prompt asks the model to cite `[n]`; `GetCitations` reports what the text actually cites, and a model may cite nothing, cite wrongly, or answer from what it knows. The kit does not check that a claim is supported by the cited source.
- **The index is exact and in memory.** A search scans every vector (`SetThreads` helps on a large one), and an index holds its vectors and chunk texts in memory; `Load` holds the file and the decoded index at once. It is not a hosted vector store.
- **One embedder per index.** There is no check that the embedder that built a loaded index is the one asking the question, only that the dimensions agree (when the embedder knows its dimension).
- **Characters.** Chunk sizes and offsets count characters of the `String`. A character outside the Basic Multilingual Plane may count as two where strings are stored as UTF-16 (the tests use BMP text only).
- **Not thread-safe.** Use a `RagIndex`, `RagChunker` or `RagPipeline` from one thread at a time.
- **Short names.** Every class here begins with `Rag` because the compiler resolves generic arguments by short name and `Chunk` and `Document` already exist in the Gemini library.
- **Network adapters.** Replies are not streamed, and the request is sent as one prompt with no history.

### Running the offline demo and the tests

`programs/examples/rag_demo.obs` indexes three small documents, answers four questions (one the documents do not cover), saves the index, loads it into a fresh pipeline and asks again, with no key or model:

```
obc -src rag_demo.obs -lib rag,ml_vector,gen_collect
obr rag_demo.obe
obr rag_demo.obe "how do I save an index"
```

The regression tests (`programs/regression`) are `rag_chunking_test` (empty, shorter than a chunk, exact multiples, overlap, paragraphs, sentences, CRLF, accented and CJK text, bad parameters), `rag_index_test` (known nearest neighbours under the toy embedder, the index, the save and load round trip, a truncated, damaged and foreign file), `rag_pipeline_test` (exact prompt text, citations, the pipeline end to end with the stub generator, a saved index serving the same answers, failures, classes implementing the interfaces) and `rag_adapters_ollama_test` (the Ollama adapters against a fake server on loopback). None of them needs a key, a model or a network.
