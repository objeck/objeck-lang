/***************************************************************************
 * A control-flow graph over a method's intermediate instruction list.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#pragma once

#include "emit.h"
#include <cstdint>
#include <string>
#include <vector>

namespace backend {
  //
  // WHAT THIS IS
  // ------------
  // The optimizer works on a flat list of instructions per method, so each pass
  // used to guess the control flow for itself. This is the one description of it:
  // basic blocks, the edges between them, dominators, natural loops with their
  // preheaders, and a liveness query for local slots. It only ANALYZES: it never
  // changes the list it is given. A pass that edits the list builds a new Cfg.
  //
  // Labels are read the way the optimizer sees them, BEFORE JumpToLocation: a
  // JMP, a TRY_START and a JMP_TABLE_SLOT carry a label id, a JMP_TABLE carries
  // its default label in operand3, and an LBL defines the id. Do not build a Cfg
  // over a method whose jumps were already resolved to instruction indexes.
  //
  // Control flow it models:
  //  - a leader is the first instruction, every LBL, and the instruction after a
  //    JMP, RTRN, TRY_START, TRY_END or a JMP_TABLE with its slots;
  //  - JMP (operand2 < 0) goes to its label only; a conditional JMP also falls
  //    through; RTRN ends the method; JMP_TABLE goes to every slot and the default;
  //  - a TRY_START..TRY_END region's handler is reached from EVERY block of the
  //    region (a trap can happen anywhere in it), and those edges are marked.
  //
  class Cfg {
   public:
    struct Block {
      int id;
      size_t first;                  // index of its first instruction
      size_t last;                   // index of its last instruction (inclusive)
      std::vector<int> succs;        // every successor, handler edges included; no duplicates
      std::vector<int> preds;
      std::vector<int> normal;       // successors reached by running the block to its end
      std::vector<int> handlers;     // successors reached because a trap can leave the block
      std::vector<int> region;       // the handlers of the try regions the block is inside, innermost last
      bool reachable;                // from the entry block
      int idom;                      // immediate dominator; the entry's is itself; -1 when unreachable
      int rpo;                       // reverse post-order number; -1 when unreachable
      bool falls_off_end;            // its last instruction falls through but there is no next block
    };

    struct Exit {
      int from;                      // a block of the loop
      int to;                        // a block outside it
      bool is_handler;               // true when only a trap takes control there
    };

    struct Loop {
      int header;
      std::vector<int> latches;      // the sources of its back edges
      std::vector<int> blocks;       // every block of the loop, header included, in block order
      std::vector<bool> member;      // indexed by block id
      std::vector<Exit> exits;
      int parent;                    // the smallest loop that contains this one, or -1
      int depth;                     // 1 for an outermost loop
      // The preheader: the one block outside the loop that enters its header, when
      // it enters nothing else. -1 when there is none (several outside entries, the
      // header is the method's first block, or the entering block also branches
      // elsewhere). preheader_at is the instruction index code can be inserted
      // before so that it runs once per entry of the loop and never inside it: the
      // header's own first instruction on a fall-through entry, or the entering
      // jump itself.
      int preheader;
      size_t preheader_at;

      bool Contains(int block) const {
        return block >= 0 && static_cast<size_t>(block) < member.size() && member[block];
      }
    };

    // Builds the graph of instrs. The list must outlive the Cfg (it is read, not copied).
    explicit Cfg(const std::vector<IntermediateInstruction*>& instrs);

    // false when the list did not have the shape the analysis relies on: a jump to a
    // label nothing defines, a label defined twice, a TRY_END with no TRY_START, or a
    // control-flow edge that leaves a try region another way than TRY_END. Every query
    // other than Describe() is then meaningless and a pass must leave the method alone.
    bool WellFormed() const { return well_formed; }
    const std::string& Problem() const { return problem; }

    const std::vector<Block>& Blocks() const { return blocks; }
    size_t Size() const { return blocks.size(); }
    // the block that holds instruction index i
    int BlockOf(size_t i) const { return block_of[i]; }
    // the block an LBL id names, or -1
    int BlockOfLabel(long label) const;
    // block a dominates block b (every path from the entry to b goes through a); a block dominates itself
    bool Dominates(int a, int b) const;

    const std::vector<Loop>& Loops() const { return loops; }
    // loops that could not be analyzed: a back-looking edge to a block that does not dominate it
    size_t IrreducibleEdges() const { return irreducible_edges; }

    // ---- liveness of local slots (LOCL variables), computed on demand ----
    // A slot is live where some path from there reads it before writing it. A trap
    // can leave a try region at any point, so a handler's live-in counts as live
    // through the whole region. Instructions that read the frame without naming a
    // slot (native calls, thread operations) count as reading every slot.
    void ComputeLiveness();
    bool LiveIn(int block, long slot) const;
    bool LiveOut(int block, long slot) const;
    // live just before instruction i executes
    bool LiveBefore(size_t i, long slot) const;

    // What an instruction does to the frame's local slots. Used by liveness and by
    // the passes that need the same answer.
    struct SlotUse {
      long read_a;      // -1 when none
      long read_b;
      long write_a;
      long write_b;
      bool reads_all;   // reads the frame without naming a slot
    };
    static SlotUse SlotUseOf(IntermediateInstruction* instr);

    // ---- checks ----
    // The invariants the analysis promises: the blocks cover every instruction
    // exactly once, every edge leaves from a block's last instruction and arrives
    // at a leader, the dominator tree is a tree and agrees with an independent
    // computation, and every loop's header dominates its latches. true when all
    // hold; otherwise says which in *why.
    bool Verify(std::string* why) const;

    // a readable dump for debugging
    std::string Describe() const;

    // true when the environment variable OBJECK_CFG_VERIFY is set to 1
    static bool VerifyRequested();
    // the file OBJECK_CFG_DUMP names, or empty: the optimizer writes every method's final graph there (tools/cicd/cfg_oracle.py)
    static std::string DumpPath();

   private:
    const std::vector<IntermediateInstruction*>& instrs;
    std::vector<Block> blocks;
    std::vector<int> block_of;
    std::vector<int> rpo_order;       // reachable blocks, entry first
    std::vector<int> dom_in;          // dominator-tree entry and exit numbers, for Dominates
    std::vector<int> dom_out;
    std::vector<Loop> loops;
    std::vector<long> label_ids;      // label id for lookup (sorted with label_blocks)
    std::vector<int> label_blocks;
    std::vector<bool> is_leader;
    size_t irreducible_edges;
    bool well_formed;
    std::string problem;

    // liveness: a bit set of slots per block
    size_t slot_words;
    size_t slot_count;
    bool has_liveness;
    std::vector<uint64_t> live_in;
    std::vector<uint64_t> live_out;

    void Build();
    void Fail(const std::string& why);
    void AddEdge(int from, int to, bool handler);
    void ComputeDominators();
    void FindLoops();
    size_t LastReal(const Block& b) const;
    bool TestBit(const std::vector<uint64_t>& v, int block, long slot) const;
  };
}
