/***************************************************************************
 * Unit test for the control-flow graph (cfg.h): hand-built instruction lists.
 *
 * Build and run (from core/compiler, after `make` has built the objects):
 *   make cfg_test && ./cfg_test
 * The library sources are checked by compiling them with OBJECK_CFG_VERIFY=1,
 * which makes the optimizer verify the graph of every method it sees.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved. See cfg.h for the license.
 ***************************************************************************/

#include "cfg.h"
#include <iostream>
#include <string>

using namespace backend;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { ++checks; if(!(cond)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #cond << std::endl; } } while(0)

namespace {
  IntermediateInstruction* Make(InstructionType t, long o1 = 0, long o2 = -1, long o3 = 0)
  {
    if(o3 != 0) {
      return IntermediateFactory::Instance()->MakeInstruction(nullptr, 1, t, o1, o2, o3);
    }
    return IntermediateFactory::Instance()->MakeInstruction(nullptr, 1, t, o1, o2);
  }

  IntermediateInstruction* Lbl(long id) { return Make(LBL, id, 0); }
  IntermediateInstruction* Jmp(long id) { return Make(JMP, id, -1); }
  IntermediateInstruction* JmpIf(long id, long when) { return Make(JMP, id, when); }
  IntermediateInstruction* Load(long slot) { return Make(LOAD_INT_VAR, slot, LOCL); }
  IntermediateInstruction* Stor(long slot) { return Make(STOR_INT_VAR, slot, LOCL); }
  IntermediateInstruction* Lit(long v) { return IntermediateFactory::Instance()->MakeIntLitInstruction(nullptr, 1, v); }

  typedef std::vector<IntermediateInstruction*> List;

  void Verified(const Cfg& cfg, const char* name)
  {
    std::string why;
    ++checks;
    if(!cfg.Verify(&why)) {
      ++failures;
      std::cerr << "FAIL verify " << name << ": " << why << "\n" << cfg.Describe() << std::endl;
    }
  }
}

// a while loop:  x := 0; while(x < n) { x := x + 1 }; return
static void TestWhile()
{
  List l = {
    Lit(0), Stor(0),            // 0 1
    Lbl(1),                     // 2   header
    Load(0), Load(1), Make(LES_INT), JmpIf(2, 0),   // 3 4 5 6
    Load(0), Lit(1), Make(ADD_INT), Stor(0),        // 7 8 9 10
    Jmp(1),                     // 11
    Lbl(2),                     // 12
    Make(RTRN)                  // 13
  };
  Cfg cfg(l);
  CHECK(cfg.WellFormed());
  Verified(cfg, "while");
  CHECK(cfg.Size() == 4);                     // init | header test | body | exit
  CHECK(cfg.Loops().size() == 1);
  const Cfg::Loop& loop = cfg.Loops()[0];
  CHECK(loop.header == cfg.BlockOfLabel(1));
  CHECK(loop.blocks.size() == 2);            // the header (test) and the body
  CHECK(loop.exits.size() == 1);
  CHECK(!loop.exits[0].is_handler);
  CHECK(loop.preheader == 0);
  CHECK(loop.preheader_at == 2);             // before the header label: fall-through entry
  CHECK(cfg.Dominates(0, loop.header));
  CHECK(cfg.Dominates(loop.header, cfg.BlockOfLabel(2)));
  CHECK(!cfg.Dominates(cfg.BlockOfLabel(2), loop.header));

  // liveness: x (slot 0) and n (slot 1) are read in the loop; x is written before the loop
  cfg.ComputeLiveness();
  CHECK(cfg.LiveIn(loop.header, 0));
  CHECK(cfg.LiveIn(loop.header, 1));
  CHECK(!cfg.LiveIn(0, 0));                  // x := 0 kills it
  CHECK(cfg.LiveIn(0, 1));                   // n is a parameter, read through the loop
  CHECK(!cfg.LiveIn(cfg.BlockOfLabel(2), 0));
  CHECK(cfg.LiveBefore(3, 0));
  CHECK(!cfg.LiveBefore(10, 2));
}

// a do-while: the back edge is a conditional jump
static void TestDoWhile()
{
  List l = {
    Lit(0), Stor(0),            // 0 1
    Lbl(1),                     // 2
    Load(0), Lit(1), Make(ADD_INT), Stor(0),        // 3..6
    Load(0), Lit(10), Make(LES_INT), JmpIf(1, 1),   // 7..10   back edge when true
    Make(RTRN)                  // 11
  };
  Cfg cfg(l);
  CHECK(cfg.WellFormed());
  Verified(cfg, "do-while");
  CHECK(cfg.Loops().size() == 1);
  CHECK(cfg.Loops()[0].blocks.size() == 1);
  CHECK(cfg.Loops()[0].latches.size() == 1);
  CHECK(cfg.Loops()[0].latches[0] == cfg.Loops()[0].header);
  CHECK(cfg.Loops()[0].preheader == 0);
}

// an if / else diamond and its dominators
static void TestDiamond()
{
  List l = {
    Load(0), JmpIf(1, 0),               // 0 1
    Lit(1), Stor(1), Jmp(2),            // 2 3 4
    Lbl(1),                             // 5
    Lit(2), Stor(1),                    // 6 7
    Lbl(2),                             // 8
    Load(1), Stor(2), Make(RTRN)        // 9 10 11
  };
  Cfg cfg(l);
  CHECK(cfg.WellFormed());
  Verified(cfg, "diamond");
  CHECK(cfg.Size() == 4);
  CHECK(cfg.Loops().empty());
  const int join = cfg.BlockOfLabel(2);
  CHECK(cfg.Blocks()[join].idom == 0);       // neither arm dominates the join
  CHECK(!cfg.Dominates(1, join));
  CHECK(!cfg.Dominates(2, join));
  CHECK(cfg.Blocks()[join].preds.size() == 2);
  cfg.ComputeLiveness();
  CHECK(!cfg.LiveIn(0, 1));                  // slot 1 is written on both arms before the join reads it
  CHECK(cfg.LiveIn(join, 1));
}

// a continue: two latches, one header
static void TestContinue()
{
  List l = {
    Lit(0), Stor(0),                              // 0 1
    Lbl(1),                                       // 2
    Load(0), Load(1), Make(LES_INT), JmpIf(9, 0), // 3..6   exit
    Load(0), JmpIf(2, 1),                         // 7 8    continue
    Load(0), Lit(1), Make(ADD_INT), Stor(0),      // 9..12
    Jmp(1),                                       // 13
    Lbl(9),                                       // 14
    Make(RTRN)                                    // 15
  };
  // the continue jumps to label 2, which the list does not define: that is a malformed list
  Cfg bad(l);
  CHECK(!bad.WellFormed());

  List ok = {
    Lit(0), Stor(0),
    Lbl(1),
    Load(0), Load(1), Make(LES_INT), JmpIf(9, 0),
    Load(0), JmpIf(1, 1),
    Load(0), Lit(1), Make(ADD_INT), Stor(0),
    Jmp(1),
    Lbl(9),
    Make(RTRN)
  };
  Cfg cfg(ok);
  CHECK(cfg.WellFormed());
  Verified(cfg, "continue");
  CHECK(cfg.Loops().size() == 1);
  CHECK(cfg.Loops()[0].latches.size() == 2);
  CHECK(cfg.Loops()[0].blocks.size() == 3);
}

// a loop nest: the inner loop first, with its depth
static void TestNest()
{
  List l = {
    Lit(0), Stor(0),                                  // 0 1
    Lbl(1),                                           // 2  outer header
    Load(0), Lit(5), Make(LES_INT), JmpIf(4, 0),      // 3..6
    Lit(0), Stor(1),                                  // 7 8
    Lbl(2),                                           // 9  inner header
    Load(1), Lit(5), Make(LES_INT), JmpIf(3, 0),      // 10..13
    Load(1), Lit(1), Make(ADD_INT), Stor(1),          // 14..17
    Jmp(2),                                           // 18
    Lbl(3),                                           // 19
    Load(0), Lit(1), Make(ADD_INT), Stor(0),          // 20..23
    Jmp(1),                                           // 24
    Lbl(4),                                           // 25
    Make(RTRN)                                        // 26
  };
  Cfg cfg(l);
  CHECK(cfg.WellFormed());
  Verified(cfg, "nest");
  CHECK(cfg.Loops().size() == 2);
  CHECK(cfg.Loops()[0].depth == 2);          // innermost first
  CHECK(cfg.Loops()[1].depth == 1);
  CHECK(cfg.Loops()[0].parent == 1);
  CHECK(cfg.Loops()[0].blocks.size() < cfg.Loops()[1].blocks.size());
  CHECK(cfg.Loops()[1].Contains(cfg.Loops()[0].header));
  // the inner loop's preheader is the outer body's store block
  CHECK(cfg.Loops()[0].preheader >= 0);
  CHECK(cfg.Loops()[0].preheader_at == 9);
  // the outer loop is entered from the method's start block
  CHECK(cfg.Loops()[1].preheader == 0);
}

// a try region: the handler is reached from every block of the region
static void TestTry()
{
  List l = {
    Lit(0), Stor(5),                                  // 0 1
    Make(TRY_START, 1, -1),                           // 2
    Lit(1), Stor(5), Make(TRY_END),                   // 3 4 5  (one block: TRY_END ends it)
    Jmp(2),                                           // 6
    Lbl(1),                                           // 7  handler
    Load(5), Stor(6),                                 // 8 9
    Lbl(2),                                           // 10
    Make(RTRN)                                        // 11
  };
  Cfg cfg(l);
  CHECK(cfg.WellFormed());
  Verified(cfg, "try");
  const int handler = cfg.BlockOfLabel(1);
  const int region_block = cfg.BlockOf(3);
  CHECK(!cfg.Blocks()[region_block].handlers.empty());
  CHECK(cfg.Blocks()[region_block].handlers[0] == handler);
  CHECK(cfg.Blocks()[cfg.BlockOf(0)].handlers.empty());     // before TRY_START
  CHECK(cfg.Blocks()[handler].handlers.empty());
  // the handler reads slot 5, so it is live through the whole region, even before the store in it
  cfg.ComputeLiveness();
  CHECK(cfg.LiveIn(handler, 5));
  CHECK(cfg.LiveIn(region_block, 5));
  CHECK(cfg.LiveBefore(3, 5));
  // ...and slot 5 is written before the region, so it is not live at the start
  CHECK(!cfg.LiveIn(0, 5));

  // an unbalanced region is refused
  List unbalanced = { Make(TRY_START, 1, -1), Lbl(1), Make(RTRN) };
  Cfg u(unbalanced);
  CHECK(!u.WellFormed());
  List stray = { Make(TRY_END), Make(RTRN) };
  Cfg s(stray);
  CHECK(!s.WellFormed());
}

// a loop with a try region inside it: the handler is part of the loop
static void TestLoopWithTry()
{
  List l = {
    Lbl(1),                                           // 0  header
    Load(0), JmpIf(9, 0),                             // 1 2
    Make(TRY_START, 2, -1),                           // 3
    Lit(1), Stor(1), Make(TRY_END),                   // 4 5 6
    Jmp(3),                                           // 7
    Lbl(2),                                           // 8  handler
    Lit(2), Stor(1),                                  // 9 10
    Lbl(3),                                           // 11
    Jmp(1),                                           // 12
    Lbl(9),                                           // 13
    Make(RTRN)                                        // 14
  };
  Cfg cfg(l);
  CHECK(cfg.WellFormed());
  Verified(cfg, "loop with try");
  CHECK(cfg.Loops().size() == 1);
  CHECK(cfg.Loops()[0].Contains(cfg.BlockOfLabel(2)));      // the handler is in the loop
  CHECK(cfg.Loops()[0].header == 0);
  CHECK(cfg.Loops()[0].preheader == -1);                    // the header is the first block: no place for code
}

// a select: JMP_TABLE with its slots and default
static void TestSelect()
{
  List l = {
    Load(0),                                          // 0
    Make(JMP_TABLE, 1, 3, 20),                        // 1  base 1, three slots, default 20
    Make(JMP_TABLE_SLOT, 10, -1),                     // 2
    Make(JMP_TABLE_SLOT, 11, -1),                     // 3
    Make(JMP_TABLE_SLOT, 20, -1),                     // 4
    Lbl(10), Lit(1), Stor(1), Jmp(30),                // 5..8
    Lbl(11), Lit(2), Stor(1), Jmp(30),                // 9..12
    Lbl(20), Lit(3), Stor(1), Jmp(30),                // 13..16
    Lbl(30), Make(RTRN)                               // 17 18
  };
  Cfg cfg(l);
  CHECK(cfg.WellFormed());
  Verified(cfg, "select");
  CHECK(cfg.BlockOf(2) == cfg.BlockOf(1));          // slots stay with the table
  CHECK(cfg.BlockOf(4) == cfg.BlockOf(1));
  CHECK(cfg.Blocks()[cfg.BlockOf(1)].succs.size() == 3);   // 10, 11, 20 (the default repeats a slot)
  CHECK(cfg.Blocks()[cfg.BlockOfLabel(30)].preds.size() == 3);
  CHECK(cfg.Blocks()[cfg.BlockOfLabel(30)].idom == 0);
}

// two ways into a loop header: no preheader
static void TestTwoEntries()
{
  List l = {
    Load(0), JmpIf(1, 1),                             // 0 1  jumps into the header
    Lit(0), Stor(1),                                  // 2 3
    Lbl(1),                                           // 4  header
    Load(1), Lit(1), Make(ADD_INT), Stor(1),          // 5..8
    Load(1), Lit(9), Make(LES_INT), JmpIf(1, 1),      // 9..12
    Make(RTRN)                                        // 13
  };
  Cfg cfg(l);
  CHECK(cfg.WellFormed());
  Verified(cfg, "two entries");
  CHECK(cfg.Loops().size() == 1);
  CHECK(cfg.Loops()[0].preheader == -1);
}

// an entering jump: the preheader code goes before the JMP
static void TestEnteringJump()
{
  List l = {
    Lit(0), Stor(0),                                  // 0 1
    Jmp(1),                                           // 2
    Lit(7), Stor(3),                                  // 3 4  (dead)
    Lbl(1),                                           // 5
    Load(0), Lit(1), Make(ADD_INT), Stor(0),          // 6..9
    Load(0), Lit(9), Make(LES_INT), JmpIf(1, 1),      // 10..13
    Make(RTRN)                                        // 14
  };
  Cfg cfg(l);
  CHECK(cfg.WellFormed());
  Verified(cfg, "entering jump");
  CHECK(cfg.Loops().size() == 1);
  CHECK(cfg.Loops()[0].preheader == 0);
  CHECK(cfg.Loops()[0].preheader_at == 2);           // before the JMP
  CHECK(!cfg.Blocks()[cfg.BlockOf(3)].reachable);    // the dead store is unreachable
}

// a loop with an early return in its body
static void TestEarlyReturn()
{
  List l = {
    Lit(0), Stor(0),                                  // 0 1
    Lbl(1),                                           // 2
    Load(0), Lit(5), Make(LES_INT), JmpIf(9, 0),      // 3..6
    Load(0), Lit(3), Make(EQL_INT), JmpIf(2, 0),      // 7..10
    Make(RTRN),                                       // 11   early return
    Lbl(2),                                           // 12
    Load(0), Lit(1), Make(ADD_INT), Stor(0), Jmp(1),  // 13..17
    Lbl(9), Make(RTRN)                                // 18 19
  };
  Cfg cfg(l);
  CHECK(cfg.WellFormed());
  Verified(cfg, "early return");
  CHECK(cfg.Loops().size() == 1);
  CHECK(!cfg.Loops()[0].Contains(cfg.BlockOf(11)));  // the return block cannot reach the back edge
  CHECK(cfg.Loops()[0].exits.size() == 2);
}

// malformed lists are refused, not mis-analyzed
static void TestMalformed()
{
  List missing = { Jmp(5), Make(RTRN) };
  Cfg a(missing);
  CHECK(!a.WellFormed());
  List twice = { Lbl(1), Lbl(1), Make(RTRN) };
  Cfg b(twice);
  CHECK(!b.WellFormed());
  List empty;
  Cfg c(empty);
  CHECK(c.WellFormed());
  CHECK(c.Size() == 0);
}

int main()
{
  TestWhile();
  TestDoWhile();
  TestDiamond();
  TestContinue();
  TestNest();
  TestTry();
  TestLoopWithTry();
  TestSelect();
  TestTwoEntries();
  TestEnteringJump();
  TestEarlyReturn();
  TestMalformed();

  std::cout << "cfg_test: " << checks << " checks, " << failures << " failures" << std::endl;
  return failures == 0 ? 0 : 1;
}
