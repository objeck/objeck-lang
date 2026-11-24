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

#include "cfg.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>
#include <unordered_map>

namespace backend {
  namespace {
    inline bool IsUncondJump(IntermediateInstruction* i) {
      return i->GetType() == JMP && i->GetOperand2() < 0;
    }

    inline bool Has(const std::vector<int>& v, int x) {
      return std::find(v.begin(), v.end(), x) != v.end();
    }
  }

  bool Cfg::VerifyRequested()
  {
#ifdef _WIN32
    size_t len = 0;
    char buf[16];
    if(getenv_s(&len, buf, sizeof(buf), "OBJECK_CFG_VERIFY") == 0 && len > 1) {
      return buf[0] == '1';
    }
    return false;
#else
    const char* found = std::getenv("OBJECK_CFG_VERIFY");
    return found && found[0] == '1';
#endif
  }

  std::string Cfg::DumpPath()
  {
#ifdef _WIN32
    size_t len = 0;
    char buf[1024];
    if(getenv_s(&len, buf, sizeof(buf), "OBJECK_CFG_DUMP") == 0 && len > 1) {
      return std::string(buf);
    }
    return std::string();
#else
    const char* found = std::getenv("OBJECK_CFG_DUMP");
    return found ? std::string(found) : std::string();
#endif
  }

  Cfg::Cfg(const std::vector<IntermediateInstruction*>& i) : instrs(i)
  {
    irreducible_edges = 0;
    well_formed = true;
    slot_words = 0;
    slot_count = 0;
    has_liveness = false;
    Build();
  }

  void Cfg::Fail(const std::string& why)
  {
    if(well_formed) {
      problem = why;
    }
    well_formed = false;
  }

  void Cfg::AddEdge(int from, int to, bool handler)
  {
    Block& f = blocks[from];
    if(handler) {
      if(!Has(f.handlers, to)) {
        f.handlers.push_back(to);
      }
    }
    else if(!Has(f.normal, to)) {
      f.normal.push_back(to);
    }

    if(!Has(f.succs, to)) {
      f.succs.push_back(to);
      blocks[to].preds.push_back(from);
    }
  }

  // the last instruction of a block that is not a jump-table slot
  size_t Cfg::LastReal(const Block& b) const
  {
    size_t k = b.last;
    while(k > b.first && instrs[k]->GetType() == JMP_TABLE_SLOT) {
      --k;
    }
    return k;
  }

  int Cfg::BlockOfLabel(long label) const
  {
    auto it = std::lower_bound(label_ids.begin(), label_ids.end(), label);
    if(it != label_ids.end() && *it == label) {
      return label_blocks[it - label_ids.begin()];
    }
    return -1;
  }

