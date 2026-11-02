# Checkers: a game, a thinking computer and a music box, all in Objeck

A complete checkers (English draughts) game in a few thousand lines of Objeck, built to show
three things at once: **game programming** (a 60 frames-a-second SDL2 window, input,
animation), **classic AI** (alpha-beta search, Monte Carlo tree search through the
`System.AI` library) and **learning** (the computer tunes its own evaluation by playing
itself, and its music learns habits from example phrases). Everything is drawn and played
from code: no pictures, no sound files, nothing to license.

```
cd programs/examples/checkers
obc -src checkers_engine.obs,checkers_game.obs,checkers_ai.obs,checkers_logic.obs,checkers_draw.obs,checkers_fx.obs,checkers_music.obs,checkers_audio.obs,checkers.obs -lib sdl2,sdl_game,json,ai,gen_collect -dest checkers.obe
obr checkers.obe
```

Run it from this folder so it finds `checkers_weights.json` (without it levels 9 and 10 use
the weights built into the source, which are the same numbers).

## Playing

Click a piece, then click where it goes (or use the arrow keys and Enter). Jumping is
compulsory, as in the real game: when you can jump, only the pieces that can are
highlighted. Other keys: **H** hint, **U** take a move back, **N** new game, **F** flip the
board, **S** sound on/off, **E** save the music so far as a `.mid` file, **Esc** menu,
**Q** quit. The menu picks the piece set, the level (1 to 10), your colour (or **Watch the
computer** play itself), flip, fun mode, animations, reduced motion, sound, volume and the
music style. Your choices are remembered in `.checkers.json` in your home folder.

### Rules

English / American checkers on an 8 x 8 board: men move one square diagonally forward and
capture by jumping forward over an enemy piece; a jump is **mandatory**, and a jump may
continue with the same piece (a multi-jump is one move). A man that reaches the far row is
crowned a king (a king moves and jumps in all four diagonal directions) and **its move
ends there**, even if a further jump would be possible. You lose when you have no legal
move. The game is drawn by threefold repetition, or after 40 moves each (80 plies) without
a capture or a crowning.

### Piece sets

Four sets, all drawn with SDL2 shapes: **Classic** (flat discs with a rim), **Glossy**
(gradient discs with a highlight), **Engraved** (grooved concentric rings) and **Modern**
(flat teal against amber). Every set has its own board colours and a clear crown for kings.

## The ten levels

| Level | Name | How it plays |
|---|---|---|
| 1 | Dozy Dave | any legal move (but always takes a compulsory jump) |
| 2 | Clumsy Clara | the best move one ply ahead; 30% of the time a random one |
| 3 | Wobbly Walt | 2 plies ahead; 12% random moves |
| 4 | Careful Kit | the library's `Minimax`, 3 plies |
| 5 | Sly Sam | `Minimax`, 4 plies |
| 6 | Professor Pawn | `Minimax`, 6 plies |
| 7 | Captain Crown | our alpha-beta (move ordering, jump extension), 7 plies |
| 8 | Madame Minimax | alpha-beta, 9 plies |
| 9 | Deep Thinker | iterative deepening with the **learned** evaluation, up to 10 plies / 0.4 million positions |
| 10 | Grandmaster Gizmo | the same, up to 16 plies / 2.4 million positions (a few seconds a move) |

The computer thinks on its own thread, so the window never freezes: the pieces keep
wobbling and the face keeps sweating while a deep search runs.

### Does a higher level really play better? The tournament

`checkers_tournament.obs` plays level N against level N-1 for 10 random openings, each
played twice with the colours swapped (20 games per pair, fun mode off). The openings are 4
random plies from a seeded generator, so the table below is exactly reproducible:

```
obc -src checkers_engine.obs,checkers_game.obs,checkers_ai.obs,checkers_tournament.obs -lib ai,json -dest checkers_tournament.obe
obr checkers_tournament.obe 10 2 10 checkers_weights.json 1
```

| pair | N wins | draws | N losses | N score | as Red | as White |
|---|---|---|---|---|---|---|
| L2 vs L1 | 19 | 0 | 1 | 95.0% | 10.0/10 | 9.0/10 |
| L3 vs L2 | 14 | 0 | 6 | 70.0% | 8.0/10 | 6.0/10 |
| L4 vs L3 | 17 | 1 | 2 | 87.5% | 9.0/10 | 8.5/10 |
| L5 vs L4 | 13 | 5 | 2 | 77.5% | 7.5/10 | 8.0/10 |
| L6 vs L5 | 8 | 12 | 0 | 70.0% | 7.5/10 | 6.5/10 |
| L7 vs L6 | 11 | 9 | 0 | 77.5% | 8.0/10 | 7.5/10 |
| L8 vs L7 | 9 | 10 | 1 | 70.0% | 7.5/10 | 6.5/10 |
| L9 vs L8 | (running) | | | | | |
| L10 vs L9 | (running) | | | | | |

