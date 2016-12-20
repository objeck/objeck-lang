/***************************************************************************
 * SDL support for Objeck
 *
 * Copyright (c) 2015, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck Team nor the names of its
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

#include <SDL.h>
#include <stdio.h>
#include "../../vm/lib_api.h"

using namespace std;

extern "C" {
  //
  // initialize library
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void load_lib() {
  }
  
  //
  // release library
  //
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  void unload_lib() {
  }
  
  //
  // SDL Core
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_init(VMContext& context) {
    const int flags = APITools_GetIntValue(context, 1);
    const int return_value = SDL_Init(flags);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_init_sub_system(VMContext& context) {
    const int flags = APITools_GetIntValue(context, 1);
    const int return_value = SDL_InitSubSystem(flags);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_quit_sub_system(VMContext& context) {
    const int flags = APITools_GetIntValue(context, 0);
    SDL_QuitSubSystem(flags);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_was_init(VMContext& context) {
    const int flags = APITools_GetIntValue(context, 1);
    const int return_value = SDL_WasInit(flags);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_quit(VMContext& context) {
    SDL_Quit();
  }

  //
  // Hints
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_hints_set_hint_with_priority(VMContext& context) {
    const wstring w_name = APITools_GetStringValue(context, 1);
    const string name(w_name.begin(), w_name.end());

    const wstring w_value = APITools_GetStringValue(context, 2);
    const string value(w_value.begin(), w_value.end());

    const int priority = APITools_GetIntValue(context, 3);
    
    const int return_value = SDL_SetHintWithPriority(name.c_str(), value.c_str(), (SDL_HintPriority)priority);    
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_hints_set_hint(VMContext& context) {
    const wstring w_name = APITools_GetStringValue(context, 1);
    const string name(w_name.begin(), w_name.end());

    const wstring w_value = APITools_GetStringValue(context, 2);
    const string value(w_value.begin(), w_value.end());

    const int return_value = SDL_SetHint(name.c_str(), value.c_str());
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_hints_get_hint(VMContext& context) {
    const wstring w_name = APITools_GetStringValue(context, 1);
    const string name(w_name.begin(), w_name.end());
    const string return_value = SDL_GetHint(name.c_str());

    const wstring w_return_value(return_value.begin(), return_value.end());
    APITools_SetStringValue(context, 0, w_return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_hints_clear(VMContext& context) {
    SDL_ClearHints();
  }
}