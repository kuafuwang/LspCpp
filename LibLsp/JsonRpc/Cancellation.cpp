//===--- Cancellation.cpp -----------------------------------------*-C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Cancellation.h"
#include <atomic>

namespace lsp {
  
// We don't want a cancelable scope to "shadow" an enclosing one.
struct CancelState {
  std::shared_ptr<std::atomic<int>> cancelled;
  const CancelState *parent= nullptr;
};
static Key<CancelState> g_stateKey;

std::pair<Context, Canceler> cancelableTask(int Reason) {
  assert(Reason != 0 && "Can't detect cancellation if Reason is zero");
  CancelState State;
  State.cancelled = std::make_shared<std::atomic<int>>();
  State.parent = Context::current().get(g_stateKey);
  return {
      Context::current().derive(g_stateKey, State),
      [Reason, Flag(State.cancelled)] { *Flag = Reason; },
  };
}

int isCancelled(const Context &ctx) {
  for (const CancelState *state = ctx.get(g_stateKey); state != nullptr;
       state = state->parent)
    if (const int reason = state->cancelled->load())
      return reason;
  return 0;
}


} // namespace lsp