  void Cfg::Build()
  {
    const size_t n = instrs.size();
    block_of.assign(n, -1);
    if(n == 0) {
      return;
    }

    // labels
    std::map<long, size_t> label_pos;
    for(size_t i = 0; i < n; ++i) {
      if(instrs[i]->GetType() == LBL) {
        if(!label_pos.insert(std::make_pair(instrs[i]->GetOperand(), i)).second) {
          Fail("label " + std::to_string(instrs[i]->GetOperand()) + " is defined twice");
        }
      }
    }

    // leaders
    is_leader.assign(n + 1, false);
    is_leader[0] = true;
    for(size_t i = 0; i < n; ++i) {
      switch(instrs[i]->GetType()) {
      case LBL:
        is_leader[i] = true;
        break;

      case JMP:
      case RTRN:
      case TRY_START:
      case TRY_END:
        is_leader[i + 1] = true;
        break;

      case JMP_TABLE: {
        // the slots are data that belong to the table; code resumes after them
        size_t j = i + 1;
        while(j < n && instrs[j]->GetType() == JMP_TABLE_SLOT) {
          ++j;
        }
        is_leader[j] = true;
        break;
      }

      default:
        break;
      }
    }

    // blocks
    for(size_t i = 0; i < n; ) {
      Block b;
      b.id = static_cast<int>(blocks.size());
      b.first = i;
      size_t j = i + 1;
      while(j < n && !is_leader[j]) {
        ++j;
      }
      b.last = j - 1;
      b.reachable = false;
      b.idom = -1;
      b.rpo = -1;
      b.falls_off_end = false;
      for(size_t k = i; k < j; ++k) {
        block_of[k] = b.id;
      }
      blocks.push_back(b);
      i = j;
    }

    // which block each label starts (a label is always a leader)
    for(const auto& entry : label_pos) {
      label_ids.push_back(entry.first);
      label_blocks.push_back(block_of[entry.second]);
    }

    const int block_count = static_cast<int>(blocks.size());

    // normal edges
    for(int bi = 0; bi < block_count; ++bi) {
      const size_t last = LastReal(blocks[bi]);
      IntermediateInstruction* term = instrs[last];
      bool falls = true;

      switch(term->GetType()) {
      case JMP: {
        const int target = BlockOfLabel(term->GetOperand());
        if(target < 0) {
          Fail("JMP to label " + std::to_string(term->GetOperand()) + " that nothing defines");
        }
        else {
          AddEdge(bi, target, false);
        }
        falls = term->GetOperand2() >= 0;
        break;
      }

      case RTRN:
        falls = false;
        break;

      case JMP_TABLE: {
        for(size_t k = last + 1; k <= blocks[bi].last; ++k) {
          const int target = BlockOfLabel(instrs[k]->GetOperand());
          if(target < 0) {
            Fail("JMP_TABLE_SLOT to label " + std::to_string(instrs[k]->GetOperand()) + " that nothing defines");
          }
          else {
            AddEdge(bi, target, false);
          }
        }
        const int target = BlockOfLabel(term->GetOperand3());
        if(target < 0) {
          Fail("JMP_TABLE default label " + std::to_string(term->GetOperand3()) + " is not defined");
        }
        else {
          AddEdge(bi, target, false);
        }
        falls = false;
        break;
      }

      default:
        break;
      }

      if(falls) {
        if(bi + 1 < block_count) {
          AddEdge(bi, bi + 1, false);
        }
        else {
          blocks[bi].falls_off_end = true;
        }
      }
    }

    // The try regions each block is inside, by walking the edges: TRY_START pushes its
    // handler and TRY_END pops it. A TRY_START or TRY_END is the last instruction of its
    // block, so a block is inside the same regions throughout. The walk follows control
    // flow rather than the layout because a `return`, `break` or `continue` inside a region
    // closes it with a TRY_END of its own and leaves the layout's next TRY_END unmatched.
    // A trap can leave any block of a region for its handler, so every block of the region
    // gets an edge there; control arriving at the handler is outside that region.
    {
      std::vector<bool> known(block_count, false);
      std::vector<int> work;
      known[0] = true;
      work.push_back(0);

      auto reach = [&](int to, const std::vector<int>& stack, int from) {
        if(!known[to]) {
          known[to] = true;
          blocks[to].region = stack;
          work.push_back(to);
        }
        else if(blocks[to].region != stack) {
          Fail("block " + std::to_string(to) + " is entered from block " + std::to_string(from) + " in a different try region");
        }
      };

      while(!work.empty() && well_formed) {
        const int bi = work.back();
        work.pop_back();
        const std::vector<int> stack = blocks[bi].region;
        std::vector<int> after = stack;
        IntermediateInstruction* term = instrs[LastReal(blocks[bi])];
        if(term->GetType() == TRY_START) {
          const int handler = BlockOfLabel(term->GetOperand());
          if(handler < 0) {
            Fail("TRY_START names label " + std::to_string(term->GetOperand()) + " that nothing defines");
            break;
          }
          after.push_back(handler);
        }
        else if(term->GetType() == TRY_END) {
          if(after.empty()) {
            Fail("TRY_END with no TRY_START");
            break;
          }
          after.pop_back();
        }

        const std::vector<int> normal = blocks[bi].normal;
        for(int to : normal) {
          reach(to, after, bi);
        }
        for(size_t k = 0; k < stack.size(); ++k) {
          const std::vector<int> outside(stack.begin(), stack.begin() + k);
          AddEdge(bi, stack[k], true);
          reach(stack[k], outside, bi);
        }
      }
    }

    if(!well_formed) {
      return;
    }

    // reachability and reverse post-order, by an iterative depth-first search
    {
      std::vector<int> post;
      std::vector<std::pair<int, size_t> > work;
      blocks[0].reachable = true;
      work.push_back(std::make_pair(0, static_cast<size_t>(0)));
      while(!work.empty()) {
        const int b = work.back().first;
        size_t& next = work.back().second;
        if(next < blocks[b].succs.size()) {
          const int s = blocks[b].succs[next++];
          if(!blocks[s].reachable) {
            blocks[s].reachable = true;
            work.push_back(std::make_pair(s, static_cast<size_t>(0)));
          }
        }
        else {
          post.push_back(b);
          work.pop_back();
        }
      }
      rpo_order.assign(post.rbegin(), post.rend());
      for(size_t k = 0; k < rpo_order.size(); ++k) {
        blocks[rpo_order[k]].rpo = static_cast<int>(k);
      }
    }

    ComputeDominators();
    FindLoops();
  }