("N score" counts a win as 1 and a draw as half. The strong levels draw a lot: checkers is
a drawish game.) Levels 4 to 6 use the library `Minimax` class over `CkState`, a
`System.AI.GameState` wrapper of the position; the same wrapper lets the library's
`MonteCarloTreeSearch` play, and the tests check that it takes a free piece.

## How it learns

The evaluation is a **weighted sum of eight facts** about a position, counted for Red minus
White: men, kings, how far the men have advanced, men still on the home row, pieces on the
eight central squares, men on the edge files, the number of simple moves available, and
kings in the middle. A positive total is good for Red. Which weights are good is not
obvious, so `checkers_train.obs` finds them by playing the game:

1. **Hill climbing against the champion.** The weights start as "just count the pieces"
   (men 100, kings 100, everything else 0). Each round makes a challenger by adding random
   noise to the champion's weights and plays it against the champion for 60 games (30
   openings, both colours, 3 plies deep). A challenger that scores more than 56% becomes
   the new champion. It is evolution in miniature: a few hundred rounds take minutes.
2. **TD(lambda) self-play** is available too (`obr checkers_train.obe 7 4000 ...`): the
   program plays itself and, after every move, nudges the weights so that a position
   predicts the positions that follow it (the idea behind TD-Gammon). On this problem it
   learned a defensive style that scored 17 to 29% against the hand-tuned weights, so the
   shipped weights come from the hill climb.

Everything is seeded, so the same command always writes the same file:

```
obc -src checkers_engine.obs,checkers_game.obs,checkers_ai.obs,checkers_train.obs -lib ai,json -dest checkers_train.obe
obr checkers_train.obe 7 0 300 checkers_weights.json 100
```

**Result of the shipped run** (seed 7, 300 rounds, about 8 minutes on a laptop-class
core): men 100, kings 137, advancement -4.7, back row -1.6, centre 0.9, edge 8.2,
mobility 2.3, king centre 17.2. Over fresh openings, at depth 4:

| Match (120 to 200 games) | Score of the first |
|---|---|
| piece-counting weights (where training starts) vs the hand-tuned weights | 30% |
| trained weights vs the hand-tuned weights | 51% (200 games) |
| trained weights vs piece counting with kings worth 1.5 men | 67% |

So starting from nothing but "count the pieces", the program learned by itself an
evaluation as good as the one I tuned by hand (and clearly better than material alone). The
hand-tuned weights are `CkEval->Hand`, used by levels 1 to 8; levels 9 and 10 use the
learned ones. Honest summary: the learned weights match, they do not beat, the hand-tuned
ones; the strength of the top levels comes mostly from search depth.

## How the music learns

The music is **generated while you play**, from formulas, as MIDI-style note events (track,
note, velocity, time) that two back ends can play: a built-in software synthesizer, or a
Standard MIDI File (format 1, drums on channel 10) that **Save song** writes to your home
folder.

* **Music style** is a table row (`CkStyle`): **Hip-hop** is boom-bap at 80 to 95 BPM with
  swung sixteenths, a kick/snare/hat pattern with ghost snares and open hats, an 808
  bass on the chord roots, minor-seventh chord stabs and a lead; **Modern beats** is trap
  at 132 to 144 BPM written half-time, a clap on the third beat, rolling hi-hats with
  occasional triplet bursts, a gliding 808 sub bass, a pad and a plucked arpeggio. **Off**
  keeps the sound effects only. A third style is one more row.
* **It follows the game.** Drums alone while the position is calm; bass and keys when a
  jump is threatened; the lead and drum fills when the position is tense; a breakdown and a
  key change up a tone when a piece is crowned; an outro for a win or a loss. Captures,
  crowning, wins, losses and the fun-mode events have synthesized sound effects too.
* **The learning part** is `CkMarkov`, a Markov chain: a table that counts "after this
  note, which note came next?". It is trained, when the game starts, on ten short melodies
  and nine chord progressions written in `CkSeeds` (my own phrases, in the source). The
  chain then walks its table with dice to invent a four-note motif that has the *habits*
  of the seeds (it likes steps and thirds, it falls back to the tonic) but is new, picks
  the chord progression the same way, and rewrites one note of the motif every bar, so the
  lead keeps changing without ever leaving the scale.
* **Where it plays.** The synthesizer queues samples to the sound card. If that cannot be
  opened the game tries SDL_mixer's MIDI player with an eight-bar file; with no audio
  device at all it runs silently and without an error message. Check
  `obr checkers.obe --demo` prints `audio engine: synthesizer | midi | silent`.

## Fun mode (off by default)

