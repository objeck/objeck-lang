# LSP Features Test Checklist

Open this folder as a workspace in VS Code with the Objeck LSP extension.

## Setup
1. Ensure `build.json` points to the three .obs files
2. Open `main.obs` — diagnostics should show no errors

## Test: Type Definition
- [ ] Cursor on `circle` (main.obs:38) → **Go to Type Definition** → jumps to `class Circle` in shapes.obs
- [ ] Cursor on `dog` (main.obs:43) → **Go to Type Definition** → jumps to `class Dog` in animals.obs
- [ ] Cursor on `rect` (main.obs:39) → **Go to Type Definition** → jumps to `class Rectangle` in shapes.obs

## Test: Go to Implementation
- [ ] Cursor on `Drawable` (shapes.obs:13) → **Go to Implementation** → lists Circle and Rectangle
- [ ] Cursor on `Speaker` (animals.obs:8) → **Go to Implementation** → lists Dog and Cat

## Test: Call Hierarchy
- [ ] Cursor on `ProcessShape` (main.obs:63) → **Show Call Hierarchy**
  - [ ] Incoming: Main calls ProcessShape
  - [ ] Outgoing: ProcessShape calls Describe, Draw, GetArea
- [ ] Cursor on `MakeNoise` (main.obs:73) → **Show Call Hierarchy**
  - [ ] Incoming: Main calls MakeNoise
  - [ ] Outgoing: MakeNoise calls Speak

## Test: Inlay Hints
- [ ] main.obs:38 `Circle->New("red", 5.0)` → shows `color:` and `radius:` hints
- [ ] main.obs:39 `Rectangle->New("blue", 10.0, 20.0)` → shows `color:`, `width:`, `height:`
- [ ] main.obs:43 `Dog->New("Labrador")` → shows `breed:`
- [ ] main.obs:44 `Cat->New(true)` → shows `indoor:`

## Test: Semantic Tokens
- [ ] Class names highlighted differently from variables
- [ ] Interface names highlighted differently from classes
- [ ] Method calls highlighted as methods
- [ ] `@name`, `@color` etc. highlighted as properties
- [ ] `AnimalType` enum highlighted as enum type

## Test: Document Highlight
- [ ] Click on `circle` in main.obs → all `circle` occurrences highlight
- [ ] Click on `@name` in shapes.obs → all `@name` in that file highlight

## Test: Folding Ranges
- [ ] Class bodies fold
- [ ] Method bodies fold
- [ ] Multi-line `#~ ~#` comments fold

## Test: Selection Range
- [ ] Cursor inside method body → Shift+Alt+Right expands to method → class → file

## Test: Type Hierarchy
- [ ] Right-click on `class Circle` header (shapes.obs) → **Show Type Hierarchy**
  - [ ] Supertypes: walks up to `Drawable` (or whichever interface/parent)
  - [ ] Subtypes: Circle has no subtypes — empty
- [ ] Right-click on `Drawable` → **Show Type Hierarchy**
  - [ ] Subtypes: lists Circle and Rectangle
- [ ] Cursor inside a method body of Circle → **Show Type Hierarchy**
  - [ ] Resolves to the enclosing class (Circle) and shows its supers

## Test: Document Formatting
- [ ] Mess up indentation in any file → Shift+Alt+F → fixes formatting

## Test: Existing Features (sanity check)
- [ ] Go to Definition (F12) works
- [ ] Find References (Shift+F12) works
- [ ] Hover shows type info
- [ ] Completion (Ctrl+Space) works
- [ ] Signature help (Ctrl+Shift+Space) works
- [ ] Rename (F2) works
- [ ] Document Symbols (Ctrl+Shift+O) works
- [ ] Workspace Symbols (Ctrl+T) works