  // Cooper, Harvey and Kennedy, "A Simple, Fast Dominance Algorithm"
  void Cfg::ComputeDominators()
  {
    const int count = static_cast<int>(blocks.size());
    blocks[0].idom = 0;

    bool changed = true;
    while(changed) {
      changed = false;
      for(size_t k = 1; k < rpo_order.size(); ++k) {
        const int b = rpo_order[k];
        int candidate = -1;
        for(int p : blocks[b].preds) {
          if(!blocks[p].reachable || blocks[p].idom < 0) {
            continue;
          }
          if(candidate < 0) {
            candidate = p;
            continue;
          }
          int x = p;
          int y = candidate;
          while(x != y) {
            while(blocks[x].rpo > blocks[y].rpo) {
              x = blocks[x].idom;
            }
            while(blocks[y].rpo > blocks[x].rpo) {
              y = blocks[y].idom;
            }
          }
          candidate = x;
        }
        if(candidate >= 0 && blocks[b].idom != candidate) {
          blocks[b].idom = candidate;
          changed = true;
        }
      }
    }

    // number the dominator tree so that Dominates is a range test
    std::vector<std::vector<int> > children(count);
    for(int b = 1; b < count; ++b) {
      if(blocks[b].reachable && blocks[b].idom >= 0) {
        children[blocks[b].idom].push_back(b);
      }
    }
    dom_in.assign(count, -1);
    dom_out.assign(count, -1);
    int clock = 0;
    std::vector<std::pair<int, size_t> > work;
    work.push_back(std::make_pair(0, static_cast<size_t>(0)));
    dom_in[0] = clock++;
    while(!work.empty()) {
      const int b = work.back().first;
      size_t& next = work.back().second;
      if(next < children[b].size()) {
        const int c = children[b][next++];
        dom_in[c] = clock++;
        work.push_back(std::make_pair(c, static_cast<size_t>(0)));
      }
      else {
        dom_out[b] = clock++;
        work.pop_back();
      }
    }
  }

  bool Cfg::Dominates(int a, int b) const
  {
    if(a < 0 || b < 0 || static_cast<size_t>(a) >= blocks.size() || static_cast<size_t>(b) >= blocks.size()) {
      return false;
    }
    if(!blocks[a].reachable || !blocks[b].reachable) {
      return false;
    }
    return dom_in[a] <= dom_in[b] && dom_out[b] <= dom_out[a];
  }