Early in a game, when you have lost more pieces than the computer, a seeded dice roll after
a computer capture (one chance in three) can bring either a **gift** (the computer "gives
back" a man on a free square in your home rows) or a **natural disaster** (lightning, an
earthquake, or a gust of wind removes one computer piece). Events are announced before they
happen, appear in the move list marked with `*`, never remove your pieces, happen at most
twice a game and only in your first 12 moves, are deterministic for the game's seed (so a
replay gets the same events), and **Undo** takes them back. The engine tests cover all of
that. The tournament and the training never use fun mode.

"Early on" can mean early in the game or at the early levels, so there is a menu setting
with both readings: **levels 1-3** (the default when fun mode is switched on) and **all
levels**. Both only act in the opening window above.

## The playful layer

Pieces squash and stretch as they hop, wobble when picked up and look at the pointer;
captures go *poof* with a comic word, combos get bigger words, a crown pops on with a hop
and sparkles, confetti falls on a win; each of the ten opponents has a face that follows the
pointer, wears its own hat, sweats while thinking and has a line for every mood. **Animations:
off** shows every move instantly with no effects; **Reduced motion** keeps the move
animation but turns off squash, particles, shaking and flashing. Both are saved in the
settings file.

## Checking it without a human

* `obr checkers.obe --screenshot out.png --scene start|play|capture|king|over|ev_zap|ev_gift|ev_quake|ev_wind --set 0-3 --level 1-10`
  renders one frame (the scene scripted) and exits. `--frame N` picks the frame.
* `obr checkers.obe --demo [dir]` lets the computer play itself for up to 25 seconds, saves
  a picture every 0.75 s to `dir`, and prints the frame time (about 16 ms with everything
  on, the 60 fps budget) and which audio engine is in use. `--silent` turns sound off.
* Clicks and keys are turned into intents by `CkController` (no graphics), so the regression
  test exercises the same code the window uses.

### The tests

`programs/regression/checkers_engine.obs` runs about 290 checks in a couple of seconds
with no window and no sound. It is generated from the files in this folder by
`make_regression_test.py` (the regression runners compile one file per test); CI fails when
it is stale. It covers:

* **perft**: the number of positions reachable in N moves from the start. The published
  English-draughts counts are 7, 49, 302, 1469, 7361, 36768, 179740 and 845931 (depths 1
  to 8); the engine reproduces them all. Fifteen more positions with kings and
  multi-jumps were counted by a separate generator (plain 8 x 8 arrays, written from the
  rules) and agree to depth 4.
* the rules: compulsory jumps, multi-jumps, a man crowned mid-jump stops, kings move both
  ways, men do not jump backwards, wins, the 40-move and repetition draws, undo;
* fun mode: fires only under its conditions, respects the cap and the window, never builds
  an illegal board, undo restores the position, deterministic for a seed;
* the players, the search (node budget, determinism), the `GameState` wrapper with the
  library `Minimax` and `MonteCarloTreeSearch`, the thinking thread;
* the window logic (clicks, forced captures, flip, keyboard), the settings file, the
  weights file;
* the music, for each style: deterministic for a seed, tempo in range, General MIDI drum
  numbers (36 kick, 38 snare, 39 clap, 42 and 46 hats), every pitched note in the scale, the
  layers following the mood, the key change, a Standard MIDI File parsed back (header,
  tracks, channel 10), identical WAV renders.

## The files

| File | What it is |
|---|---|
| `checkers_engine.obs` | bitboards, legal moves, make-move, perft (no graphics) |
| `checkers_game.obs` | a whole game: history, undo, win and draw rules, fun-mode events |
| `checkers_ai.obs` | evaluation, alpha-beta search, `GameState` wrapper, the ten levels |
| `checkers_logic.obs` | clicks and keys as intents, settings, level names, the thinking thread |
| `checkers_draw.obs` | board, piece sets, crowns, buttons, text |
| `checkers_fx.obs` | particles, pop-up words, the ten faces |
| `checkers_music.obs` | style table, Markov chains, note events, MIDI writer, synthesizer, effects |
| `checkers_audio.obs` | playing it: the sound-card queue, MIDI fallback, song export |
| `checkers.obs` | the window and the main loop |
| `checkers_train.obs`, `checkers_tournament.obs` | the learner and the level tournament |
| `checkers_tests.obs`, `make_regression_test.py` | the tests, and the generator of the regression file |
| `checkers_weights.json` | the trained weights |

## How the board is stored

A bitboard is one 64-bit number in which bit `row*8+column` says whether that square holds a
piece; checkers only uses the 32 dark squares. Red, White and "is a king" are three numbers.
A step to the upper right is a shift by 7 bits; whether a piece can jump is a lookup in a
small table of "the square two steps away"; a move is applied with a few ANDs and XORs and
taken back by restoring five numbers, so the search never allocates. That is why a search
visits about 750,000 positions a second even in this interpreted-and-JIT language.
