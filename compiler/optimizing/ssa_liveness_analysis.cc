/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ssa_liveness_analysis.h"
#include "nodes.h"

namespace art {

void SsaLivenessAnalysis::Analyze() {
  LinearizeGraph();
  NumberInstructions();
  ComputeLiveness();
}

static bool IsLoopExit(HLoopInformation* current, HLoopInformation* to) {
  // `to` is either not part of a loop, or `current` is an inner loop of `to`.
  return to == nullptr || (current != to && current->IsIn(*to));
}

static bool IsLoop(HLoopInformation* info) {
  return info != nullptr;
}

static bool InSameLoop(HLoopInformation* first_loop, HLoopInformation* second_loop) {
  return first_loop == second_loop;
}

static bool IsInnerLoop(HLoopInformation* outer, HLoopInformation* inner) {
  return (inner != outer)
      && (inner != nullptr)
      && (outer != nullptr)
      && inner->IsIn(*outer);
}

static void VisitBlockForLinearization(HBasicBlock* block,
                                       GrowableArray<HBasicBlock*>* order,
                                       ArenaBitVector* visited) {
  if (visited->IsBitSet(block->GetBlockId())) {
    return;
  }
  visited->SetBit(block->GetBlockId());
  size_t number_of_successors = block->GetSuccessors().Size();
  if (number_of_successors == 0) {
    // Nothing to do.
  } else if (number_of_successors == 1) {
    VisitBlockForLinearization(block->GetSuccessors().Get(0), order, visited);
  } else {
    DCHECK_EQ(number_of_successors, 2u);
    HBasicBlock* first_successor = block->GetSuccessors().Get(0);
    HBasicBlock* second_successor = block->GetSuccessors().Get(1);
    HLoopInformation* my_loop = block->GetLoopInformation();
    HLoopInformation* first_loop = first_successor->GetLoopInformation();
    HLoopInformation* second_loop = second_successor->GetLoopInformation();

    if (!IsLoop(my_loop)) {
      // Nothing to do. Current order is fine.
    } else if (IsLoopExit(my_loop, second_loop) && InSameLoop(my_loop, first_loop)) {
      // Visit the loop exit first in post order.
      std::swap(first_successor, second_successor);
    } else if (IsInnerLoop(my_loop, first_loop) && !IsInnerLoop(my_loop, second_loop)) {
      // Visit the inner loop last in post order.
      std::swap(first_successor, second_successor);
    }
    VisitBlockForLinearization(first_successor, order, visited);
    VisitBlockForLinearization(second_successor, order, visited);
  }
  order->Add(block);
}

class HLinearOrderIterator : public ValueObject {
 public:
  explicit HLinearOrderIterator(const GrowableArray<HBasicBlock*>& post_order)
      : post_order_(post_order), index_(post_order.Size()) {}

  bool Done() const { return index_ == 0; }
  HBasicBlock* Current() const { return post_order_.Get(index_ -1); }
  void Advance() { --index_; DCHECK_GE(index_, 0U); }

 private:
  const GrowableArray<HBasicBlock*>& post_order_;
  size_t index_;

  DISALLOW_COPY_AND_ASSIGN(HLinearOrderIterator);
};

class HLinearPostOrderIterator : public ValueObject {
 public:
  explicit HLinearPostOrderIterator(const GrowableArray<HBasicBlock*>& post_order)
      : post_order_(post_order), index_(0) {}

  bool Done() const { return index_ == post_order_.Size(); }
  HBasicBlock* Current() const { return post_order_.Get(index_); }
  void Advance() { ++index_; }

 private:
  const GrowableArray<HBasicBlock*>& post_order_;
  size_t index_;