  void Cfg::FindLoops()
  {
    const int count = static_cast<int>(blocks.size());

    // back edges, grouped by header
    std::map<int, std::vector<int> > latches_of;
    for(int n = 0; n < count; ++n) {
      if(!blocks[n].reachable) {
        continue;
      }
      for(int s : blocks[n].succs) {
        if(Dominates(s, n)) {
          latches_of[s].push_back(n);
        }
        else if(blocks[s].rpo <= blocks[n].rpo) {
          // an edge that goes back in the order to a block that does not dominate its source
          ++irreducible_edges;
        }
      }
    }

    for(const auto& entry : latches_of) {
      Loop loop;
      loop.header = entry.first;
      loop.latches = entry.second;
      loop.member.assign(count, false);
      loop.member[loop.header] = true;
      loop.parent = -1;
      loop.depth = 1;
      loop.preheader = -1;
      loop.preheader_at = 0;

      // the loop is the header and everything that reaches a latch without going through it
      std::vector<int> work;
      for(int l : loop.latches) {
        if(!loop.member[l]) {
          loop.member[l] = true;
          work.push_back(l);
        }
      }
      while(!work.empty()) {
        const int b = work.back();
        work.pop_back();
        for(int p : blocks[b].preds) {
          if(blocks[p].reachable && !loop.member[p]) {
            loop.member[p] = true;
            work.push_back(p);
          }
        }
      }
      for(int b = 0; b < count; ++b) {
        if(loop.member[b]) {
          loop.blocks.push_back(b);
        }
      }

      // exits
      for(int b : loop.blocks) {
        for(int s : blocks[b].succs) {
          if(!loop.member[s]) {
            Exit e;
            e.from = b;
            e.to = s;
            e.is_handler = !Has(blocks[b].normal, s);
            loop.exits.push_back(e);
          }
        }
      }

      // the preheader
      if(loop.header != 0) {
        int outside = -1;
        int outside_count = 0;
        for(int p : blocks[loop.header].preds) {
          if(blocks[p].reachable && !loop.member[p]) {
            outside = p;
            ++outside_count;
          }
        }
        // a header entered by a trap (a handler that is also a loop head) has no place to put code
        if(outside_count == 1 && !Has(blocks[outside].handlers, loop.header) &&
           blocks[outside].normal.size() == 1 && blocks[outside].normal[0] == loop.header) {
          const Block& pre = blocks[outside];
          IntermediateInstruction* term = instrs[LastReal(pre)];
          if(IsUncondJump(term)) {
            loop.preheader = outside;
            loop.preheader_at = LastReal(pre);
          }
          else if(term->GetType() != JMP && term->GetType() != JMP_TABLE && term->GetType() != RTRN &&
                  pre.last + 1 == blocks[loop.header].first) {
            loop.preheader = outside;
            loop.preheader_at = blocks[loop.header].first;
          }
        }
      }

      loops.push_back(loop);
    }

    // innermost first, then the nesting
    std::stable_sort(loops.begin(), loops.end(), [this](const Loop& a, const Loop& b) {
      if(a.blocks.size() != b.blocks.size()) {
        return a.blocks.size() < b.blocks.size();
      }
      return blocks[a.header].rpo < blocks[b.header].rpo;
    });
    for(size_t i = 0; i < loops.size(); ++i) {
      for(size_t j = i + 1; j < loops.size(); ++j) {
        if(loops[j].Contains(loops[i].header)) {
          loops[i].parent = static_cast<int>(j);
          break;
        }
      }
    }
    for(size_t i = loops.size(); i-- > 0; ) {
      loops[i].depth = loops[i].parent < 0 ? 1 : loops[loops[i].parent].depth + 1;
    }
  }

  // ------------------- liveness -------------------

  Cfg::SlotUse Cfg::SlotUseOf(IntermediateInstruction* instr)
  {
    SlotUse use;
    use.read_a = use.read_b = use.write_a = use.write_b = -1;
    use.reads_all = false;

    const bool local = instr->GetOperand2() == LOCL;
    switch(instr->GetType()) {
    case LOAD_INT_VAR:
    case LOAD_FLOAT_VAR:
      if(local) {
        use.read_a = instr->GetOperand();
      }
      break;

    case LOAD_FUNC_VAR:
      if(local) {
        use.read_a = instr->GetOperand();
        use.read_b = instr->GetOperand() + 1;
      }
      break;

    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
    case COPY_INT_VAR:
    case COPY_FLOAT_VAR:
      if(local) {
        use.write_a = instr->GetOperand();
      }
      break;

    case STOR_FUNC_VAR:
    case COPY_FUNC_VAR:
      if(local) {
        use.write_a = instr->GetOperand();
        use.write_b = instr->GetOperand() + 1;
      }
      break;

    // 'self' is local slot 0
    case LOAD_INST_MEM:
      use.read_a = 0;
      break;

    // these read the frame without naming a slot
    case EXT_LIB_LOAD:
    case EXT_LIB_UNLOAD:
    case EXT_LIB_FUNC_CALL:
    case ASYNC_MTHD_CALL:
    case THREAD_JOIN:
    case THREAD_MUTEX:
      use.reads_all = true;
      break;

    default:
      break;
    }

    return use;
  }

