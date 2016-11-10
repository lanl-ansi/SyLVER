#pragma once

#include "ssids/cpu/SymbolicNode.hxx"

#if defined(SPLDLT_USE_STARPU)
#include <starpu.h>
#endif

using namespace spral::ssids::cpu;

namespace spldlt {

   struct SymbolicSNode : SymbolicNode {
      int sa; // index of first column in node (pivotal order)
      int en; // index of last column in node (pivotal order)
      int least_desc; // least descendants, to allow easy walk of subtrees
#if defined(SPLDLT_USE_STARPU)
      starpu_data_handle_t hdl;
      std::vector<starpu_data_handle_t> handles; // array containing the block handles
#endif
   };
   
}