  DISALLOW_COPY_AND_ASSIGN(HLinearPostOrderIterator);
};

void SsaLivenessAnalysis::LinearizeGraph() {
  // For simplicity of the implementation, we create post linear order. The order for
  // computing live ranges is the reverse of that order.
  ArenaBitVector visited(graph_.GetArena(), graph_.GetBlocks().Size(), false);
  VisitBlockForLinearization(graph_.GetEntryBlock(), &linear_post_order_, &visited);
}

void SsaLivenessAnalysis::NumberInstructions() {
  int ssa_index = 0;
  size_t lifetime_position = 0;
  // Each instruction gets an individual lifetime position, and a block gets a lifetime
  // start and end position. Non-phi instructions have a distinct lifetime position than
  // the block they are in. Phi instructions have the lifetime start of their block as
  // lifetime position
  for (HLinearOrderIterator it(linear_post_order_); !it.Done(); it.Advance()) {
    HBasicBlock* block = it.Current();
    block->SetLifetimeStart(++lifetime_position);

    for (HInstructionIterator it(block->GetPhis()); !it.Done(); it.Advance()) {
      HInstruction* current = it.Current();
      if (current->HasUses()) {
        instructions_from_ssa_index_.Add(current);
        current->SetSsaIndex(ssa_index++);
        current->SetLiveInterval(new (graph_.GetArena()) LiveInterval(graph_.GetArena()));
      }
      current->SetLifetimePosition(lifetime_position);
    }

    for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
      HInstruction* current = it.Current();
      if (current->HasUses()) {
        instructions_from_ssa_index_.Add(current);
        current->SetSsaIndex(ssa_index++);
        current->SetLiveInterval(new (graph_.GetArena()) LiveInterval(graph_.GetArena()));
      }
      current->SetLifetimePosition(++lifetime_position);
    }

    block->SetLifetimeEnd(++lifetime_position);
  }
  number_of_ssa_values_ = ssa_index;
}

void SsaLivenessAnalysis::ComputeLiveness() {
  for (HLinearOrderIterator it(linear_post_order_); !it.Done(); it.Advance()) {
    HBasicBlock* block = it.Current();
    block_infos_.Put(
        block->GetBlockId(),
        new (graph_.GetArena()) BlockInfo(graph_.GetArena(), *block, number_of_ssa_values_));
  }

  // Compute the live ranges, as well as the initial live_in, live_out, and kill sets.
  // This method does not handle backward branches for the sets, therefore live_in
  // and live_out sets are not yet correct.
  ComputeLiveRanges();

  // Do a fixed point calculation to take into account backward branches,
  // that will update live_in of loop headers, and therefore live_out and live_in
  // of blocks in the loop.
  ComputeLiveInAndLiveOutSets();
}

class InstructionBitVectorIterator : public ValueObject {
 public:
  InstructionBitVectorIterator(const BitVector& vector,
                               const GrowableArray<HInstruction*>& instructions)
        : instructions_(instructions),
          iterator_(BitVector::Iterator(&vector)),
          current_bit_index_(iterator_.Next()) {}

  bool Done() const { return current_bit_index_ == -1; }
  HInstruction* Current() const { return instructions_.Get(current_bit_index_); }
  void Advance() {
    current_bit_index_ = iterator_.Next();
  }

 private:
  const GrowableArray<HInstruction*> instructions_;
  BitVector::Iterator iterator_;
  int32_t current_bit_index_;

  DISALLOW_COPY_AND_ASSIGN(InstructionBitVectorIterator);
};