  void Cfg::ComputeLiveness()
  {
    if(has_liveness || !well_formed) {
      return;
    }
    has_liveness = true;

    long max_slot = 0;
    for(IntermediateInstruction* instr : instrs) {
      const SlotUse u = SlotUseOf(instr);
      max_slot = (std::max)(max_slot, (std::max)((std::max)(u.read_a, u.read_b), (std::max)(u.write_a, u.write_b)));
    }
    slot_count = static_cast<size_t>(max_slot) + 1;
    slot_words = (slot_count + 63) / 64;

    const size_t count = blocks.size();
    std::vector<uint64_t> use_set(count * slot_words, 0);
    std::vector<uint64_t> def_set(count * slot_words, 0);
    live_in.assign(count * slot_words, 0);
    live_out.assign(count * slot_words, 0);

    auto set_bit = [this](std::vector<uint64_t>& v, size_t block, long slot) {
      v[block * slot_words + static_cast<size_t>(slot) / 64] |= (uint64_t(1) << (static_cast<size_t>(slot) % 64));
    };
    auto get_bit = [this](const std::vector<uint64_t>& v, size_t block, long slot) {
      return (v[block * slot_words + static_cast<size_t>(slot) / 64] >> (static_cast<size_t>(slot) % 64)) & 1;
    };

    for(size_t b = 0; b < count; ++b) {
      if(!blocks[b].reachable) {
        continue;
      }
      for(size_t k = blocks[b].first; k <= blocks[b].last; ++k) {
        const SlotUse u = SlotUseOf(instrs[k]);
        const long reads[2] = { u.read_a, u.read_b };
        for(long r : reads) {
          if(r >= 0 && !get_bit(def_set, b, r)) {
            set_bit(use_set, b, r);
          }
        }
        if(u.reads_all) {
          for(size_t w = 0; w < slot_words; ++w) {
            use_set[b * slot_words + w] |= ~def_set[b * slot_words + w];
          }
        }
        const long writes[2] = { u.write_a, u.write_b };
        for(long w : writes) {
          if(w >= 0) {
            set_bit(def_set, b, w);
          }
        }
      }
    }

    bool changed = true;
    while(changed) {
      changed = false;
      for(size_t k = rpo_order.size(); k-- > 0; ) {
        const size_t b = static_cast<size_t>(rpo_order[k]);
        for(size_t w = 0; w < slot_words; ++w) {
          uint64_t out = 0;
          for(int s : blocks[b].succs) {
            out |= live_in[static_cast<size_t>(s) * slot_words + w];
          }
          uint64_t in = use_set[b * slot_words + w] | (out & ~def_set[b * slot_words + w]);
          for(int h : blocks[b].handlers) {
            in |= live_in[static_cast<size_t>(h) * slot_words + w];
          }
          if(out != live_out[b * slot_words + w] || in != live_in[b * slot_words + w]) {
            live_out[b * slot_words + w] = out;
            live_in[b * slot_words + w] = in;
            changed = true;
          }
        }
      }
    }
  }

  bool Cfg::TestBit(const std::vector<uint64_t>& v, int block, long slot) const
  {
    // a slot nothing in the method names is never live
    if(!has_liveness || slot < 0 || static_cast<size_t>(slot) >= slot_count || block < 0) {
      return false;
    }
    return ((v[static_cast<size_t>(block) * slot_words + static_cast<size_t>(slot) / 64] >> (static_cast<size_t>(slot) % 64)) & 1) != 0;
  }

