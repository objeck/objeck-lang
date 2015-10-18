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
  
#ifdef _WIN32
  __declspec(dllexport) 
#endif
  // core
  void sdl_init(VMContext& context) {
    const int flag = APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_Init(flag));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_quit(VMContext& context) {
    SDL_Quit();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void update_window_surface(VMContext& context) {
    long* src_window = APITools_GetObjectValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_UpdateWindowSurface((SDL_Window*)src_window[0]));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  // window
  void blit_surface(VMContext& context) {
    long* src_obj = APITools_GetObjectValue(context, 1);
    long* srcrect_obj = APITools_GetObjectValue(context, 2);
    long* dst_obj = APITools_GetObjectValue(context, 3);
    long* dstrect_obj = APITools_GetObjectValue(context, 4);

    const int value = SDL_BlitSurface((SDL_Surface*)src_obj[0], srcrect_obj ? (SDL_Rect*)srcrect_obj[0] : NULL,
                                      (SDL_Surface*)dst_obj[0], dstrect_obj ? (SDL_Rect*)dstrect_obj[0] : NULL);

    APITools_SetIntValue(context, 0, value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_poll_event(VMContext& context) {
    long* value = APITools_GetObjectValue(context, 1);
    SDL_Event* event = (SDL_Event*)value[0];
    const int foo = SDL_PollEvent(event);
    APITools_SetIntValue(context, 0, foo);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  // window
  void sdl_create_window(VMContext& context) {
    const wstring wtitle(APITools_GetStringValue(context, 1));
    int x = APITools_GetIntValue(context, 2);
    int y = APITools_GetIntValue(context, 3);
    int w = APITools_GetIntValue(context, 4);
    int h = APITools_GetIntValue(context, 5);
    Uint32 flags = (Uint32)APITools_GetIntValue(context, 6);

    const string title(wtitle.begin(), wtitle.end());
    SDL_Window* window = SDL_CreateWindow(title.c_str(), x, y, w, h, flags);

    APITools_SetIntValue(context, 0, (long)window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_destroy_window(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    SDL_DestroyWindow(window);
  }

  // surface
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_get_window_surface(VMContext& context) {
    long* window_obj = (long*)APITools_GetObjectValue(context, 1);
    APITools_SetIntValue(context, 0, (long)SDL_GetWindowSurface((SDL_Window*)window_obj[0]));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_load_bmp(VMContext& context) {
    const wstring wname = APITools_GetStringValue(context, 1);
    SDL_Surface* foo = SDL_LoadBMP(UnicodeToBytes(wname).c_str());
    APITools_SetIntValue(context, 0, (long)foo);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_free_surface(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 0);
    SDL_FreeSurface(surface);
  }

  // event
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_new(VMContext& context) {
    void* foo = calloc(1, sizeof(SDL_Event));
    APITools_SetIntValue(context, 0, (long)foo);
  }
}
