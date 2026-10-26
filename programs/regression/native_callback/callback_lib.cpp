/***************************************************************************
 * A native library that calls back into Objeck, for run_vm_flag_tests.py
 * (section 18). Built there with the platform's C++ compiler as
 * libobjk_callback_test; never shipped.
 *
 * No library in the repository calls back into Objeck, so nothing else
 * exercises APITools_CallMethod from a native library. callback_bounce calls
 * the Objeck function VmNativeCallbackDepth:Bounce:i, with the depth plus one,
 * and Bounce calls callback_bounce again, so with no limit on the Objeck side
 * the two recurse through the native boundary until the VM stops them. It must
 * stop them at its call-stack limit with a clean error, as it stops a
 * recursion that never leaves Objeck.
 ***************************************************************************/

#include "../../../core/vm/lib_api.h"

extern "C" {
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib(VMContext& context) {
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void unload_lib() {
  }

  // args[0]: an IntRef holding the depth so far; holds Bounce's result after
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void callback_bounce(VMContext& context) {
    const size_t depth = APITools_GetIntValue(context, 0);
    // the callee's one parameter goes on the operand stack, as for any call
    context.op_stack[(*context.stack_pos)++] = depth + 1;
    APITools_CallMethod(context, nullptr, L"VmNativeCallbackDepth:Bounce:i,");
    // Bounce returns an Int, left on the operand stack
    const size_t result = context.op_stack[--(*context.stack_pos)];
    APITools_SetIntValue(context, 0, result);
  }
}