  bool Cfg::LiveIn(int block, long slot) const
  {
    return TestBit(live_in, block, slot);
  }

  bool Cfg::LiveOut(int block, long slot) const
  {
    return TestBit(live_out, block, slot);
  }

  bool Cfg::LiveBefore(size_t i, long slot) const
  {
    const int b = block_of[i];
    bool live = LiveOut(b, slot);
    bool handler_live = false;
    for(int h : blocks[b].handlers) {
      handler_live = handler_live || LiveIn(h, slot);
    }
    for(size_t k = blocks[b].last + 1; k-- > i; ) {
      const SlotUse u = SlotUseOf(instrs[k]);
      if(u.write_a == slot || u.write_b == slot) {
        live = false;
      }
      if(u.read_a == slot || u.read_b == slot || u.reads_all) {
        live = true;
      }
      live = live || handler_live;
    }
    return live;
  }


  std::vector<bool> Cfg::DeadLocalStores() const
  {
    std::vector<bool> dead(instrs.size(), false);
    if(!has_liveness || !well_formed) {
      return dead;
    }
    for(const Block& b : blocks) {
      if(!b.reachable) {
        continue;
      }
      std::vector<uint64_t> live(live_out.begin() + b.id * slot_words, live_out.begin() + (b.id + 1) * slot_words);
      std::vector<uint64_t> handler(slot_words, 0);
      for(int h : b.handlers) {
        for(size_t w = 0; w < slot_words; ++w) {
          handler[w] |= live_in[static_cast<size_t>(h) * slot_words + w];
        }
      }
      auto test = [&](const std::vector<uint64_t>& v, long slot) {
        return ((v[static_cast<size_t>(slot) / 64] >> (static_cast<size_t>(slot) % 64)) & 1) != 0;
      };
      for(size_t k = b.last + 1; k-- > b.first; ) {
        const SlotUse u = SlotUseOf(instrs[k]);
        const InstructionType t = instrs[k]->GetType();
        if((t == STOR_INT_VAR || t == STOR_FLOAT_VAR || t == STOR_FUNC_VAR) && u.write_a >= 0) {
          bool any = test(live, u.write_a) || test(handler, u.write_a);
          if(u.write_b >= 0) {
            any = any || test(live, u.write_b) || test(handler, u.write_b);
          }
          dead[k] = !any;
        }
        const long writes[2] = { u.write_a, u.write_b };
        for(long w : writes) {
          if(w >= 0) {
            live[static_cast<size_t>(w) / 64] &= ~(uint64_t(1) << (static_cast<size_t>(w) % 64));
          }
        }
        const long reads[2] = { u.read_a, u.read_b };
        for(long r : reads) {
          if(r >= 0) {
            live[static_cast<size_t>(r) / 64] |= (uint64_t(1) << (static_cast<size_t>(r) % 64));
          }
        }
        if(u.reads_all) {
          for(size_t w = 0; w < slot_words; ++w) {
            live[w] = ~uint64_t(0);
          }
        }
      }
    }
    return dead;
  }

  // ------------------- checks -------------------