void SsaLivenessAnalysis::ComputeLiveRanges() {
  // Do a post order visit, adding inputs of instructions live in the block where
  // that instruction is defined, and killing instructions that are being visited.
  for (HLinearPostOrderIterator it(linear_post_order_); !it.Done(); it.Advance()) {
    HBasicBlock* block = it.Current();

    BitVector* kill = GetKillSet(*block);
    BitVector* live_in = GetLiveInSet(*block);

    // Set phi inputs of successors of this block corresponding to this block
    // as live_in.
    for (size_t i = 0, e = block->GetSuccessors().Size(); i < e; ++i) {
      HBasicBlock* successor = block->GetSuccessors().Get(i);
      live_in->Union(GetLiveInSet(*successor));
      size_t phi_input_index = successor->GetPredecessorIndexOf(block);
      for (HInstructionIterator it(successor->GetPhis()); !it.Done(); it.Advance()) {
        HInstruction* input = it.Current()->InputAt(phi_input_index);
        live_in->SetBit(input->GetSsaIndex());
      }
    }

    // Add a range that covers this block to all instructions live_in because of successors.
    for (InstructionBitVectorIterator it(*live_in, instructions_from_ssa_index_);
         !it.Done();
         it.Advance()) {
      it.Current()->GetLiveInterval()->AddRange(block->GetLifetimeStart(), block->GetLifetimeEnd());
    }

    for (HBackwardInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
      HInstruction* current = it.Current();
      if (current->HasSsaIndex()) {
        // Kill the instruction and shorten its interval.
        kill->SetBit(current->GetSsaIndex());
        live_in->ClearBit(current->GetSsaIndex());
        current->GetLiveInterval()->SetFrom(current->GetLifetimePosition());
      }

      // All inputs of an instruction must be live.
      for (size_t i = 0, e = current->InputCount(); i < e; ++i) {
        HInstruction* input = current->InputAt(i);
        DCHECK(input->HasSsaIndex());
        live_in->SetBit(input->GetSsaIndex());
        input->GetLiveInterval()->AddUse(current);
      }

      if (current->HasEnvironment()) {
        // All instructions in the environment must be live.
        GrowableArray<HInstruction*>* environment = current->GetEnvironment()->GetVRegs();
        for (size_t i = 0, e = environment->Size(); i < e; ++i) {
          HInstruction* instruction = environment->Get(i);
          if (instruction != nullptr) {
            DCHECK(instruction->HasSsaIndex());
            live_in->SetBit(instruction->GetSsaIndex());
            instruction->GetLiveInterval()->AddUse(current);
          }
        }
      }
    }

    // Kill phis defined in this block.
    for (HInstructionIterator it(block->GetPhis()); !it.Done(); it.Advance()) {
      HInstruction* current = it.Current();
      if (current->HasSsaIndex()) {
        kill->SetBit(current->GetSsaIndex());
        live_in->ClearBit(current->GetSsaIndex());
      }
    }

    if (block->IsLoopHeader()) {
      HBasicBlock* back_edge = block->GetLoopInformation()->GetBackEdges().Get(0);
      // For all live_in instructions at the loop header, we need to create a range
      // that covers the full loop.
      for (InstructionBitVectorIterator it(*live_in, instructions_from_ssa_index_);
           !it.Done();
           it.Advance()) {
        it.Current()->GetLiveInterval()->AddLoopRange(block->GetLifetimeStart(),
                                                      back_edge->GetLifetimeEnd());
      }
    }
  }
}

void SsaLivenessAnalysis::ComputeLiveInAndLiveOutSets() {
  bool changed;
  do {
    changed = false;

    for (HPostOrderIterator it(graph_); !it.Done(); it.Advance()) {
      const HBasicBlock& block = *it.Current();

      // The live_in set depends on the kill set (which does not
      // change in this loop), and the live_out set.  If the live_out
      // set does not change, there is no need to update the live_in set.
      if (UpdateLiveOut(block) && UpdateLiveIn(block)) {
        changed = true;
      }
    }
  } while (changed);
}

bool SsaLivenessAnalysis::UpdateLiveOut(const HBasicBlock& block) {
  BitVector* live_out = GetLiveOutSet(block);
  bool changed = false;
  // The live_out set of a block is the union of live_in sets of its successors.
  for (size_t i = 0, e = block.GetSuccessors().Size(); i < e; ++i) {
    HBasicBlock* successor = block.GetSuccessors().Get(i);
    if (live_out->Union(GetLiveInSet(*successor))) {
      changed = true;
    }
  }
  return changed;
}


bool SsaLivenessAnalysis::UpdateLiveIn(const HBasicBlock& block) {
  BitVector* live_out = GetLiveOutSet(block);
  BitVector* kill = GetKillSet(block);
  BitVector* live_in = GetLiveInSet(block);
  // If live_out is updated (because of backward branches), we need to make
  // sure instructions in live_out are also in live_in, unless they are killed
  // by this block.
  return live_in->UnionIfNotIn(live_out, kill);
}

}  // namespace art
