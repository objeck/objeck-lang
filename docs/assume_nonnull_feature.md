# assume_nonnull Feature Documentation

## Overview

The `assume_nonnull` block statement is a new language feature for Objeck that provides syntactic sugar for code where the programmer assumes references are non-nil. This is similar to Objective-C's `NS_ASSUME_NONNULL_BEGIN/NS_ASSUME_NONNULL_END` macros.

## Motivation

Before this feature, Objeck code often required extensive nil checking:

```objeck
response := Realtime->Respond("query", "model", token);
if(response <> Nil & response->GetFirst() <> Nil & response->GetSecond() <> Nil) {
  text := response->GetFirst();
  text_size := text->Size();
  audio := response->GetSecond();
  audio_bytes := audio->Get();
  Mixer->PlayPcm(audio_bytes, 22050, AudioFormat->SDL_AUDIO_S16LSB, 1);
};
```

## Syntax

```objeck
assume_nonnull {
  // Code inside this block assumes all references are non-nil
  // The runtime will abort if a nil dereference occurs
};
```

## Usage Example

With the new feature, the code becomes cleaner:

```objeck
response := Realtime->Respond("query", "model", token);
assume_nonnull {
  # response, response->GetFirst(), response->GetSecond() assumed non-nil
  text := response->GetFirst();
  text_size := text->Size();
  
  audio := response->GetSecond();
  audio_bytes := audio->Get();
  
  Mixer->PlayPcm(audio_bytes, 22050, AudioFormat->SDL_AUDIO_S16LSB, 1);
};
```

## Behavior

- The `assume_nonnull` block is **syntactic sugar** that helps make code intent clearer
- It does NOT add runtime checks - it relies on Objeck's existing behavior
- Dereferencing a nil value anywhere (inside or outside the block) causes the runtime to abort
- The feature is useful for:
  - Documenting programmer intent
  - Reducing visual clutter from repeated nil checks
  - Making code more readable when you're confident values are non-nil

## Examples

### Basic Usage

```objeck
value := "Hello World";

assume_nonnull {
  length := value->Size();
  upper := value->ToUpper();
  "Length: {$length}, Upper: {$upper}"->PrintLine();
};
```

### With Method Calls

```objeck
result := GetSomeString();

assume_nonnull {
  trimmed := result->Trim();
  "Result: '{$trimmed}'"->PrintLine();
};
```

### With Collections

```objeck
list := Vector->New()<String>;
list->AddBack("First");
list->AddBack("Second");

assume_nonnull {
  first := list->Get(0);
  size := list->Size();
  "First: {$first}, Size: {$size}"->PrintLine();
};
```

## Implementation Details

The feature was implemented across multiple compiler stages:

1. **Scanner (scanner.h/cpp)**: Added `TOKEN_ASSUME_NONNULL_ID` keyword token
2. **Parser (parser.h/cpp)**: Added parsing logic for the block statement
3. **AST (tree.h)**: Added `AssumeNonNull` statement class
4. **Context Analyzer (context.h/cpp)**: Added semantic analysis
5. **Code Generator (intermediate.h/cpp)**: Added code emission (simply emits inner statements)

## Testing

Comprehensive tests were created in `programs/tests/test_assume_nonnull.obs` covering:
- Basic assume_nonnull blocks
- Method calls within blocks
- Collection operations within blocks

All tests pass successfully.

## Notes

- This feature does not change runtime behavior
- It's a documentation/clarity feature for developers
- Use it when you're confident values are non-nil
- The runtime will still abort on nil dereference as it always has