  bool Cfg::Verify(std::string* why) const
  {
    auto bad = [why](const std::string& msg) {
      if(why) {
        *why = msg;
      }
      return false;
    };

    if(!well_formed) {
      return bad("not well formed: " + problem);
    }

    const size_t n = instrs.size();
    if(n == 0) {
      return blocks.empty() ? true : bad("blocks for an empty list");
    }

    // the blocks cover every instruction exactly once
    if(blocks.empty() || blocks[0].first != 0 || blocks.back().last != n - 1) {
      return bad("the blocks do not span the list");
    }
    for(size_t b = 0; b < blocks.size(); ++b) {
      if(blocks[b].id != static_cast<int>(b) || blocks[b].first > blocks[b].last) {
        return bad("block " + std::to_string(b) + " is malformed");
      }
      if(b + 1 < blocks.size() && blocks[b].last + 1 != blocks[b + 1].first) {
        return bad("blocks " + std::to_string(b) + " and " + std::to_string(b + 1) + " leave a gap or overlap");
      }
      for(size_t k = blocks[b].first; k <= blocks[b].last; ++k) {
        if(block_of[k] != static_cast<int>(b)) {
          return bad("instruction " + std::to_string(k) + " is not mapped to its block");
        }
        // a leader starts a block and nothing else does
        if(k > blocks[b].first && is_leader[k]) {
          return bad("a leader at instruction " + std::to_string(k) + " is inside block " + std::to_string(b));
        }
      }
      if(!is_leader[blocks[b].first]) {
        return bad("block " + std::to_string(b) + " does not start at a leader");
      }
    }

    // edges: symmetric, from the end of a block to the start of one, and each justified by the code
    for(const Block& b : blocks) {
      IntermediateInstruction* term = instrs[LastReal(b)];
      for(int s : b.succs) {
        if(!Has(blocks[s].preds, b.id)) {
          return bad("edge " + std::to_string(b.id) + "->" + std::to_string(s) + " is missing from the predecessors");
        }
        if(!is_leader[blocks[s].first]) {
          return bad("edge " + std::to_string(b.id) + "->" + std::to_string(s) + " arrives at a non-leader");
        }
      }
      for(int p : b.preds) {
        if(!Has(blocks[p].succs, b.id)) {
          return bad("predecessor " + std::to_string(p) + " of block " + std::to_string(b.id) + " has no edge to it");
        }
      }
      for(int s : b.normal) {
        bool justified = false;
        switch(term->GetType()) {
        case JMP:
          justified = s == BlockOfLabel(term->GetOperand()) || (term->GetOperand2() >= 0 && s == b.id + 1);
          break;
        case JMP_TABLE:
          justified = s == BlockOfLabel(term->GetOperand3());
          for(size_t k = LastReal(b) + 1; k <= b.last && !justified; ++k) {
            justified = s == BlockOfLabel(instrs[k]->GetOperand());
          }
          break;
        case RTRN:
          justified = false;
          break;
        default:
          justified = s == b.id + 1;
          break;
        }
        if(!justified) {
          return bad("edge " + std::to_string(b.id) + "->" + std::to_string(s) + " is not made by the block's last instruction");
        }
      }
      for(int h : b.handlers) {
        if(instrs[blocks[h].first]->GetType() != LBL || !Has(b.region, h)) {
          return bad("handler edge " + std::to_string(b.id) + "->" + std::to_string(h) + " does not reach a handler label of its region");
        }
      }
      for(int h : b.region) {
        if(!Has(b.handlers, h)) {
          return bad("block " + std::to_string(b.id) + " is in a try region but has no edge to its handler");
        }
      }
    }

    // dominators: a tree rooted at the entry, equal to an independent computation
    const size_t count = blocks.size();
    for(size_t b = 0; b < count; ++b) {
      if(!blocks[b].reachable) {
        if(blocks[b].idom != -1) {
          return bad("unreachable block " + std::to_string(b) + " has a dominator");
        }
        continue;
      }
      int x = static_cast<int>(b);
      size_t steps = 0;
      while(x != 0) {
        const int up = blocks[x].idom;
        if(up < 0 || up == x || ++steps > count) {
          return bad("the dominator chain of block " + std::to_string(b) + " does not reach the entry");
        }
        x = up;
      }
    }

    if(count <= 3000) {
      // dom(b) = {b} + the intersection of dom(p) over the reachable predecessors, to a fixed point
      const size_t words = (count + 63) / 64;
      std::vector<uint64_t> dom(count * words, ~uint64_t(0));
      for(size_t w = 0; w < words; ++w) {
        dom[w] = 0;
      }
      dom[0] |= 1;
      bool changed = true;
      while(changed) {
        changed = false;
        for(size_t b = 1; b < count; ++b) {
          if(!blocks[b].reachable) {
            continue;
          }
          std::vector<uint64_t> next(words, ~uint64_t(0));
          for(int p : blocks[b].preds) {
            if(!blocks[p].reachable) {
              continue;
            }
            for(size_t w = 0; w < words; ++w) {
              next[w] &= dom[static_cast<size_t>(p) * words + w];
            }
          }
          next[b / 64] |= uint64_t(1) << (b % 64);
          for(size_t w = 0; w < words; ++w) {
            if(next[w] != dom[b * words + w]) {
              dom[b * words + w] = next[w];
              changed = true;
            }
          }
        }
      }
      auto dominated_by = [&](size_t b, size_t d) {
        return ((dom[b * words + d / 64] >> (d % 64)) & 1) != 0;
      };
      auto dom_size = [&](size_t b) {
        size_t c = 0;
        for(size_t d = 0; d < count; ++d) {
          if(blocks[d].reachable && dominated_by(b, d)) {
            ++c;
          }
        }
        return c;
      };
      for(size_t b = 1; b < count; ++b) {
        if(!blocks[b].reachable) {
          continue;
        }
        // the immediate dominator is the strict dominator with the most dominators of its own
        const size_t want = dom_size(b) - 1;
        const int idom = blocks[b].idom;
        if(idom < 0 || !dominated_by(b, static_cast<size_t>(idom)) || dom_size(static_cast<size_t>(idom)) != want) {
          return bad("block " + std::to_string(b) + " has the wrong immediate dominator " + std::to_string(idom));
        }
        for(size_t d = 0; d < count; ++d) {
          if(blocks[d].reachable && Dominates(static_cast<int>(d), static_cast<int>(b)) != dominated_by(b, d)) {
            return bad("Dominates(" + std::to_string(d) + "," + std::to_string(b) + ") disagrees with the dataflow answer");
          }
        }
      }
    }

    // loops
    for(const Loop& loop : loops) {
      if(!loop.Contains(loop.header) || loop.latches.empty()) {
        return bad("a loop has no header or no latch");
      }
      for(int l : loop.latches) {
        if(!loop.Contains(l) || !Has(blocks[l].succs, loop.header) || !Dominates(loop.header, l)) {
          return bad("the header " + std::to_string(loop.header) + " does not dominate its latch " + std::to_string(l));
        }
      }
      for(int b : loop.blocks) {
        if(!blocks[b].reachable || !Dominates(loop.header, b)) {
          return bad("block " + std::to_string(b) + " of the loop at " + std::to_string(loop.header) + " is not dominated by the header");
        }
      }
      for(const Exit& e : loop.exits) {
        if(!loop.Contains(e.from) || loop.Contains(e.to) || !Has(blocks[e.from].succs, e.to)) {
          return bad("a loop exit is not an edge out of the loop");
        }
      }
      if(loop.preheader >= 0) {
        const Block& pre = blocks[loop.preheader];
        if(loop.Contains(loop.preheader) || pre.normal.size() != 1 || pre.normal[0] != loop.header) {
          return bad("the preheader of the loop at " + std::to_string(loop.header) + " is not a lone entry");
        }
        const bool at_jump = loop.preheader_at == LastReal(pre) && IsUncondJump(instrs[loop.preheader_at]);
        const bool at_header = loop.preheader_at == blocks[loop.header].first && pre.last + 1 == loop.preheader_at;
        if(!at_jump && !at_header) {
          return bad("the preheader insertion point of the loop at " + std::to_string(loop.header) + " is not at the entry");
        }
      }
    }

    return true;
  }

  std::string Cfg::Describe() const
  {
    std::ostringstream out;
    for(const Block& b : blocks) {
      out << "B" << b.id << " [" << b.first << ".." << b.last << "]" << (b.reachable ? "" : " unreachable") << " idom=" << b.idom << " succ:";
      for(int s : b.succs) {
        out << " " << s << (Has(b.handlers, s) && !Has(b.normal, s) ? "h" : "");
      }
      out << "\n";
    }
    for(const Loop& l : loops) {
      out << "loop header=B" << l.header << " depth=" << l.depth << " blocks:";
      for(int b : l.blocks) {
        out << " " << b;
      }
      out << " preheader=" << l.preheader << "\n";
    }
    return out.str();
  }
}
