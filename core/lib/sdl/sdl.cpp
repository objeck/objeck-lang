/***************************************************************************
 * SDL support for Objeck
 *
 * Copyright (c) 2015-2019, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *
 * - Redistributions of source code must retain the above copyright 
 * notice, this list of conditions and the following disclaimer.
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

#ifdef _OSX
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include "SDL2_gfxPrimitives.h"
#include "SDL2_rotozoom.h"
#else
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include "SDL2_gfxPrimitives.h"
#include "SDL2_rotozoom.h"
#endif
#include <stdio.h>
#include "../../vm/lib_api.h"
#include "../../shared/sys.h"

// OpenGL. Must come AFTER lib_api.h on Windows: lib_api.h pulls in
// core/vm/common.h, which includes <windows.h> and relies on it dragging in
// winsock for fd_set / FD_ISSET, while SDL_opengl.h defines WIN32_LEAN_AND_MEAN
// before including <windows.h> itself -- and that macro suppresses winsock, so
// putting this first breaks common.h with "FD_ISSET: identifier not found".
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>   // PFNGL*PROC typedefs for the GL 2.0+ loader
#include <map>
#include <string>
#include <vector>

#define POLY_MAX 1024

extern "C" {
  void sdl_color_raw_read(SDL_Color* color, size_t* color_obj);
  void sdl_color_raw_write(SDL_Color* color, size_t* color_obj);

  void sdl_point_raw_read(SDL_Point* point, size_t* point_obj);
  void sdl_point_raw_write(SDL_Point* point, size_t* point_obj);

  void sdl_rect_raw_read(SDL_Rect* rect, size_t* rect_obj);
  void sdl_rect_raw_write(SDL_Rect* rect, size_t* rect_obj);

  void sdl_pixel_format_raw_read(SDL_PixelFormat* pixel_format, size_t* pixel_format_obj);
  void sdl_pixel_format_raw_write(SDL_PixelFormat* pixel_format, size_t* pixel_format_obj);

  void sdl_palette_raw_read(SDL_Palette* palette_format, size_t* palette_format_obj);
  void sdl_palette_raw_write(SDL_Palette* palette_format, size_t* palette_format_obj);

  void sdl_display_mode_raw_read(SDL_DisplayMode* mode, size_t* display_mode_obj);
  void sdl_display_mode_raw_write(SDL_DisplayMode* mode, size_t* display_mode_obj);

  void sdl_gamecontroller_button_bind_read(struct SDL_GameControllerButtonBind* button_bind, size_t* button_bind_obj);


  //
  // initialize library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib(VMContext& context) {
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
  void sdl_core_gl_get_swap_interval(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GL_GetSwapInterval());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_get_error(VMContext& context) {
    const  std::string return_value = SDL_GetError();
    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_clear_error(VMContext& context) {
    SDL_ClearError();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_set_swap_interval(VMContext& context) {
    const int interval = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GL_SetSwapInterval(interval));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_get_current(VMContext& context) {
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    APITools_SetIntValue(context, 0, (size_t)window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_get_attribute(VMContext& context) {
    const int attr = (int)APITools_GetIntValue(context, 1);
    
    int value;
    const int return_value = SDL_GL_GetAttribute((SDL_GLattr)attr, &value);

    APITools_SetIntValue(context, 2, value);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_set_attribute(VMContext& context) {
    const int attr = (int)APITools_GetIntValue(context, 1);
    const int value = (int)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_GL_SetAttribute((SDL_GLattr)attr, value));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_reset_attributes(VMContext& context) {
    SDL_GL_ResetAttributes();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_extension_supported(VMContext& context) {
    const std::wstring w_extension = APITools_GetStringValue(context, 1);
    const  std::string extension = UnicodeToBytes(w_extension);
    const int return_value = SDL_GL_ExtensionSupported(extension.c_str());
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_load_library(VMContext& context) {
    const wchar_t* w_path = APITools_GetStringValue(context, 1);
    if(w_path) {
      const  std::string path = UnicodeToBytes(w_path);
      APITools_SetIntValue(context, 0, SDL_GL_LoadLibrary(path.c_str()));
    }
    else {
      APITools_SetIntValue(context, 0, SDL_GL_LoadLibrary(nullptr ));
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_unload_library(VMContext& context) {
    SDL_GL_UnloadLibrary();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_init(VMContext& context) {
    const int flags = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_Init(flags));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_init_sub_system(VMContext& context) {
    const int flags = (int)APITools_GetIntValue(context, 1);
    const int return_value = (int)SDL_InitSubSystem(flags);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_quit_sub_system(VMContext& context) {
    const int flags = (int)APITools_GetIntValue(context, 0);
    SDL_QuitSubSystem(flags);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_was_init(VMContext& context) {
    const int flags = (int)APITools_GetIntValue(context, 1);
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
    const std::wstring w_name = APITools_GetStringValue(context, 1);
    const  std::string name = UnicodeToBytes(w_name);

    const std::wstring w_value = APITools_GetStringValue(context, 2);
    const  std::string value = UnicodeToBytes(w_value);

    const int priority = (int)APITools_GetIntValue(context, 3);

    const int return_value = SDL_SetHintWithPriority(name.c_str(), value.c_str(), (SDL_HintPriority)priority);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_hints_set_hint(VMContext& context) {
    const std::wstring w_name = APITools_GetStringValue(context, 1);
    const  std::string name = UnicodeToBytes(w_name);

    const std::wstring w_value = APITools_GetStringValue(context, 2);
    const  std::string value = UnicodeToBytes(w_value);

    const int return_value = SDL_SetHint(name.c_str(), value.c_str());
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_hints_get_hint(VMContext& context) {
    const std::wstring w_name = APITools_GetStringValue(context, 1);
    const  std::string name = UnicodeToBytes(w_name);
    const  std::string return_value = SDL_GetHint(name.c_str());

    const std::wstring w_return_value(return_value.begin(), return_value.end());
    APITools_SetStringValue(context, 0, w_return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_hints_get_hint_boolean(VMContext& context) {
    const std::wstring w_name = APITools_GetStringValue(context, 1);
    const  std::string name = UnicodeToBytes(w_name);

    const SDL_bool value = (SDL_bool)APITools_GetIntValue(context, 2);

    APITools_SetIntValue(context, 0, SDL_GetHintBoolean(name.c_str(), value));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_hints_clear(VMContext& context) {
    SDL_ClearHints();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_creatergb(VMContext& context) {
    const Uint32 flags = (int)APITools_GetIntValue(context, 1);
    const int width = (int)APITools_GetIntValue(context, 2);
    const int height = (int)APITools_GetIntValue(context, 3);
    const int depth = (int)APITools_GetIntValue(context, 4);
    const Uint32 Rmask = (int)APITools_GetIntValue(context, 5);
    const Uint32 Gmask = (int)APITools_GetIntValue(context, 6);
    const Uint32 Bmask = (int)APITools_GetIntValue(context, 7);
    const Uint32 Amask = (int)APITools_GetIntValue(context, 8);

    SDL_Surface* surface = SDL_CreateRGBSurface(flags, width, height, depth, Rmask, Gmask, Bmask, Amask);
    APITools_SetIntValue(context, 0, (size_t)surface);
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_free(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 0);
    SDL_FreeSurface(surface);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_get_pixel_format(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
	  size_t* pixel_format_obj = APITools_GetObjectValue(context, 0);
	  sdl_pixel_format_raw_read(surface->format, pixel_format_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_duplicate(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)SDL_ConvertSurface(surface, surface->format, 0));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_get_w(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)surface->w);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_get_h(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)surface->h);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_get_pitch(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)surface->pitch);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_create_texture(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    size_t* renderer_obj = APITools_GetObjectValue(context, 2);
    SDL_Renderer* renderer = (SDL_Renderer*)renderer_obj[0];
    APITools_SetIntValue(context, 0, (size_t)SDL_CreateTextureFromSurface(renderer, surface));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_set_palette(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    const size_t* palette_obj = APITools_GetObjectValue(context, 2);
    SDL_Palette* palette = palette_obj ? (SDL_Palette*)palette_obj[0] : nullptr ;

    APITools_SetIntValue(context, 0, SDL_SetSurfacePalette(surface, palette));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_lock(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int return_value = SDL_LockSurface(surface);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_unlock(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 0);
    SDL_UnlockSurface(surface);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_pixels(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    
    size_t* pixel_obj= APITools_CreateObject(context, L"Game.SDL2.PixelData");
    pixel_obj[0] = (size_t)surface->pixels;
    pixel_obj[1] = (size_t)surface->pitch;
    pixel_obj[2] = (size_t)surface->h;
    APITools_SetObjectValue(context, 0, pixel_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_loadbmp(VMContext& context) {
    const std::wstring w_file = APITools_GetStringValue(context, 1);
    const  std::string file = UnicodeToBytes(w_file);

    SDL_Surface* surface = SDL_LoadBMP(file.c_str());
    APITools_SetIntValue(context, 0, (size_t)surface);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_savebmp(VMContext& context) {
    const size_t* surface_obj = APITools_GetObjectValue(context, 1);
    SDL_Surface* surface = surface_obj ? (SDL_Surface*)surface_obj[0] : nullptr ;
    const std::wstring w_file = APITools_GetStringValue(context, 2);
    const  std::string file = UnicodeToBytes(w_file);

    APITools_SetIntValue(context, 0, SDL_SaveBMP(surface, file.c_str()));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_setrle(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int flag = (int)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetSurfaceRLE(surface, flag));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_set_color_key(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int flag = (int)APITools_GetIntValue(context, 2);
    const int key = (int)APITools_GetIntValue(context, 3);
    APITools_SetIntValue(context, 0, SDL_SetColorKey(surface, flag, key));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_get_color_key(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    
    Uint32 key;
    const int return_value = SDL_GetColorKey(surface, &key);

    APITools_SetIntValue(context, 2, key);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_set_color_mod(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int r = (int)APITools_GetIntValue(context, 2);
    const int g = (int)APITools_GetIntValue(context, 3);
    const int b = (int)APITools_GetIntValue(context, 4);
    APITools_SetIntValue(context, 0, SDL_SetSurfaceColorMod(surface, r, g, b));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_get_color_mod(VMContext& context) {
    Uint8 r, g, b;
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GetSurfaceColorMod(surface, &r, &g, &b));
    APITools_SetIntValue(context, 2, r);
    APITools_SetIntValue(context, 3, g);
    APITools_SetIntValue(context, 4, b);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_set_alpha_mod(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int alpha = (int)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetSurfaceAlphaMod(surface, alpha));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_get_alpha_mod(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    
    Uint8 alpha;
    const int return_value = SDL_GetSurfaceAlphaMod(surface, &alpha);

    APITools_SetIntValue(context, 2, alpha);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_set_blend_mode(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const SDL_BlendMode blendMode = (SDL_BlendMode)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetSurfaceBlendMode(surface, blendMode));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_get_blend_mode(VMContext& context) {
    
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    SDL_BlendMode blendMode;
    const int return_value = SDL_GetSurfaceBlendMode(surface, &blendMode);

    APITools_SetIntValue(context, 2, blendMode);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_get_clip_rect(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 0);
    size_t* rect_obj = APITools_GetObjectValue(context, 1);
    
    SDL_Rect rect;
    SDL_GetClipRect(surface, &rect);
    sdl_rect_raw_read(&rect, rect_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_set_clip_rect(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    size_t* rect_obj = APITools_GetObjectValue(context, 2);
    
    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);

    APITools_SetIntValue(context, 0, SDL_SetClipRect(surface, rect_obj ? &rect : nullptr ));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_convert(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    size_t* fmt_obj = APITools_GetObjectValue(context, 2);
    const int flags = (int)APITools_GetIntValue(context, 3);

    SDL_PixelFormat fmt;
    sdl_pixel_format_raw_write(&fmt, fmt_obj);

    APITools_SetIntValue(context, 0, (size_t)SDL_ConvertSurface(surface, fmt_obj ? &fmt : nullptr , flags));
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_convert_format(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int pixel_format = (int)APITools_GetIntValue(context, 2);
    const int flags = (int)APITools_GetIntValue(context, 3);

    APITools_SetIntValue(context, 0, (size_t)SDL_ConvertSurfaceFormat(surface, pixel_format, flags));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_rotozoom(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const double angle = APITools_GetFloatValue(context, 2);
    const double zoom = APITools_GetFloatValue(context, 3);
    const int flags = (int)APITools_GetIntValue(context, 4);

    const size_t return_value = (size_t)rotozoomSurface(surface, angle, zoom, flags);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_rotozoom_size(VMContext& context) {
    const int width = (int)APITools_GetIntValue(context, 0);
    const int height = (int)APITools_GetIntValue(context, 1);
    const double angle = APITools_GetFloatValue(context, 2);
    const double zoom = APITools_GetFloatValue(context, 3);

    int dstwidth; int dstheight;
    rotozoomSurfaceSize(width, height, angle, zoom, &dstwidth, &dstheight);

    APITools_SetIntValue(context, 4, dstwidth);
    APITools_SetIntValue(context, 5, dstheight);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_rotozoom_size_xy(VMContext& context) {
    const int width = (int)APITools_GetIntValue(context, 0);
    const int height = (int)APITools_GetIntValue(context, 1);
    const double angle = APITools_GetFloatValue(context, 2);
    const double zoomx = APITools_GetFloatValue(context, 3);
    const double zoomy = APITools_GetFloatValue(context, 4);

    int dstwidth; int dstheight;
    rotozoomSurfaceSizeXY(width, height, angle, zoomx, zoomy, &dstwidth, &dstheight);

    APITools_SetIntValue(context, 5, dstwidth);
    APITools_SetIntValue(context, 6, dstheight);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_zoom_size(VMContext& context) {
    const int width = (int)APITools_GetIntValue(context, 0);
    const int height = (int)APITools_GetIntValue(context, 1);
    const double zoomx = APITools_GetFloatValue(context, 2);
    const double zoomy = APITools_GetFloatValue(context, 3);

    int dstwidth; int dstheight;
    zoomSurfaceSize(width, height, zoomx, zoomy, &dstwidth, &dstheight);

    APITools_SetIntValue(context, 4, dstwidth);
    APITools_SetIntValue(context, 5, dstheight);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_zoom(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const double zoomx = APITools_GetFloatValue(context, 2);
    const double zoomy = APITools_GetFloatValue(context, 3);
    const int flags = (int)APITools_GetIntValue(context, 4);

    const size_t return_value = (size_t)zoomSurface(surface, zoomx, zoomy, flags);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_shrink(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int factorx = (int)APITools_GetIntValue(context, 2);
    const int factory = (int)APITools_GetIntValue(context, 3);

    const size_t return_value = (size_t)shrinkSurface(surface, factorx, factory);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_rotate_90degrees(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int numClockwiseTurns = (int)APITools_GetIntValue(context, 2);

    const size_t return_value = (size_t)rotateSurface90Degrees(surface, numClockwiseTurns);
    APITools_SetIntValue(context, 0, return_value);
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_rotozoom_xy(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const double angle = APITools_GetFloatValue(context, 2);
    const double zoomx = APITools_GetFloatValue(context, 3);
    const double zoomy = APITools_GetFloatValue(context, 4);
    const int flags = (int)APITools_GetIntValue(context, 5);

    const size_t return_value = (size_t)rotozoomSurfaceXY(surface, angle, zoomx, zoomy, flags);
    APITools_SetIntValue(context, 0, return_value);
  }



  

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_pixelformat_maprgb(VMContext& context) {
    size_t* pixel_format_obj = APITools_GetObjectValue(context, 1);

    SDL_PixelFormat pixel_format;
    sdl_pixel_format_raw_write(&pixel_format, pixel_format_obj);

    const int r = (int)APITools_GetIntValue(context, 2);
    const int g = (int)APITools_GetIntValue(context, 3);
    const int b = (int)APITools_GetIntValue(context, 4);

    APITools_SetIntValue(context, 0, SDL_MapRGB(pixel_format_obj ? &pixel_format : nullptr , r, g, b));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_pixelformat_maprgba(VMContext& context) {
    size_t* pixel_format_obj = APITools_GetObjectValue(context, 1);

    SDL_PixelFormat pixel_format;
    sdl_pixel_format_raw_write(&pixel_format, pixel_format_obj);

    const int r = (int)APITools_GetIntValue(context, 2);
    const int g = (int)APITools_GetIntValue(context, 3);
    const int b = (int)APITools_GetIntValue(context, 4);
    const int a = (int)APITools_GetIntValue(context, 5);

    APITools_SetIntValue(context, 0, SDL_MapRGBA(pixel_format_obj ? &pixel_format : nullptr , r, g, b, a));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_pixelformat_alloc(VMContext& context) {
    size_t* pixel_format_obj = APITools_GetObjectValue(context, 0);
    const int format = (int)APITools_GetIntValue(context, 1);

    SDL_PixelFormat* pixel_format = SDL_AllocFormat(format);
    sdl_pixel_format_raw_read(pixel_format, pixel_format_obj);
    SDL_FreeFormat(pixel_format);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_fill_rect(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    size_t* rect_obj = APITools_GetObjectValue(context, 2);
    const int color = (int)APITools_GetIntValue(context, 3);

    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);

    APITools_SetIntValue(context, 0, SDL_FillRect(surface, rect_obj ? &rect : nullptr , color));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_upper_blit(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    size_t* srcrect_obj = APITools_GetObjectValue(context, 2);
    SDL_Rect srcrect;
    sdl_rect_raw_write(&srcrect, srcrect_obj);

    const size_t* dst_obj = APITools_GetObjectValue(context, 3);
    SDL_Surface* dst = dst_obj ? (SDL_Surface*)dst_obj[0] : nullptr ;

    size_t* dstrect_obj = APITools_GetObjectValue(context, 4);
    SDL_Rect dstrect;
    sdl_rect_raw_write(&dstrect, dstrect_obj);

    APITools_SetIntValue(context, 0, SDL_BlitSurface(surface,
      srcrect_obj ? &srcrect : nullptr , dst, dstrect_obj ? &dstrect : nullptr ));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_lower_blit(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    size_t* srcrect_obj = APITools_GetObjectValue(context, 2);
    SDL_Rect srcrect;
    sdl_rect_raw_write(&srcrect, srcrect_obj);

    const size_t* dst_obj = APITools_GetObjectValue(context, 3);
    SDL_Surface* dst = dst_obj ? (SDL_Surface*)dst_obj[0] : nullptr ;

    size_t* dstrect_obj = APITools_GetObjectValue(context, 4);
    SDL_Rect dstrect;
    sdl_rect_raw_write(&dstrect, dstrect_obj);

    APITools_SetIntValue(context, 0, SDL_LowerBlit(surface, srcrect_obj ? &srcrect : nullptr , 
      dst, dstrect_obj ? &dstrect : nullptr ));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_blit_scaled(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    size_t* srcrect_obj = APITools_GetObjectValue(context, 2);
    SDL_Rect srcrect;
    sdl_rect_raw_write(&srcrect, srcrect_obj);

    const size_t* dst_obj = APITools_GetObjectValue(context, 3);
    SDL_Surface* dst = dst_obj ? (SDL_Surface*)dst_obj[0] : nullptr ;

    size_t* dstrect_obj = APITools_GetObjectValue(context, 4);
    SDL_Rect dstrect;
    sdl_rect_raw_write(&dstrect, dstrect_obj);

    APITools_SetIntValue(context, 0, SDL_BlitScaled(surface,
      srcrect_obj ? &srcrect : nullptr , dst, dstrect_obj ? &dstrect : nullptr ));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_lower_blit_scaled(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    size_t* srcrect_obj = APITools_GetObjectValue(context, 2);
    SDL_Rect srcrect;
    sdl_rect_raw_write(&srcrect, srcrect_obj);

    const size_t* dst_obj = APITools_GetObjectValue(context, 3);
    SDL_Surface* dst = dst_obj ? (SDL_Surface*)dst_obj[0] : nullptr ;

    size_t* dstrect_obj = APITools_GetObjectValue(context, 4);
    SDL_Rect dstrect;
    sdl_rect_raw_write(&dstrect, dstrect_obj);

    APITools_SetIntValue(context, 0, SDL_LowerBlitScaled(surface, srcrect_obj ? &srcrect : nullptr , dst, dstrect_obj ? &dstrect : nullptr ));
  }

  //
  // SDL_PixelFormat
  //
  void sdl_pixel_format_raw_read(SDL_PixelFormat* pixel_format, size_t* pixel_format_obj) {
    if(pixel_format_obj) {
      pixel_format_obj[0] = pixel_format->format;
      if(pixel_format->palette) {
        size_t* palette_obj = (size_t*)pixel_format_obj[1];
        sdl_palette_raw_read(pixel_format->palette, palette_obj);
      }
      else {
        pixel_format_obj[1] = 0;
      }
      pixel_format_obj[2] = pixel_format->BitsPerPixel;
      pixel_format_obj[3] = pixel_format->BytesPerPixel;
      pixel_format_obj[4] = pixel_format->Rmask;
      pixel_format_obj[5] = pixel_format->Gmask;
      pixel_format_obj[6] = pixel_format->Bmask;
      pixel_format_obj[7] = pixel_format->Amask;
      pixel_format_obj[8] = pixel_format->Rloss;
      pixel_format_obj[9] = pixel_format->Gloss;
      pixel_format_obj[10] = pixel_format->Bloss;
      pixel_format_obj[11] = pixel_format->Aloss;
      pixel_format_obj[12] = pixel_format->Rshift;
      pixel_format_obj[13] = pixel_format->Gshift;
      pixel_format_obj[14] = pixel_format->Bshift;
      pixel_format_obj[15] = pixel_format->Ashift;
    }
  }

  void sdl_pixel_format_raw_write(SDL_PixelFormat* pixel_format, size_t* pixel_format_obj) {
    if(pixel_format_obj) {
      pixel_format->format = (Uint32)pixel_format_obj[0];
      if(pixel_format_obj[1]) {
          size_t* palette_obj = (size_t*)pixel_format_obj[1];
          sdl_palette_raw_write(pixel_format->palette, palette_obj);
      }
      else {
        pixel_format->palette = nullptr ;
      }
      pixel_format->BitsPerPixel = (Uint8)pixel_format_obj[2];
      pixel_format->BytesPerPixel = (Uint8)pixel_format_obj[3];
      pixel_format->Rmask = (Uint32)pixel_format_obj[4];
      pixel_format->Gmask = (Uint32)pixel_format_obj[5];
      pixel_format->Bmask = (Uint32)pixel_format_obj[6];
      pixel_format->Amask = (Uint32)pixel_format_obj[7];
      pixel_format->Rloss = (Uint8)pixel_format_obj[8];
      pixel_format->Gloss = (Uint8)pixel_format_obj[9];
      pixel_format->Bloss = (Uint8)pixel_format_obj[10];
      pixel_format->Aloss = (Uint8)pixel_format_obj[11];
      pixel_format->Rshift = (Uint8)pixel_format_obj[12];
      pixel_format->Gshift = (Uint8)pixel_format_obj[13];
      pixel_format->Bshift = (Uint8)pixel_format_obj[14];
      pixel_format->Ashift = (Uint8)pixel_format_obj[15];
    }
  }

  //
  // Color
  //
  void sdl_color_raw_read(SDL_Color* color, size_t* color_obj) {
    if(color) {
      color_obj[0] = color->r;
      color_obj[1] = color->g;
      color_obj[2] = color->b;
      color_obj[3] = color->a;
    }
  }
  
  void sdl_color_raw_write(SDL_Color* color, size_t* color_obj) {
    if(color_obj) {
      color->r = (Uint8)color_obj[0];
      color->g = (Uint8)color_obj[1];
      color->b = (Uint8)color_obj[2];
      color->a = (Uint8)color_obj[3];
    }
  }

  //
  // SDL_Point
  //
  void sdl_point_raw_read(SDL_Point* point, size_t* point_obj) {
    if(point) {
      point_obj[0] = point->x;
      point_obj[1] = point->y;
    }
  }

  void sdl_point_raw_write(SDL_Point* point, size_t* point_obj) {
    if(point_obj) {
      point->x = (int)point_obj[0];
      point->y = (int)point_obj[1];
    }
  }

  //
  // SDL_Rect
  //
  void sdl_rect_raw_read(SDL_Rect* rect, size_t* rect_obj) {
    if(rect && rect_obj) {
      rect_obj[0] = rect->x;
      rect_obj[1] = rect->y;
      rect_obj[2] = rect->w;
      rect_obj[3] = rect->h;
    }
  }

  void sdl_rect_raw_write(SDL_Rect* rect, size_t* rect_obj) {
    if(rect && rect_obj) {
      rect->x = (int)rect_obj[0];
      rect->y = (int)rect_obj[1];
      rect->w = (int)rect_obj[2];
      rect->h = (int)rect_obj[3];
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_rect_has_intersection(VMContext& context) {
    size_t* rect_obj = APITools_GetObjectValue(context, 1);
    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);

    size_t* B_obj = APITools_GetObjectValue(context, 2);
    SDL_Rect B;
    sdl_rect_raw_write(&B, B_obj);

    APITools_SetIntValue(context, 0, SDL_HasIntersection(rect_obj ? &rect : nullptr , B_obj ? &B : nullptr ));
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_rect_intersect(VMContext& context) {
    size_t* rect_obj = APITools_GetObjectValue(context, 1);
    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);

    size_t* B_obj = APITools_GetObjectValue(context, 2);
    SDL_Rect B;
    sdl_rect_raw_write(&B, B_obj);

    size_t* C_obj = APITools_GetObjectValue(context, 3);
    SDL_Rect C;
    const int return_value = SDL_IntersectRect(rect_obj ? &rect : nullptr , B_obj ? &B : nullptr , C_obj ? &C : nullptr );
    sdl_rect_raw_read(&C, C_obj);

    APITools_SetIntValue(context, 0, return_value);
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_rect_union(VMContext& context) {
    size_t* rect_obj = APITools_GetObjectValue(context, 0);
    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);

    size_t* B_obj = APITools_GetObjectValue(context, 1);
    SDL_Rect B;
    sdl_rect_raw_write(&B, B_obj);

    size_t* C_obj = APITools_GetObjectValue(context, 2);
    
    SDL_Rect C;
    SDL_UnionRect(rect_obj ? &rect : nullptr , B_obj ? &B : nullptr , &C);
    sdl_rect_raw_read(&C, C_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_rect_intersect_and_line(VMContext& context) {
    size_t* rect_obj = APITools_GetObjectValue(context, 1);
    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);

    int X1 = (int)APITools_GetIntValue(context, 2);
    int Y1 = (int)APITools_GetIntValue(context, 3);
    int X2 = (int)APITools_GetIntValue(context, 4);
    int Y2 = (int)APITools_GetIntValue(context, 5);

    APITools_SetIntValue(context, 0, SDL_IntersectRectAndLine(rect_obj ? &rect : nullptr , &X1, &Y1, &X2, &Y2));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_rect_point_in_rect(VMContext& context) {
    SDL_Rect rect;
    size_t* rect_obj = APITools_GetObjectValue(context, 1);
    sdl_rect_raw_write(&rect, rect_obj);

    SDL_Point point;
    size_t* point_obj = (size_t*)APITools_GetObjectValue(context, 2);
    sdl_point_raw_write(&point, point_obj);

    APITools_SetIntValue(context, 0, SDL_PointInRect(point_obj ? &point : nullptr , rect_obj ? &rect : nullptr ));
  }

  //
  // Display
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_num_video_drivers(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GetNumVideoDrivers());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_video_driver(VMContext& context) {
    const int index = (int)APITools_GetIntValue(context, 1);
    const  std::string value = SDL_GetVideoDriver(index);
    const std::wstring w_value(value.begin(), value.end());
    APITools_SetStringValue(context, 0, w_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_video_init(VMContext& context) {
    const std::wstring w_driver_name = APITools_GetStringValue(context, 1);
    const  std::string driver_name = UnicodeToBytes(w_driver_name);
    APITools_SetIntValue(context, 0, SDL_VideoInit(driver_name.c_str()));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_video_quit(VMContext& context) {
    SDL_VideoQuit();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_current_video_driver(VMContext& context) {
    const  std::string value = SDL_GetCurrentVideoDriver();
    const std::wstring w_value(value.begin(), value.end());
    APITools_SetStringValue(context, 0, w_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_num_video_displays(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GetNumVideoDisplays());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_display_name(VMContext& context) {
    const int displayIndex = (int)APITools_GetIntValue(context, 1);
    const  std::string value = SDL_GetDisplayName(displayIndex);
    const std::wstring w_value = BytesToUnicode(value);
    APITools_SetStringValue(context, 0, w_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_display_bounds(VMContext& context) {
    const int displayIndex = (int)APITools_GetIntValue(context, 1);
    size_t* rect_obj = APITools_GetObjectValue(context, 2);

    SDL_Rect rect;
    const int return_value = SDL_GetDisplayBounds(displayIndex, &rect);
    sdl_rect_raw_read(&rect, rect_obj);

    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_num_display_modes(VMContext& context) {
    const int displayIndex = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GetNumDisplayModes(displayIndex));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_display_mode(VMContext& context) {
    const int displayIndex = (int)APITools_GetIntValue(context, 1);
    const int modeIndex = (int)APITools_GetIntValue(context, 2);
    size_t* mode_obj = APITools_GetObjectValue(context, 3);
    
    SDL_DisplayMode mode;
    const int return_value = SDL_GetDisplayMode(displayIndex, modeIndex, &mode);
    sdl_display_mode_raw_read(&mode, mode_obj);

    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_current_display_mode(VMContext& context) {
    const int displayIndex = (int)APITools_GetIntValue(context, 1);
    size_t* mode_obj = APITools_GetObjectValue(context, 2);

    SDL_DisplayMode mode;
    const int return_value = SDL_GetCurrentDisplayMode(displayIndex, &mode);
    sdl_display_mode_raw_read(&mode, mode_obj);
    
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_closest_display_mode(VMContext& context) {
    const int displayIndex = (int)APITools_GetIntValue(context, 1);
    size_t* mode_obj = APITools_GetObjectValue(context, 2);
    size_t* closest_obj = APITools_GetObjectValue(context, 3);

    SDL_DisplayMode mode;
    sdl_display_mode_raw_write(&mode, mode_obj);

    SDL_DisplayMode closest;
    SDL_DisplayMode* return_value = SDL_GetClosestDisplayMode(displayIndex, mode_obj ? &mode : nullptr , &closest);
    sdl_display_mode_raw_read(&closest, closest_obj);

    APITools_SetIntValue(context, 0, return_value == nullptr );
  }

  void sdl_display_mode_raw_read(SDL_DisplayMode* mode, size_t* display_mode_obj) {
    if(mode && display_mode_obj) {
      display_mode_obj[0] = mode->format;
      display_mode_obj[1] = mode->w;
      display_mode_obj[2] = mode->h;
      display_mode_obj[3] = mode->refresh_rate;
    }
  }

  void sdl_display_mode_raw_write(SDL_DisplayMode* mode, size_t* display_mode_obj) {
    if(mode && display_mode_obj) {
      mode->format = (int)display_mode_obj[0];
      mode->w = (int)display_mode_obj[1];
      mode->h = (int)display_mode_obj[2];
      mode->refresh_rate = (int)display_mode_obj[3];
    }
  }

  void sdl_gamecontroller_button_bind_read(struct SDL_GameControllerButtonBind* button_bind, size_t* button_bind_obj) {
    if(button_bind) {
      button_bind_obj[0] = button_bind->bindType;
      button_bind_obj[1] = button_bind->value.button;
      button_bind_obj[2] = button_bind->value.axis;
      button_bind_obj[3] = button_bind->value.hat.hat;
      button_bind_obj[4] = button_bind->value.hat.hat_mask;
    }
  }

  //
  // Window
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_create(VMContext& context) {
    const std::wstring w_title = APITools_GetStringValue(context, 1);
    const  std::string title = UnicodeToBytes(w_title);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const int w = (int)APITools_GetIntValue(context, 4);
    const int h = (int)APITools_GetIntValue(context, 5);
    const Uint32 flags = (int)APITools_GetIntValue(context, 6);
    SDL_Window* window = SDL_CreateWindow(title.c_str(), x, y, w, h, flags);
    APITools_SetIntValue(context, 0, (size_t)window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_grabbed_window(VMContext& context) {
    APITools_SetIntValue(context, 0, (size_t)SDL_GetGrabbedWindow());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_gl_swap(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    SDL_GL_SwapWindow(window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_gl_get_drawable_size(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);

    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);
    APITools_SetIntValue(context, 1, w);
    APITools_SetIntValue(context, 2, h);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_display_index(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GetWindowDisplayIndex(window));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_display_mode(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    size_t* mode_obj = APITools_GetObjectValue(context, 2);

    SDL_DisplayMode mode;
    sdl_display_mode_raw_write(&mode, mode_obj);

    APITools_SetIntValue(context, 0, SDL_SetWindowDisplayMode(window, mode_obj ? &mode : nullptr ));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_pixel_format(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GetWindowPixelFormat(window));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_getid(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GetWindowID(window));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_fromid(VMContext& context) {
    const Uint32 id = (int)APITools_GetIntValue(context, 1);
    SDL_Window* window = SDL_GetWindowFromID(id);
    APITools_SetIntValue(context, 0, (size_t)window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_flags(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GetWindowFlags(window));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_title(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    const std::wstring w_title = APITools_GetStringValue(context, 1);
    const  std::string title = UnicodeToBytes(w_title);
    SDL_SetWindowTitle(window, title.c_str());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_icon(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);

    const size_t* icon_obj = APITools_GetObjectValue(context, 1);
    SDL_Surface* icon = icon_obj ? (SDL_Surface*)icon_obj[0] : nullptr ;
    
    SDL_SetWindowIcon(window, icon);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_modal_for(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);

    const size_t* parent_window_obj = APITools_GetObjectValue(context, 2);
    SDL_Window* parent_window = parent_window_obj ? (SDL_Window*)parent_window_obj[0] : nullptr ;

    APITools_SetIntValue(context, 0, SDL_SetWindowModalFor(window, parent_window));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_opacity(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    const float opacity = (float)APITools_GetFloatValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetWindowOpacity(window, opacity));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_resizable(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    const SDL_bool resizable = (SDL_bool)APITools_GetIntValue(context, 1);
    SDL_SetWindowResizable(window, resizable);
  }


#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_borders_size(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);

    int top; int left; int bottom; int right;
    const int result = SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);

    APITools_SetIntValue(context, 2, top);
    APITools_SetIntValue(context, 3, left);
    APITools_SetIntValue(context, 4, bottom);
    APITools_SetIntValue(context, 5, right);

    APITools_SetIntValue(context, 0, result);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_display_usable_bounds(VMContext& context) {
    const int displayIndex = (int)APITools_GetIntValue(context, 1);
    size_t* rect_obj = APITools_GetObjectValue(context, 2);

    SDL_Rect rect;
    const int return_value = SDL_GetDisplayUsableBounds(displayIndex, &rect);
    sdl_rect_raw_read(&rect, rect_obj);

    APITools_SetIntValue(context, 0, return_value);
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_creatergb_with_format(VMContext& context) {
    const Uint32 flags = (int)APITools_GetIntValue(context, 1);
    const int width = (int)APITools_GetIntValue(context, 2);
    const int height = (int)APITools_GetIntValue(context, 3);
    const int depth = (int)APITools_GetIntValue(context, 4);
    const  SDL_PixelFormatEnum format = (SDL_PixelFormatEnum)APITools_GetIntValue(context, 5);

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(flags, width, height, depth, format);
    APITools_SetIntValue(context, 0, (size_t)surface);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_position(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    const int x = (int)APITools_GetIntValue(context, 1);
    const int y = (int)APITools_GetIntValue(context, 2);
    SDL_SetWindowPosition(window, x, y);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_position(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);

    int x, y;
    SDL_GetWindowPosition(window, &x, &y);
    APITools_SetIntValue(context, 1, x);
    APITools_SetIntValue(context, 2, y);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_size(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    const int w = (int)APITools_GetIntValue(context, 1);
    const int h = (int)APITools_GetIntValue(context, 2);
    SDL_SetWindowSize(window, w, h);
  }


#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_size(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    APITools_SetIntValue(context, 1, w);
    APITools_SetIntValue(context, 2, h);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_minimum_size(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    const int min_w = (int)APITools_GetIntValue(context, 1);
    const int min_h = (int)APITools_GetIntValue(context, 2);
    SDL_SetWindowMinimumSize(window, min_w, min_h);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_minimum_size(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);

    int w, h;
    SDL_GetWindowMinimumSize(window, &w, &h);
    APITools_SetIntValue(context, 1, w);
    APITools_SetIntValue(context, 2, h);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_maximum_size(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    const int max_w = (int)APITools_GetIntValue(context, 1);
    const int max_h = (int)APITools_GetIntValue(context, 2);
    SDL_SetWindowMaximumSize(window, max_w, max_h);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_maximum_size(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);

    int w, h;
    SDL_GetWindowMaximumSize(window, &w, &h);
    APITools_SetIntValue(context, 1, w);
    APITools_SetIntValue(context, 2, h);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_bordered(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    const SDL_bool bordered = (SDL_bool)APITools_GetIntValue(context, 1);
    SDL_SetWindowBordered(window, bordered);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_show(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    SDL_ShowWindow(window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_hide(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    SDL_HideWindow(window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_raise(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    SDL_RaiseWindow(window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_maximize(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    SDL_MaximizeWindow(window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_minimize(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    SDL_MinimizeWindow(window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_restore(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    SDL_RestoreWindow(window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_fullscreen(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    const int flags = (int)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetWindowFullscreen(window, flags));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_surface(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    SDL_Surface* surface = SDL_GetWindowSurface(window);
    APITools_SetIntValue(context, 0, (size_t)surface);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_update_surface(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_UpdateWindowSurface(window));
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_grab(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    const SDL_bool grabbed = (SDL_bool)APITools_GetIntValue(context, 1);
    SDL_SetWindowGrab(window, grabbed);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_grab(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GetWindowGrab(window));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_brightness(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    const float brightness = (float)APITools_GetFloatValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetWindowBrightness(window, brightness));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_brightness(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    APITools_SetFloatValue(context, 0, SDL_GetWindowBrightness(window));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_set_gamma_ramp(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    Uint16 red = (Uint16)APITools_GetIntValue(context, 2);
    Uint16 green = (Uint16)APITools_GetIntValue(context, 3);
    Uint16 blue = (Uint16)APITools_GetIntValue(context, 4);

    APITools_SetIntValue(context, 0, SDL_SetWindowGammaRamp(window, &red, &green, &blue));
    APITools_SetIntValue(context, 2, red);
    APITools_SetIntValue(context, 3, green);
    APITools_SetIntValue(context, 4, blue);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_get_gamma_ramp(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);

    Uint16 red, green, blue;
    APITools_SetIntValue(context, 0, SDL_GetWindowGammaRamp(window, &red, &green, &blue));
    APITools_SetIntValue(context, 2, red);
    APITools_SetIntValue(context, 3, green);
    APITools_SetIntValue(context, 4, blue);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_destroy(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    SDL_DestroyWindow(window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_is_screen_saver_enabled(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_IsScreenSaverEnabled());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_enable_screen_saver(VMContext& context) {
    SDL_EnableScreenSaver();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_disable_screen_saver(VMContext& context) {
    SDL_DisableScreenSaver();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_show_simple_messagebox(VMContext& context) {    
    const Uint32 flags = (Uint32)APITools_GetIntValue(context, 0);

    const std::wstring w_title = APITools_GetStringValue(context, 1);
    const  std::string title = UnicodeToBytes(w_title);

    const std::wstring w_message = APITools_GetStringValue(context, 2);
    const  std::string message = UnicodeToBytes(w_message);
    
    size_t* window_obj = APITools_GetObjectValue(context, 3);

    APITools_SetIntValue(context, 0, SDL_ShowSimpleMessageBox(flags, title.c_str(), message.c_str(), window_obj ? (SDL_Window*)window_obj[0] : nullptr));
  }

  //
  // Event
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_new(VMContext& context) {
    SDL_Event* event = new SDL_Event;
    APITools_SetIntValue(context, 0, (size_t)event);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_free(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    delete event;
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_type(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, event->type);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_key(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    if(event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
      size_t* key_obj = APITools_GetObjectValue(context, 2);
      key_obj[0] = event->key.type;
      key_obj[1] = event->key.timestamp;
      key_obj[2] = event->key.windowID;
      key_obj[3] = event->key.state;
      key_obj[4] = event->key.repeat;
      
      size_t* key_sym_obj = (size_t*)key_obj[5];
      key_sym_obj[0] = event->key.keysym.scancode;
      key_sym_obj[1] = event->key.keysym.sym;
      key_sym_obj[2] = event->key.keysym.mod;

      APITools_SetIntValue(context, 0, 0);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_axis_key(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    if(event->type == SDL_JOYAXISMOTION) {
      size_t* axis_obj = APITools_GetObjectValue(context, 2);
      axis_obj[0] = event->jaxis.type;
      axis_obj[1] = event->jaxis.timestamp;
      axis_obj[2] = event->jaxis.which;
      axis_obj[3] = event->jaxis.axis;
      axis_obj[4] = event->jaxis.value;

      APITools_SetIntValue(context, 0, 0);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_text_key(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    if(event->type == SDL_TEXTINPUT) {
      size_t* text_obj = APITools_GetObjectValue(context, 2);
      text_obj[0] = event->text.type;
      text_obj[1] = event->text.timestamp;
      text_obj[2] = event->text.windowID;

      const std::wstring w_text = BytesToUnicode(event->text.text);
      text_obj[3] = (size_t)APITools_CreateStringObject(context, w_text);

      APITools_SetIntValue(context, 0, 0);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mouse_motion(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    if(event->type == SDL_MOUSEMOTION) {
      size_t* motion_obj = APITools_GetObjectValue(context, 2);
      motion_obj[0] = event->motion.type;
      motion_obj[1] = event->motion.timestamp;
      motion_obj[2] = event->motion.windowID;
      motion_obj[3] = event->motion.which;
      motion_obj[4] = event->motion.state;
      motion_obj[5] = event->motion.x;
      motion_obj[6] = event->motion.y;
      motion_obj[7] = event->motion.xrel;
      motion_obj[8] = event->motion.yrel;

      APITools_SetIntValue(context, 0, 0);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mouse_wheel(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    if(event->type == SDL_MOUSEWHEEL) {
      size_t* wheel_obj = APITools_GetObjectValue(context, 2);
      wheel_obj[0] = event->wheel.type;
      wheel_obj[1] = event->wheel.timestamp;
      wheel_obj[2] = event->wheel.windowID;
      wheel_obj[3] = event->wheel.which;
      wheel_obj[4] = event->wheel.x;
      wheel_obj[5] = event->wheel.y;
      wheel_obj[6] = event->wheel.direction;

      APITools_SetIntValue(context, 0, 0);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_jbutton_key(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    if(event->type == SDL_JOYBUTTONDOWN || event->type == SDL_JOYBUTTONUP) {
      size_t* button_obj = APITools_GetObjectValue(context, 2);
      button_obj[0] = event->cbutton.type;
      button_obj[1] = event->cbutton.timestamp;
      button_obj[2] = event->cbutton.which;
      button_obj[3] = event->cbutton.button;
      button_obj[4] = event->cbutton.state;

      APITools_SetIntValue(context, 0, 0);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mbutton_key(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    if(event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
      size_t* button_obj = APITools_GetObjectValue(context, 2);
      button_obj[0] = event->button.type;
      button_obj[1] = event->button.timestamp;
      button_obj[2] = event->button.windowID;
      button_obj[3] = event->button.which;
      button_obj[4] = event->button.button;
      button_obj[5] = event->button.state;
      button_obj[6] = event->button.clicks;
      button_obj[7] = event->button.x;
      button_obj[8] = event->button.y;

      APITools_SetIntValue(context, 0, 0);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_hat_key(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    if(event->type == SDL_JOYHATMOTION) {
      size_t* button_obj = APITools_GetObjectValue(context, 2);
      button_obj[0] = event->jhat.type;
      button_obj[1] = event->jhat.timestamp;
      button_obj[2] = event->jhat.which;
      button_obj[3] = event->jhat.hat;
      button_obj[4] = event->jhat.value;

      APITools_SetIntValue(context, 0, 0);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_peeps(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    const int numevents = (int)APITools_GetIntValue(context, 2);
    const SDL_eventaction action = (SDL_eventaction)APITools_GetIntValue(context, 3);
    const int minType = (int)APITools_GetIntValue(context, 4);
    const int maxType = (int)APITools_GetIntValue(context, 5);
    APITools_SetIntValue(context, 0, SDL_PeepEvents(event, numevents, action, minType, maxType));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_has(VMContext& context) {
    const int type = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_HasEvent(type));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_flush(VMContext& context) {
    SDL_FlushEvent((int)APITools_GetIntValue(context, 0));
  }

  #ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_pump(VMContext& context) {
    SDL_PumpEvents();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_poll(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_PollEvent(event));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_wait(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_WaitEvent(event));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_wait_timeout(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    const int timeout = (int)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_WaitEventTimeout(event, timeout));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_push(VMContext& context) {
    SDL_Event* event = (SDL_Event*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_PushEvent(event));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_state(VMContext& context) {
    const int type = (int)APITools_GetIntValue(context, 1);
    const int state = (int)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_EventState(type, state));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_registers(VMContext& context) {
    const int numevents = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_RegisterEvents(numevents));
  }

  //
  // Palette
  //
  void sdl_palette_raw_read(SDL_Palette* palette, size_t* palette_obj) {
    if(palette_obj) {
      palette_obj[0] = palette->ncolors;
//       palette_obj[1] = palette->format;
      palette_obj[2] = palette->version;
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_palette_read(VMContext& context) {
    SDL_Palette* palette = (SDL_Palette*)APITools_GetIntValue(context, 0);
    size_t* palette_obj = APITools_GetObjectValue(context, 1);
    sdl_palette_raw_read(palette, palette_obj);
  }
  
  void sdl_palette_raw_write(SDL_Palette* palette, size_t* palette_obj) {
    if(palette_obj) {
      palette->ncolors = (int)palette_obj[0];
//      palette->format = palette_obj[1];
      palette->version = (Uint8)palette_obj[2];
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_palette_write(VMContext& context) {
    SDL_Palette* palette = (SDL_Palette*)APITools_GetIntValue(context, 0);
    size_t* palette_obj = APITools_GetObjectValue(context, 1);
    sdl_palette_raw_write(palette, palette_obj);
  }

  //
  // Image library
  // 
  // 
  #ifdef _WIN32
  __declspec(dllexport)
  #endif
  void sdl_image_init(VMContext& context) {
    const int flags = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, IMG_Init(flags));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_image_quit(VMContext& context) {
    IMG_Quit();
  }

  #ifdef _WIN32
  __declspec(dllexport)
  #endif
  void sdl_image_load(VMContext& context) {
    const std::wstring wfile = APITools_GetStringValue(context, 1);
    const  std::string file = UnicodeToBytes(wfile);
    APITools_SetIntValue(context, 0, (size_t)IMG_Load(file.c_str()));
  }

  //
  // Renderer
  //
  #ifdef _WIN32
  __declspec(dllexport)
  #endif
  void sdl_renderer_create(VMContext& context) {
    size_t* window_obj = APITools_GetObjectValue(context, 1);
    SDL_Window* window = (SDL_Window*)window_obj[0];

    const int index = (int)APITools_GetIntValue(context, 2);
    const Uint32 flags = (int)APITools_GetIntValue(context, 3);

    APITools_SetIntValue(context, 0, (size_t)SDL_CreateRenderer(window, index, flags));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_destroy(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 0);
    SDL_DestroyRenderer(renderer);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_create_texture(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int format = (int)APITools_GetIntValue(context, 2);
    const int access = (int)APITools_GetIntValue(context, 3);
    const int w = (int)APITools_GetIntValue(context, 4);
    const int h = (int)APITools_GetIntValue(context, 5);

    APITools_SetIntValue(context, 0, (size_t)SDL_CreateTexture(renderer, format, access, w, h));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
   void sdl_renderer_get_num_render_drivers(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GetNumRenderDrivers());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_set_target(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);

    const size_t* texture_obj = APITools_GetObjectValue(context, 2);
    SDL_Texture* texture = texture_obj ? (SDL_Texture*)texture_obj[0] : nullptr ;

    APITools_SetIntValue(context, 0, SDL_SetRenderTarget(renderer, texture));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_set_integer_scale(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const SDL_bool  enable = (SDL_bool)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_RenderSetIntegerScale(renderer, enable));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_get_target(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)SDL_GetRenderTarget(renderer));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_render_fill_rect(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);

    size_t* rect_obj = (size_t*)APITools_GetObjectValue(context, 2);
    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);

    APITools_SetIntValue(context, 0, SDL_RenderFillRect(renderer, rect_obj ? &rect : nullptr ));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_render_draw_line(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);

    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);

    APITools_SetIntValue(context, 0, SDL_RenderDrawLine(renderer, x1, y1, x2, y2));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_render_draw_rect(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);

    size_t* rect_obj = (size_t*)APITools_GetObjectValue(context, 2);
    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);

    APITools_SetIntValue(context, 0, SDL_RenderDrawRect(renderer, rect_obj ? &rect : nullptr ));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_render_set_viewport(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    size_t* rect_obj = (size_t*)APITools_GetObjectValue(context, 2);
    
    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);

    APITools_SetIntValue(context, 0, SDL_RenderSetViewport(renderer, rect_obj ? &rect : nullptr ));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_render_get_viewport(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 0);
    size_t* rect_obj = APITools_GetObjectValue(context, 1);

    SDL_Rect rect;
    SDL_RenderGetViewport(renderer, &rect);
    sdl_rect_raw_read(&rect, rect_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_render_draw_point(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);

    APITools_SetIntValue(context, 0, SDL_RenderDrawPoint(renderer, x, y));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_get_render_driver_info(VMContext& context) {
    const int index = (int)APITools_GetIntValue(context, 1);
    size_t* info_obj = APITools_GetObjectValue(context, 2);

    if(info_obj) {
      SDL_RendererInfo info;
      const int return_value = SDL_GetRenderDriverInfo(index, &info);

       std::string name(info.name);
      std::wstring wname(name.begin(), name.end());

      info_obj[0] = (size_t)APITools_CreateStringObject(context, wname);
      info_obj[1] = info.flags;
      info_obj[2] = info.num_texture_formats;

      size_t* dest_formats = (size_t*)info_obj[3];
      Uint32* src_formats = info.texture_formats;
      for(int i = 0; i < 16; ++i) {
        dest_formats[i] = src_formats[i];
      }

      info_obj[4] = info.max_texture_width;
      info_obj[5] = info.max_texture_height;

      APITools_SetIntValue(context, 0, return_value);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_create_software(VMContext& context) {
    size_t* surface_obj = APITools_GetObjectValue(context, 1);
    SDL_Surface* surface = (SDL_Surface*)surface_obj[0];
    APITools_SetIntValue(context, 0, (size_t)SDL_CreateSoftwareRenderer(surface));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_get(VMContext& context) {
    size_t* window_obj = APITools_GetObjectValue(context, 1);
    SDL_Window* window = (SDL_Window*)window_obj[0];
    APITools_SetIntValue(context, 0, (size_t)SDL_GetRenderer(window));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_renderer_get_info(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    size_t* info_obj = APITools_GetObjectValue(context, 2);

    if(info_obj) {
      SDL_RendererInfo info;
      const int return_value = SDL_GetRendererInfo(renderer, &info);

      std::string name(info.name);
      std::wstring wname(name.begin(), name.end());

      info_obj[0] = (size_t)APITools_CreateStringObject(context, wname);
      info_obj[1] = info.flags;
      info_obj[2] = info.num_texture_formats;

      size_t* dest_formats = (size_t*)info_obj[3];
      Uint32* src_formats = info.texture_formats;
      for(int i = 0; i < 16; ++i) {
        dest_formats[i] = src_formats[i];
      }

      info_obj[4] = info.max_texture_width;
      info_obj[5] = info.max_texture_height;

      APITools_SetIntValue(context, 0, return_value);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_render_clear(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_RenderClear(renderer));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_render_copy(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    
    const size_t* texture_obj = APITools_GetObjectValue(context, 2);
    SDL_Texture* texture = texture_obj ? (SDL_Texture*)texture_obj[0] : nullptr ;

    size_t* srcrect_obj = APITools_GetObjectValue(context, 3);
    SDL_Rect srcrect;
    sdl_rect_raw_write(&srcrect, srcrect_obj);

    size_t* dstrect_obj = APITools_GetObjectValue(context, 4);
    SDL_Rect dstrect;
    sdl_rect_raw_write(&dstrect, dstrect_obj);

    APITools_SetIntValue(context, 0, SDL_RenderCopy(renderer, texture, 
      srcrect_obj ? &srcrect : nullptr , dstrect_obj ? &dstrect : nullptr ));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_render_copy_ex(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);

    const size_t* texture_obj = APITools_GetObjectValue(context, 2);
    SDL_Texture* texture = texture_obj ? (SDL_Texture*)texture_obj[0] : nullptr ;

    size_t* srcrect_obj = APITools_GetObjectValue(context, 3);
    SDL_Rect srcrect;
    sdl_rect_raw_write(&srcrect, srcrect_obj);

    size_t* dstrect_obj = APITools_GetObjectValue(context, 4);
    SDL_Rect dstrect;
    sdl_rect_raw_write(&dstrect, dstrect_obj);

    const double angle = APITools_GetFloatValue(context, 5);

    SDL_Point center;
    size_t* center_obj = (size_t*)APITools_GetObjectValue(context, 6);
    sdl_point_raw_write(&center, center_obj);

    const SDL_RendererFlip flip = (SDL_RendererFlip)APITools_GetIntValue(context, 7);

    APITools_SetIntValue(context, 0, SDL_RenderCopyEx(renderer, texture, 
      srcrect_obj ? &srcrect : nullptr , dstrect_obj ? &dstrect : nullptr , angle, center_obj ? &center : nullptr , flip));
}

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_render_present(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 0);
    SDL_RenderPresent(renderer);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_set_render_draw_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int r = (int)APITools_GetIntValue(context, 2);
    const int g = (int)APITools_GetIntValue(context, 3);
    const int b = (int)APITools_GetIntValue(context, 4);
    const int a = (int)APITools_GetIntValue(context, 5);
    APITools_SetIntValue(context, 0, SDL_SetRenderDrawColor(renderer, r, g, b, a));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_get_render_draw_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);

    Uint8 r, g, b, a;
    const int return_value = SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    
    APITools_SetIntValue(context, 2, r);
    APITools_SetIntValue(context, 3, g);
    APITools_SetIntValue(context, 4, b);
    APITools_SetIntValue(context, 5, a);
    
    APITools_SetIntValue(context, 0, return_value);
  }

  //
  // pixeldata
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_pixeldata_get(VMContext& context) {
    size_t* pixels_obj = APITools_GetObjectValue(context, 1);
    const Uint32 index = (Uint32)APITools_GetIntValue(context, 2);
    const Uint32 max_pixels = ((Uint32)pixels_obj[1] / sizeof(Uint32)) * (Uint32)pixels_obj[2];

    if(index < max_pixels) {
      Uint32* pixels = (Uint32*)pixels_obj[0];
      APITools_SetIntValue(context, 0, pixels[index]);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_pixeldata_set(VMContext& context) {
    size_t* pixels_obj = APITools_GetObjectValue(context, 1);
    const Uint32 index = (Uint32)APITools_GetIntValue(context, 2);
    const Uint32 max_pixels = ((Uint32)pixels_obj[1] / sizeof(Uint32)) * (Uint32)pixels_obj[2];

    if (index < max_pixels) {
      Uint32* pixels = (Uint32*)pixels_obj[0];
      pixels[index] = (Uint32)APITools_GetIntValue(context, 3);;
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_pixeldata_copy(VMContext& context) {
    size_t* to_obj = APITools_GetObjectValue(context, 1);
    size_t* from_obj = APITools_GetObjectValue(context, 2);

    const Uint32 to_size = (Uint32)to_obj[1] * (Uint32)to_obj[2];
    void* to = (void*)(to_obj[0]);

    const Uint32 from_size = (Uint32)from_obj[1] * (Uint32)from_obj[2];
    void* from = (void*)(from_obj[0]);
    
    if(to && from && to_size >= from_size) {
      memcpy(to, from, from_size);
      APITools_SetIntValue(context, 0, 1);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

  //
  // texture
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_create(VMContext& context) {
    size_t* renderer_obj = APITools_GetObjectValue(context, 1);
    SDL_Renderer* renderer = (SDL_Renderer*)renderer_obj[0];
    const int format = (int)APITools_GetIntValue(context, 2);
    const int access = (int)APITools_GetIntValue(context, 3);
    const int w = (int)APITools_GetIntValue(context, 4);
    const int h = (int)APITools_GetIntValue(context, 5);
    APITools_SetIntValue(context, 0, (size_t)SDL_CreateTexture(renderer, format, access, w, h));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_destroy(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 0);
    SDL_DestroyTexture(texture);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_query(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);

    size_t* format_obj = APITools_GetObjectValue(context, 2);
    size_t* access_obj = APITools_GetObjectValue(context, 3);

    Uint32 format; int access, w, h;
    APITools_SetIntValue(context, 0, SDL_QueryTexture(texture, format_obj ? &format : nullptr , access_obj ? &access : nullptr , &w, &h));

    if(format_obj) {
      APITools_SetIntValue(context, 2, format);
    }

    if(access_obj) {
      APITools_SetIntValue(context, 3, access);
    }

    APITools_SetIntValue(context, 4, w);
    APITools_SetIntValue(context, 5, h);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_lock(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);

    size_t* rect_obj = APITools_GetObjectValue(context, 2);
    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);
    
    // create PixelData
    void* pixels; int pitch;
    if(!SDL_LockTexture(texture, rect_obj ? &rect : nullptr , &pixels, &pitch)) {
      int width, height;
      SDL_QueryTexture(texture, nullptr , nullptr , &width, &height);
#ifdef _DEBUG
      assert(width == pitch / sizeof(Uint32));
#endif

      size_t* pixel_obj= APITools_CreateObject(context, L"Game.SDL2.PixelData");
      pixel_obj[0] = (size_t)pixels;
      pixel_obj[1] = (size_t)pitch;
      pixel_obj[2] = (size_t)height;
      APITools_SetObjectValue(context, 0, pixel_obj);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_unlock(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 0);
    SDL_UnlockTexture(texture);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_set_color_mod(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);
    const int r = (int)APITools_GetIntValue(context, 2);
    const int g = (int)APITools_GetIntValue(context, 3);
    const int b = (int)APITools_GetIntValue(context, 4);
    APITools_SetIntValue(context, 0, SDL_SetTextureColorMod(texture, r, g, b));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_get_color_mod(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);

    Uint8 r, g, b;
    const int return_value = SDL_GetTextureColorMod(texture, &r, &g, &b);

    APITools_SetIntValue(context, 2, r);
    APITools_SetIntValue(context, 3, g);
    APITools_SetIntValue(context, 4, b);

    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_set_alpha_mod(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);
    const int alpha = (int)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetTextureAlphaMod(texture, alpha));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_get_alpha_mod(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);
    
    Uint8 alpha;
    const int return_value = SDL_GetTextureAlphaMod(texture, &alpha);
    APITools_SetIntValue(context, 2, alpha);

    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_set_blend_mode(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);
    const SDL_BlendMode blendMode = (SDL_BlendMode)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetTextureBlendMode(texture, blendMode));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_get_blend_mode(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);
    
    SDL_BlendMode blendMode;
    const int return_value = SDL_GetTextureBlendMode(texture, &blendMode);
    APITools_SetIntValue(context, 2, blendMode);

    APITools_SetIntValue(context, 0, return_value);
  }

  //
  // Timer
  //

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_timer_get_ticks(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GetTicks());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_timer_delay(VMContext& context) {
    SDL_Delay((Uint32)APITools_GetIntValue(context, 0));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_ticks_passed(VMContext& context) {
    const Uint32 a = (Uint32)APITools_GetIntValue(context, 1);
    const Uint32 b = (Uint32)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_TICKS_PASSED(a, b));
  }

  //
  // Font
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_font_init(VMContext& context) {
    APITools_SetIntValue(context, 0, TTF_Init());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_font_quit(VMContext& context) {
    TTF_Quit();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_font_open(VMContext& context) {
    const std::wstring wfile = APITools_GetStringValue(context, 1);
    const  std::string file = UnicodeToBytes(wfile);
    const int ptsize = (int)APITools_GetIntValue(context, 2);

    TTF_Font* return_value = TTF_OpenFont(file.c_str(), ptsize);
    APITools_SetIntValue(context, 0, (size_t)return_value);
  }

  #ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_font_open_index(VMContext& context) {
    const std::wstring wfile = APITools_GetStringValue(context, 1);
    const  std::string file = UnicodeToBytes(wfile);
    const int ptsize = (int)APITools_GetIntValue(context, 2);
    const int index = (int)APITools_GetIntValue(context, 3);

    TTF_Font* return_value = TTF_OpenFontIndex(file.c_str(), ptsize, index);
    APITools_SetIntValue(context, 0, (size_t)return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_font_render_text_solid(VMContext& context) {
    TTF_Font* font = (TTF_Font*)APITools_GetIntValue(context, 1);
    
    const std::wstring wtext = APITools_GetStringValue(context, 2);
    const  std::string text = UnicodeToBytes(wtext);

    SDL_Color fg;
    size_t* fg_obj = APITools_GetObjectValue(context, 3);
    sdl_color_raw_write(&fg, fg_obj);

    APITools_SetIntValue(context, 0, (size_t)TTF_RenderText_Solid(font, text.c_str(), fg));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_font_render_utf8_solid(VMContext& context) {
    TTF_Font* font = (TTF_Font*)APITools_GetIntValue(context, 1);

    const std::wstring wtext = APITools_GetStringValue(context, 2);
    const  std::string text = UnicodeToBytes(wtext);

    SDL_Color fg;
    size_t* fg_obj = APITools_GetObjectValue(context, 3);
    sdl_color_raw_write(&fg, fg_obj);

    APITools_SetIntValue(context, 0, (size_t)TTF_RenderUTF8_Solid(font, text.c_str(), fg));
  }
   
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_font_render_text_blended(VMContext& context) {
    TTF_Font* font = (TTF_Font*)APITools_GetIntValue(context, 1);

    const std::wstring wtext = APITools_GetStringValue(context, 2);
    const  std::string text = UnicodeToBytes(wtext);

    SDL_Color fg;
    size_t* fg_obj = APITools_GetObjectValue(context, 3);
    sdl_color_raw_write(&fg, fg_obj);

    APITools_SetIntValue(context, 0, (size_t)TTF_RenderText_Blended(font, text.c_str(), fg));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_font_render_utf8_blended(VMContext& context) {
    TTF_Font* font = (TTF_Font*)APITools_GetIntValue(context, 1);

    const std::wstring wtext = APITools_GetStringValue(context, 2);
    const  std::string text = UnicodeToBytes(wtext);

    SDL_Color fg;
    size_t* fg_obj = APITools_GetObjectValue(context, 3);
    sdl_color_raw_write(&fg, fg_obj);

    APITools_SetIntValue(context, 0, (size_t)TTF_RenderUTF8_Blended(font, text.c_str(), fg));
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_font_render_text_shaded(VMContext& context) {
    TTF_Font* font = (TTF_Font*)APITools_GetIntValue(context, 1);

    const std::wstring wtext = APITools_GetStringValue(context, 2);
    const  std::string text = UnicodeToBytes(wtext);

    SDL_Color fg;
    size_t* fg_obj = APITools_GetObjectValue(context, 3);
    sdl_color_raw_write(&fg, fg_obj);

    SDL_Color bg;
    size_t* bg_obj = APITools_GetObjectValue(context, 4);
    sdl_color_raw_write(&bg, bg_obj);

    APITools_SetIntValue(context, 0, (size_t)TTF_RenderText_Shaded(font, text.c_str(), fg, bg));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_font_render_utf8_shaded(VMContext& context) {
    TTF_Font* font = (TTF_Font*)APITools_GetIntValue(context, 1);

    const std::wstring wtext = APITools_GetStringValue(context, 2);
    const  std::string text = UnicodeToBytes(wtext);

    SDL_Color fg;
    size_t* fg_obj = APITools_GetObjectValue(context, 3);
    sdl_color_raw_write(&fg, fg_obj);

    SDL_Color bg;
    size_t* bg_obj = APITools_GetObjectValue(context, 4);
    sdl_color_raw_write(&bg, bg_obj);

    APITools_SetIntValue(context, 0, (size_t)TTF_RenderUTF8_Shaded(font, text.c_str(), fg, bg));
  }

  //
  // Cursor
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cursor_get_global_mouse_state(VMContext& context) {
    int x, y;
    APITools_SetIntValue(context, 0, SDL_GetGlobalMouseState(&x, &y));
    APITools_SetIntValue(context, 1, x);
    APITools_SetIntValue(context, 2, y);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cursor_get_mouse_state(VMContext& context) {
    int x, y;
    APITools_SetIntValue(context, 0, SDL_GetMouseState(&x, &y));
    APITools_SetIntValue(context, 1, x);
    APITools_SetIntValue(context, 2, y);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cursor_get_mouse_state_pt(VMContext& context) {
    size_t* pt_obj = APITools_GetObjectValue(context, 1);

    int x, y;
    APITools_SetIntValue(context, 0, SDL_GetMouseState(&x, &y));
    
    pt_obj[0] = x;
    pt_obj[1] = y;
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cursor_warp_mouse_global(VMContext& context) {
    const int x = (int)APITools_GetIntValue(context, 1);
    const int y = (int)APITools_GetIntValue(context, 2);

    APITools_SetIntValue(context, 0, SDL_WarpMouseGlobal(x, y));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cursor_capture_mouse(VMContext& context) {
    const SDL_bool enabled = (SDL_bool)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_CaptureMouse(enabled));
  }

  //
  // Relative mouse mode: hides the cursor, locks it to the window, and reports
  // motion as unbounded deltas. This is what mouse-look needs -- without it the
  // pointer stops at the screen edge and the view stops turning with it.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cursor_set_relative_mouse_mode(VMContext& context) {
    const SDL_bool enabled = (SDL_bool)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_SetRelativeMouseMode(enabled));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cursor_get_relative_mouse_mode(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GetRelativeMouseMode() == SDL_TRUE ? 1 : 0);
  }

  //
  // Clipboard
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_power_set_clipboard_text(VMContext& context) {
    const std::wstring w_text = APITools_GetStringValue(context, 1);
    const  std::string text = UnicodeToBytes(w_text);
    APITools_SetIntValue(context, 0, SDL_SetClipboardText(text.c_str()));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_power_get_clipboard_text(VMContext& context) {
    char* text_ptr = SDL_GetClipboardText();
    const std::wstring w_text = BytesToUnicode(text_ptr);
    APITools_SetStringValue(context, 0, w_text);
    SDL_free(text_ptr);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_power_has_clipboard_text(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_HasClipboardText());
  }

  //
  // Keyboard
  //

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_keyboard_start_text_input(VMContext& context) {
    SDL_StartTextInput();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_keyboard_stop_text_input(VMContext& context) {
    SDL_StopTextInput();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_keyboard_set_text_input_rect(VMContext& context) {
    size_t* rect_obj = APITools_GetObjectValue(context, 0);

    SDL_Rect rect;
    sdl_rect_raw_write(&rect, rect_obj);
    SDL_SetTextInputRect(&rect);
  }

#ifdef _WIN32
__declspec(dllexport)
#endif
  void sdl_keyboard_get_state(VMContext& context) {
    int numkeys;
    const Uint8* states = SDL_GetKeyboardState(&numkeys);

    size_t* array = APITools_MakeByteArray(context, numkeys);
    Uint8* byte_array = (Uint8*)(array + 3);
    memcpy(byte_array, states, numkeys);

    // 'ByteArrayRef', not 'ByteArrayHolder'. The class was renamed in 8131ebb7f1
    // ("changing 'Holder' names to 'Ref'") and this string was missed, so
    // APITools_CreateObject returned null and the next line dereferenced it --
    // Keyboard->GetState() segfaulted on every platform from that commit until
    // now. Nothing caught it: no SDL test runs in CI and no shipped example
    // called GetState.
    size_t* byte_obj = APITools_CreateObject(context, L"System.ByteArrayRef");
    if(!byte_obj) {
      // fail loudly rather than crash if this name ever drifts again
      std::wcerr << L"Objeck SDL: cannot create System.ByteArrayRef" << std::endl;
      return;
    }
    byte_obj[0] = (size_t)array;

    APITools_SetObjectValue(context, 0, byte_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_keyboard_get_mod_state(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GetModState());
  }

  //
  // Game Controller
  //

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_add_mapping(VMContext& context) {
    const std::wstring w_mappingString = APITools_GetStringValue(context, 1);
    const  std::string mappingString = UnicodeToBytes(w_mappingString);

    APITools_SetIntValue(context, 0, SDL_GameControllerAddMapping(mappingString.c_str()));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_num_mappings(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GameControllerNumMappings());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_mapping_for_index(VMContext& context) {
    const int mapping_index = (int)APITools_GetIntValue(context, 1);
    const  std::string return_value = SDL_GameControllerMappingForIndex(mapping_index);

    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_mapping(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const  std::string return_value = SDL_GameControllerMapping(gamecontroller);

    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_sdl_is_game_controller(VMContext& context) {
    const int joystick_index = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_IsGameController(joystick_index));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_gamecontroller_name_for_index(VMContext& context) {
    const int joystick_index = (int)APITools_GetIntValue(context, 1);
    const  std::string return_value = SDL_GameControllerNameForIndex(joystick_index);

    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }

  /*
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_path_for_index(VMContext& context) {
    const int joystick_index = (int)APITools_GetIntValue(context, 1);
    const  std::string return_value = SDL_GameControllerPathForIndex(joystick_index);

    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_type_for_index(VMContext& context) {
    const int joystick_index = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerTypeForIndex(joystick_index));
  }
  */

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_mapping_for_device_index(VMContext& context) {
    const int joystick_index = (int)APITools_GetIntValue(context, 1);
    const  std::string return_value = SDL_GameControllerMappingForDeviceIndex(joystick_index);

    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_gamecontroller_from_instanceid(VMContext& context) {
    const SDL_JoystickID joyid = (SDL_JoystickID)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)SDL_GameControllerFromInstanceID(joyid));
  }

  /*
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_from_player_index(VMContext& context) {
    const int player_index = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)SDL_GameControllerFromPlayerIndex(player_index));
  }
  */

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_name(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const  std::string return_value = SDL_GameControllerName(gamecontroller);

    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }

  /*
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_path(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const  std::string return_value = SDL_GameControllerPath(gamecontroller);

    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }
  

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_type(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetType(gamecontroller));
  }
  */

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_player_index(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetPlayerIndex(gamecontroller));
  }

  /*
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_set_player_index(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 0);
    const int player_index = (int)APITools_GetIntValue(context, 1);
    SDL_GameControllerSetPlayerIndex(gamecontroller, player_index);
  }
  */

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_vendor(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetVendor(gamecontroller));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_product(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetProduct(gamecontroller));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_product_version(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetProductVersion(gamecontroller));
  }

  /*
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_firmware_version(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetFirmwareVersion(gamecontroller));
  }
  

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_serial(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const  std::string return_value = SDL_GameControllerGetSerial(gamecontroller);

    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }
  */

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_attached(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetAttached(gamecontroller));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_gamecontroller_get_joystick(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)SDL_GameControllerGetJoystick(gamecontroller));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_event_state(VMContext& context) {
    const int state = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerEventState(state));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_update(VMContext& context) {
    SDL_GameControllerUpdate();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontorller_get_axis_from_string(VMContext& context) {
    const std::wstring w_str = APITools_GetStringValue(context, 1);
    const  std::string str = UnicodeToBytes(w_str);

    APITools_SetIntValue(context, 0, SDL_GameControllerGetAxisFromString(str.c_str()));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_string_for_axis(VMContext& context) {
    const SDL_GameControllerAxis axis = (SDL_GameControllerAxis)APITools_GetIntValue(context, 1);
    const  std::string return_value = SDL_GameControllerGetStringForAxis(axis);

    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_bind_for_axis(VMContext& context) {
    size_t* button_bind_obj = APITools_GetObjectValue(context, 0);
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_GameControllerAxis axis = (SDL_GameControllerAxis)APITools_GetIntValue(context, 2);
    
    SDL_GameControllerButtonBind return_value = SDL_GameControllerGetBindForAxis(gamecontroller, axis);
    sdl_gamecontroller_button_bind_read(&return_value, button_bind_obj);
  }

  /*
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void sdl_gamecontroller_has_axis(VMContext& context) {
		SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
		const SDL_GameControllerAxis axis = (SDL_GameControllerAxis)APITools_GetIntValue(context, 2);
		APITools_SetIntValue(context, 0, SDL_GameControllerHasAxis(gamecontroller, axis));
	}
  */

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_axis(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_GameControllerAxis axis = (SDL_GameControllerAxis)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetAxis(gamecontroller, axis));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_button_from_string(VMContext& context) {
    const std::wstring w_str = APITools_GetStringValue(context, 1);
    const  std::string str = UnicodeToBytes(w_str);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetButtonFromString(str.c_str()));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_string_for_button(VMContext& context) {
    const SDL_GameControllerButton button = (SDL_GameControllerButton)APITools_GetIntValue(context, 1);
    APITools_SetStringValue(context, 0, BytesToUnicode(SDL_GameControllerGetStringForButton(button)));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_bind_for_button(VMContext& context) {
    size_t* button_bind_obj = APITools_GetObjectValue(context, 0);
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_GameControllerButton button = (SDL_GameControllerButton)APITools_GetIntValue(context, 2);
    SDL_GameControllerButtonBind return_value = SDL_GameControllerGetBindForButton(gamecontroller, button);
    sdl_gamecontroller_button_bind_read(&return_value, button_bind_obj);
  }

  /*
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_has_button(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_GameControllerButton button = (SDL_GameControllerButton)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_GameControllerHasButton(gamecontroller, button));
  }
  */

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_button(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_GameControllerButton button = (SDL_GameControllerButton)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetButton(gamecontroller, button));
  }

  /*
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_num_touchpads(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetNumTouchpads(gamecontroller));
  }
  

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_num_touchpad_fingers(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const int touchpad = (int)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_GameControllerGetNumTouchpadFingers(gamecontroller, touchpad));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_touchpad_finger(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const int touchpad = (int)APITools_GetIntValue(context, 2);
    const int finger = (int)APITools_GetIntValue(context, 3);

    Uint8 state; float x; float y; float pressure;
    APITools_SetIntValue(context, 0, SDL_GameControllerGetTouchpadFinger(gamecontroller, touchpad, finger, &state, &x, &y, &pressure));

    APITools_SetIntValue(context, 4, state);
    APITools_SetFloatValue(context, 5, x);
    APITools_SetFloatValue(context, 6, y);
    APITools_SetFloatValue(context, 7, pressure);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_has_sensor(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_SensorType type = (SDL_SensorType)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_GameControllerHasSensor(gamecontroller, type));
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_set_sensor_enabled(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_SensorType type = (SDL_SensorType)APITools_GetIntValue(context, 2);
    const SDL_bool enabled = (SDL_bool)APITools_GetIntValue(context, 3);
    APITools_SetIntValue(context, 0, SDL_GameControllerSetSensorEnabled(gamecontroller, type, enabled));
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_is_sensor_enabled(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_SensorType type = (SDL_SensorType)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_GameControllerIsSensorEnabled(gamecontroller, type));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_sensor_data_rate(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_SensorType type = (SDL_SensorType)APITools_GetIntValue(context, 2);
    APITools_SetFloatValue(context, 0, SDL_GameControllerGetSensorDataRate(gamecontroller, type));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_sensor_data(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_SensorType type = (SDL_SensorType)APITools_GetIntValue(context, 2);
    const int num_values = (int)APITools_GetIntValue(context, 4);
    
    float data;
    APITools_SetIntValue(context, 0, SDL_GameControllerGetSensorData(gamecontroller, type, &data, num_values));
    APITools_SetFloatValue(context, 3, data);

  }
  */

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_rumble(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const int low_frequency_rumble = (int)APITools_GetIntValue(context, 2);
    const int high_frequency_rumble = (int)APITools_GetIntValue(context, 3);
    const int duration_ms = (int)APITools_GetIntValue(context, 4);
    APITools_SetIntValue(context, 0, SDL_GameControllerRumble(gamecontroller, low_frequency_rumble, high_frequency_rumble, duration_ms));
  }

  /*
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_rumble_triggers(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const int left_rumble = (int)APITools_GetIntValue(context, 2);
    const int right_rumble = (int)APITools_GetIntValue(context, 3);
    const int duration_ms = (int)APITools_GetIntValue(context, 4);
    APITools_SetIntValue(context, 0, SDL_GameControllerRumbleTriggers(gamecontroller, left_rumble, right_rumble, duration_ms));
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_hasled(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerHasLED(gamecontroller));
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_has_rumble(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerHasRumble(gamecontroller));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_has_rumble_triggers(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GameControllerHasRumbleTriggers(gamecontroller));
  }
  

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_setled(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const int red = (int)APITools_GetIntValue(context, 2);
    const int green = (int)APITools_GetIntValue(context, 3);
    const int blue = (int)APITools_GetIntValue(context, 4);
    APITools_SetIntValue(context, 0, SDL_GameControllerSetLED(gamecontroller, red, green, blue));
  }
*/
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_close(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 0);
    SDL_GameControllerClose(gamecontroller);
  }

  /*
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_applesf_symbols_name_for_button(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_GameControllerButton button = (SDL_GameControllerButton)APITools_GetIntValue(context, 2);    
    const  std::string return_value = SDL_GameControllerGetAppleSFSymbolsNameForButton(gamecontroller, button);
    APITools_SetStringValue(context, 0, BytesToUnicode(return_value));
  }
  

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gamecontroller_get_applesf_symbols_name_for_axis(VMContext& context) {
    SDL_GameController* gamecontroller = (SDL_GameController*)APITools_GetIntValue(context, 1);
    const SDL_GameControllerAxis axis = (SDL_GameControllerAxis)APITools_GetIntValue(context, 2);
    const  std::string return_value = SDL_GameControllerGetAppleSFSymbolsNameForAxis(gamecontroller, axis);
    APITools_SetStringValue(context, 0, BytesToUnicode(return_value));
  }
  */

  //
  // Joystick
  // 
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_open(VMContext& context) {
    const int device_index = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)SDL_JoystickOpen(device_index));

  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_sdl_joystick_get_type(VMContext& context) {
    SDL_Joystick* joystick = (SDL_Joystick*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_JoystickGetType(joystick));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_sdl_joystick_get_device_type(VMContext& context) {
    const int device_index = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_JoystickGetDeviceType(device_index));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_sdl_joystick_get_device_product_version(VMContext& context) {
    const int device_index = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_JoystickGetDeviceProductVersion(device_index));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_sdl_joystick_get_device_product(VMContext& context) {
    const int device_index = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_JoystickGetDeviceProduct(device_index));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_sdl_joystick_get_product_version(VMContext& context) {
    SDL_Joystick* joystick = (SDL_Joystick*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_JoystickGetProductVersion(joystick));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_joystick_sdl_joystick_get_device_instanceid(VMContext& context) {
    const int device_index = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_JoystickGetDeviceInstanceID(device_index));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_sdl_joystick_get_device_vendor(VMContext& context) {
    const int device_index = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_JoystickGetDeviceVendor(device_index));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_sdl_joystick_get_product(VMContext& context) {
    SDL_Joystick* joystick = (SDL_Joystick*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_JoystickGetProduct(joystick));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_sdl_joystick_get_vendor(VMContext& context) {
    SDL_Joystick* joystick = (SDL_Joystick*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_JoystickGetVendor(joystick));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_sdl_joystick_get_axis_initial_state(VMContext& context) {
    SDL_Joystick* joystick = (SDL_Joystick*)APITools_GetIntValue(context, 1);
    const int axis = (int)APITools_GetIntValue(context, 2);

    Sint16 state;
    APITools_SetIntValue(context, 0, SDL_JoystickGetAxisInitialState(joystick, axis, &state));
    APITools_SetIntValue(context, 3, state);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_current_power_level(VMContext& context) {
    SDL_Joystick* joystick = (SDL_Joystick*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)SDL_JoystickCurrentPowerLevel(joystick));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_from_instance_id(VMContext& context) {
    const int instance_id = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)SDL_JoystickFromInstanceID(instance_id));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_display_get_display_dpi(VMContext& context) {
    const int displayIndex = (int)APITools_GetIntValue(context, 1);

    float ddpi; float hdpi; float vdpi;
    APITools_SetIntValue(context, 0, (size_t)SDL_GetDisplayDPI(displayIndex, &ddpi, &hdpi, &vdpi));

    APITools_SetFloatValue(context, 2, ddpi);
    APITools_SetFloatValue(context, 3, hdpi);
    APITools_SetFloatValue(context, 4, vdpi);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_close(VMContext& context) {
    SDL_Joystick* joystick = (SDL_Joystick*)APITools_GetIntValue(context, 0);
    SDL_JoystickClose(joystick);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_nums(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_NumJoysticks());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_joystick_name(VMContext& context) {
    SDL_Joystick* joystick = (SDL_Joystick*)APITools_GetIntValue(context, 1);
    const  std::string return_value = SDL_JoystickName(joystick);
    
    const std::wstring w_return_value(return_value.begin(), return_value.end());
    APITools_SetStringValue(context, 0, w_return_value);
  }

  //
  // Mixer
  //
  static Uint8* audio_buffer_pos; static int audio_buffer_len;

  static void audio_callback(void* userdata, Uint8* stream, int len) {
    if(!audio_buffer_len) {
      return;
    }

    len = (len > audio_buffer_len) ? audio_buffer_len : len;
    SDL_memcpy(stream, audio_buffer_pos, len);

    audio_buffer_pos += len;
    audio_buffer_len -= len;
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_record_id_pcm(VMContext& context) {
    const int dev_id = (int)APITools_GetIntValue(context, 1);
    const int sample_rate = (int)APITools_GetIntValue(context, 2);
    const int audio_format = (int)APITools_GetIntValue(context, 3);
    const int channels = (int)APITools_GetIntValue(context, 4);
    const int time_secs = (int)APITools_GetIntValue(context, 5);
    size_t* output_holder = APITools_GetArray(context, 0);

    if(SDL_Init(SDL_INIT_AUDIO) < 0) {
      output_holder[0] = 0;
      SDL_CloseAudio();
      return;
    }

    const int BUFFER_SIZE = 4096;
    SDL_AudioSpec desiredSpec;
    SDL_zero(desiredSpec);
    desiredSpec.freq = sample_rate;
    desiredSpec.format = audio_format;
    desiredSpec.channels = channels;
    desiredSpec.samples = BUFFER_SIZE;
    desiredSpec.callback = nullptr;

    SDL_AudioSpec obtainedSpec;
    SDL_zero(obtainedSpec);

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(dev_id, SDL_TRUE), SDL_TRUE, &desiredSpec, &obtainedSpec, 0);
    if(!dev) {
      output_holder[0] = 0;
      SDL_CloseAudio();
      return;
    }

    SDL_PauseAudioDevice(dev, 0);

    const Uint32 total_bytes = sample_rate * channels * (obtainedSpec.format == AUDIO_S16LSB ? 2 : 1) * time_secs;

    Uint8 buffer[BUFFER_SIZE] = { 0 };
    std::vector<Uint8> audio_buffer;
    Uint32 captured = 0;
    while(captured < total_bytes) {
      Uint32 available = SDL_GetQueuedAudioSize(dev);
      if(available >= BUFFER_SIZE) {
        SDL_DequeueAudio(dev, buffer, BUFFER_SIZE);
        audio_buffer.insert(audio_buffer.end(), buffer, buffer + BUFFER_SIZE);
        captured += BUFFER_SIZE;
      }
      else {
        SDL_Delay(10);
      }
    }

    SDL_CloseAudioDevice(dev);
    SDL_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    // copy output
    size_t* output_byte_array = APITools_MakeByteArray(context, audio_buffer.size());
    unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
    memcpy(output_byte_array_buffer, audio_buffer.data(), audio_buffer.size() * sizeof(Uint8));
    output_holder[0] = (size_t)output_byte_array;
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_record_pcm(VMContext& context) {
    const int sample_rate = (int)APITools_GetIntValue(context, 1);
    const int audio_format = (int)APITools_GetIntValue(context, 2);
    const int channels = (int)APITools_GetIntValue(context, 3);
    const int time_secs = (int)APITools_GetIntValue(context, 4);
    size_t* output_holder = APITools_GetArray(context, 0);

    if(SDL_Init(SDL_INIT_AUDIO) < 0) {
      output_holder[0] = 0;
      SDL_CloseAudio();
      return;
    }

    const int BUFFER_SIZE = 4096;
    SDL_AudioSpec desiredSpec;
    SDL_zero(desiredSpec);
    desiredSpec.freq = sample_rate;
    desiredSpec.format = audio_format;
    desiredSpec.channels = channels;
    desiredSpec.samples = BUFFER_SIZE;
    desiredSpec.callback = nullptr;

    SDL_AudioSpec obtainedSpec;
    SDL_zero(obtainedSpec);

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, SDL_TRUE, &desiredSpec, &obtainedSpec, 0);
    if(!dev) {
      output_holder[0] = 0;
      SDL_CloseAudio();
      return;
    }

    SDL_PauseAudioDevice(dev, 0);

    const Uint32 total_bytes = sample_rate * channels * (obtainedSpec.format == AUDIO_S16LSB ? 2 : 1) * time_secs;

    Uint8 buffer[BUFFER_SIZE] = { 0 };
    std::vector<Uint8> audio_buffer;
    Uint32 captured = 0;
    while(captured < total_bytes) {
      Uint32 available = SDL_GetQueuedAudioSize(dev);
      if(available >= BUFFER_SIZE) {
        SDL_DequeueAudio(dev, buffer, BUFFER_SIZE);        
        audio_buffer.insert(audio_buffer.end(), buffer, buffer + BUFFER_SIZE);
        captured += BUFFER_SIZE;
      }
      else {
        SDL_Delay(10);
      }
    }

    SDL_CloseAudioDevice(dev);
    SDL_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    // copy output
    size_t* output_byte_array = APITools_MakeByteArray(context, audio_buffer.size());
    unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
    memcpy(output_byte_array_buffer, audio_buffer.data(), audio_buffer.size() * sizeof(Uint8));
    output_holder[0] = (size_t)output_byte_array;
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_play_pcm(VMContext& context) {
    if(SDL_Init(SDL_INIT_AUDIO) < 0) {
      APITools_SetIntValue(context, 0, 0);
      SDL_CloseAudio();
      return;
    }

    size_t* byte_array = (size_t*)APITools_GetArray(context, 1)[0];
    audio_buffer_len = ((long)APITools_GetArraySize(byte_array));
    audio_buffer_pos = (Uint8*)APITools_GetArray(byte_array);

    const int sample_rate = (int)APITools_GetIntValue(context, 2);
    const int audio_format = (int)APITools_GetIntValue(context, 3);
    const int channels = (int)APITools_GetIntValue(context, 4);
        
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = sample_rate;
    spec.format = audio_format;
    spec.channels = channels;
    spec.samples = 4096;
    spec.callback = audio_callback;

    if(SDL_OpenAudio(&spec, nullptr ) < 0) {
      APITools_SetIntValue(context, 0, 0);
      SDL_CloseAudio();
      return;
    }

    SDL_PauseAudio(0);
    while(audio_buffer_len > 0) {
      SDL_Delay(100);
    }

    SDL_CloseAudio();
        
    APITools_SetIntValue(context, 0, 1);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_record_devs(VMContext& context) {
    if(SDL_Init(SDL_INIT_AUDIO) < 0) {
      APITools_SetIntValue(context, 0, 0);
      SDL_CloseAudio();
      return;
    }

    size_t* output_holder = APITools_GetArray(context, 0);

    const int dev_count = SDL_GetNumAudioDevices(SDL_TRUE);
    if(dev_count < 1) {
      output_holder[0] = 0;
      SDL_CloseAudio();
      return;
    }

    size_t* str_obj_array = APITools_MakeIntArray(context, dev_count);
    size_t* str_obj_array_ptr = str_obj_array + 3;

    for(int i = 0; i < dev_count; ++i) {
      const char* dev_name = SDL_GetAudioDeviceName(i, SDL_TRUE);
      const std::wstring wdevice_name = L"id=" + std::to_wstring(i) + L", name='" + BytesToUnicode(dev_name) + L'\'';
      str_obj_array_ptr[i] = (size_t)APITools_CreateStringObject(context, wdevice_name);
    }

    SDL_CloseAudio();

    output_holder[0] = (size_t)str_obj_array;
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_open_audio(VMContext& context) {
    const int frequency = (int)APITools_GetIntValue(context, 1);
    const int format = (int)APITools_GetIntValue(context, 2);
    const int channels = (int)APITools_GetIntValue(context, 3);
    const int chunksize = (int)APITools_GetIntValue(context, 4);
    
    APITools_SetIntValue(context, 0, Mix_OpenAudio(frequency, format, channels, chunksize));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_init(VMContext& context) {
    const int flags = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, Mix_Init(flags));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_quit(VMContext& context) {
    Mix_Quit();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_close(VMContext& context) {
    Mix_CloseAudio();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_audio_close(VMContext& context) {
    SDL_CloseAudio();
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_load_wav(VMContext& context) {
    const std::wstring w_file = APITools_GetStringValue(context, 1);
    const  std::string extension = UnicodeToBytes(w_file);

    APITools_SetIntValue(context, 0, (size_t)Mix_LoadWAV(extension.c_str()));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_play_channel_timed(VMContext& context) {
    const int channel = (int)APITools_GetIntValue(context, 1);
    Mix_Chunk* chunk = (Mix_Chunk*)APITools_GetIntValue(context, 2);
    const int loops = (int)APITools_GetIntValue(context, 3);
    const int ticks = (int)APITools_GetIntValue(context, 4);

    APITools_SetIntValue(context, 0, Mix_PlayChannelTimed(channel, chunk, loops, ticks));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_fade_in_channel_timed(VMContext& context) {
    const int channel = (int)APITools_GetIntValue(context, 1);
    Mix_Chunk* chunk = (Mix_Chunk*)APITools_GetIntValue(context, 2);
    const int loops = (int)APITools_GetIntValue(context, 3);
    const int ms = (int)APITools_GetIntValue(context, 4);
    const int ticks = (int)APITools_GetIntValue(context, 5);

    APITools_SetIntValue(context, 0, Mix_FadeInChannelTimed(channel, chunk, loops, ms, ticks));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_fade_out_channel(VMContext& context) {
    const int which = (int)APITools_GetIntValue(context, 1);
    const int ms = (int)APITools_GetIntValue(context, 2);

    APITools_SetIntValue(context, 0, Mix_FadeOutChannel(which, ms));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_playing(VMContext& context) {
    const int channel = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, Mix_Playing(channel));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_free_wav(VMContext& context) {
    Mix_Chunk* chunk = (Mix_Chunk*)APITools_GetIntValue(context, 0);
    Mix_FreeChunk(chunk);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_load_mus(VMContext& context) {
    const std::wstring w_file = APITools_GetStringValue(context, 1);
    const  std::string file = UnicodeToBytes(w_file);

    Mix_Music* music = Mix_LoadMUS(file.c_str());
    APITools_SetIntValue(context, 0, (size_t)music);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_play_music(VMContext& context) {
    Mix_Music* music = (Mix_Music*)APITools_GetIntValue(context, 1);
    const int loops = (int)APITools_GetIntValue(context, 2);

    APITools_SetIntValue(context, 0, Mix_PlayMusic(music, loops));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_volume(VMContext& context) {
    const int channel = (int)APITools_GetIntValue(context, 1);
    const int volume = (int)APITools_GetIntValue(context, 2);

    APITools_SetIntValue(context, 0, Mix_Volume(channel, volume));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_volume_music(VMContext& context) {
    const int volume = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, Mix_VolumeMusic(volume));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_halt_channel(VMContext& context) {
    const int channel = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, Mix_HaltChannel(channel));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_fade_in_music(VMContext& context) {
    Mix_Music* music = (Mix_Music*)APITools_GetIntValue(context, 1);
    const int loops = (int)APITools_GetIntValue(context, 2);
    const int ms = (int)APITools_GetIntValue(context, 3);

    APITools_SetIntValue(context, 0, Mix_FadeInMusic(music, loops, ms));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_fade_out_music(VMContext& context) {
    const int ms = (int)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, Mix_FadeOutMusic(ms));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_playing_music(VMContext& context) {
    APITools_SetIntValue(context, 0, Mix_PlayingMusic());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_paused_music(VMContext& context) {
    APITools_SetIntValue(context, 0, Mix_PausedMusic());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_halt_music(VMContext& context) {
    Mix_HaltMusic();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_resume_music(VMContext& context) {
    Mix_ResumeMusic();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_pause_music(VMContext& context) {
    Mix_PauseMusic();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_mixer_free_mus(VMContext& context) {
    Mix_Music* music = (Mix_Music*)APITools_GetIntValue(context, 0);
    Mix_FreeMusic(music);
  }

  // platform
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_platform_get(VMContext& context) {
    const  std::string value = SDL_GetPlatform();
    const std::wstring return_value(value.begin(), value.end());
    APITools_SetStringValue(context, 0, return_value);
  }

  // cpu
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_get_count(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GetCPUCount());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_get_cache_line_size(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GetCPUCacheLineSize());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_hasrdtsc(VMContext& context) {
    const int return_value = SDL_HasRDTSC();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_has_alti_vec(VMContext& context) {
    const int return_value = SDL_HasAltiVec();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_hasmmx(VMContext& context) {
    const int return_value = SDL_HasMMX();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_has3d_now(VMContext& context) {
    const int return_value = SDL_Has3DNow();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_hassse(VMContext& context) {
    const int return_value = SDL_HasSSE();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_hassse2(VMContext& context) {
    const int return_value = SDL_HasSSE2();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_hassse3(VMContext& context) {
    const int return_value = SDL_HasSSE3();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_hassse41(VMContext& context) {
    const int return_value = SDL_HasSSE41();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_hassse42(VMContext& context) {
    const int return_value = SDL_HasSSE42();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_hasavx(VMContext& context) {
    const int return_value = SDL_HasAVX();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_hasavx2(VMContext& context) {
    const int return_value = SDL_HasAVX2();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_cpu_get_systemram(VMContext& context) {
    const int return_value = SDL_GetSystemRAM();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_filesystem_get_base_path(VMContext& context) {
    const  std::string value = SDL_GetBasePath();
    const std::wstring return_value = BytesToUnicode(value);
    APITools_SetStringValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_filesystem_get_pref_path(VMContext& context) {
    const std::wstring w_org = APITools_GetStringValue(context, 1);
    const  std::string org = UnicodeToBytes(w_org);

    const std::wstring w_app = APITools_GetStringValue(context, 2);
    const  std::string app = UnicodeToBytes(w_app);

    const  std::string value = SDL_GetPrefPath(org.c_str(), app.c_str());

    const std::wstring return_value = BytesToUnicode(value);
    APITools_SetStringValue(context, 0, return_value);
  }

  // power
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_power_get_info(VMContext& context) {
    int secs, pct;
    const SDL_PowerState return_value = SDL_GetPowerInfo(&secs, &pct);

    APITools_SetIntValue(context, 0, return_value);
    APITools_SetIntValue(context, 1, secs);
    APITools_SetIntValue(context, 2, pct);
  }

  //////////////////// SDL2_gfx routines ////////////////////

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_pixel_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 4);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = pixelRGBA(renderer, x, y, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_renderer_is_clip_enabled(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_RenderIsClipEnabled(renderer));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_hline_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int x2 = (int)APITools_GetIntValue(context, 3);
    const int y = (int)APITools_GetIntValue(context, 4);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 5);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = hlineRGBA(renderer, x1, x2, y, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_vline_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int y2 = (int)APITools_GetIntValue(context, 4);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 5);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = vlineRGBA(renderer, x, y1, y2, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_rectangle_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 6);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = rectangleRGBA(renderer, x1, y1, x2, y2, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_rounded_rectangle_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);
    const int rad = (int)APITools_GetIntValue(context, 6);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 7);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = roundedRectangleRGBA(renderer, x1, y1, x2, y2, rad, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_renderer_box_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 6);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = boxRGBA(renderer, x1, y1, x2, y2, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_rounded_box_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);
    const int rad = (int)APITools_GetIntValue(context, 6);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 7);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = roundedBoxRGBA(renderer, x1, y1, x2, y2, rad, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_line_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 6);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = lineRGBA(renderer, x1, y1, x2, y2, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_aaline_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 6);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = aalineRGBA(renderer, x1, y1, x2, y2, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_thick_line_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);
    const int width = (int)APITools_GetIntValue(context, 6);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 7);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = thickLineRGBA(renderer, x1, y1, x2, y2, width, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_circle_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const int rad = (int)APITools_GetIntValue(context, 4);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 5);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = circleRGBA(renderer, x, y, rad, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_arc_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const int rad = (int)APITools_GetIntValue(context, 4);
    const int start = (int)APITools_GetIntValue(context, 5);
    const int end = (int)APITools_GetIntValue(context, 6);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 7);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = arcRGBA(renderer, x, y, rad, start, end, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_aacircle_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const int rad = (int)APITools_GetIntValue(context, 4);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 5);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = aacircleRGBA(renderer, x, y, rad, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_filled_circle_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const int r = (int)APITools_GetIntValue(context, 4);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 5);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = filledCircleRGBA(renderer, x, y, r, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_ellipse_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const int rx = (int)APITools_GetIntValue(context, 4);
    const int ry = (int)APITools_GetIntValue(context, 5);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 6);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = ellipseRGBA(renderer, x, y, rx, ry, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_aaellipse_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const int rx = (int)APITools_GetIntValue(context, 4);
    const int ry = (int)APITools_GetIntValue(context, 5);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 6);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = aaellipseRGBA(renderer, x, y, rx, ry, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_filled_ellipse_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const int rx = (int)APITools_GetIntValue(context, 4);
    const int ry = (int)APITools_GetIntValue(context, 5);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 6);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = filledEllipseRGBA(renderer, x, y, rx, ry, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_pie_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const int rad = (int)APITools_GetIntValue(context, 4);
    const int start = (int)APITools_GetIntValue(context, 5);
    const int end = (int)APITools_GetIntValue(context, 6);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 7);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = pieRGBA(renderer, x, y, rad, start, end, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_filled_pie_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const int rad = (int)APITools_GetIntValue(context, 4);
    const int start = (int)APITools_GetIntValue(context, 5);
    const int end = (int)APITools_GetIntValue(context, 6);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 7);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = filledPieRGBA(renderer, x, y, rad, start, end, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_trigon_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);
    const int x3 = (int)APITools_GetIntValue(context, 6);
    const int y3 = (int)APITools_GetIntValue(context, 7);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 8);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = trigonRGBA(renderer, x1, y1, x2, y2, x3, y3, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_aatrigon_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);
    const int x3 = (int)APITools_GetIntValue(context, 6);
    const int y3 = (int)APITools_GetIntValue(context, 7);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 8);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = aatrigonRGBA(renderer, x1, y1, x2, y2, x3, y3, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_filled_trigon_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x1 = (int)APITools_GetIntValue(context, 2);
    const int y1 = (int)APITools_GetIntValue(context, 3);
    const int x2 = (int)APITools_GetIntValue(context, 4);
    const int y2 = (int)APITools_GetIntValue(context, 5);
    const int x3 = (int)APITools_GetIntValue(context, 6);
    const int y3 = (int)APITools_GetIntValue(context, 7);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 8);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = filledTrigonRGBA(renderer, x1, y1, x2, y2, x3, y3, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_polygon_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);

    size_t* vx_ary = nullptr;
    const size_t* vx_obj = APITools_GetObjectValue(context, 2);
    if(vx_obj && vx_obj[0]) {
      vx_ary = (size_t*)vx_obj[0];
    }
    
    size_t* vy_ary = nullptr;
    const size_t* vy_obj = APITools_GetObjectValue(context, 3);
    if(vy_obj && vy_obj[0]) {
      vy_ary = (size_t*)vy_obj[0];
    }

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 4);
    sdl_color_raw_write(&color, color_obj);

    if(vx_ary && vy_ary && vx_ary[0] == vy_ary[0] && vx_ary[0] < POLY_MAX) {
      const int n = (int)vx_ary[0];

      vx_ary = vx_ary + ARRAY_HEADER_OFFSET;
      vy_ary = vy_ary + ARRAY_HEADER_OFFSET;

      Sint16 x_values[POLY_MAX];
      Sint16 y_values[POLY_MAX];

      for(int i = 0; i < n; ++i) {
        x_values[i] = (Sint16)vx_ary[i];
        y_values[i] = (Sint16)vy_ary[i];
      }

      const int return_value = polygonRGBA(renderer, x_values, y_values, n, color.r, color.g, color.b, color.a);

      APITools_SetIntValue(context, 0, return_value);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_filled_polygon_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);

    size_t* vx_ary = nullptr;
    const size_t* vx_obj = APITools_GetObjectValue(context, 2);
    if(vx_obj && vx_obj[0]) {
      vx_ary = (size_t*)vx_obj[0];
    }

    size_t* vy_ary = nullptr;
    const size_t* vy_obj = APITools_GetObjectValue(context, 3);
    if(vy_obj && vy_obj[0]) {
      vy_ary = (size_t*)vy_obj[0];
    }

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 4);
    sdl_color_raw_write(&color, color_obj);

    if(vx_ary && vy_ary && vx_ary[0] == vy_ary[0] && vx_ary[0] < POLY_MAX) {
      const int n = (int)vx_ary[0];

      vx_ary = vx_ary + ARRAY_HEADER_OFFSET;
      vy_ary = vy_ary + ARRAY_HEADER_OFFSET;

      Sint16 x_values[POLY_MAX];
      Sint16 y_values[POLY_MAX];

      for(int i = 0; i < n; ++i) {
        x_values[i] = (Sint16)vx_ary[i];
        y_values[i] = (Sint16)vy_ary[i];
      }

      const int return_value = filledPolygonRGBA(renderer, x_values, y_values, n, color.r, color.g, color.b, color.a);

      APITools_SetIntValue(context, 0, return_value);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_aapolygon_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);

    size_t* vx_ary = nullptr;
    const size_t* vx_obj = APITools_GetObjectValue(context, 2);
    if(vx_obj && vx_obj[0]) {
      vx_ary = (size_t*)vx_obj[0];
    }

    size_t* vy_ary = nullptr;
    const size_t* vy_obj = APITools_GetObjectValue(context, 3);
    if(vy_obj && vy_obj[0]) {
      vy_ary = (size_t*)vy_obj[0];
    }

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 4);
    sdl_color_raw_write(&color, color_obj);

    if(vx_ary && vy_ary && vx_ary[0] == vy_ary[0] && vx_ary[0] < POLY_MAX) {
      const int n = (int)vx_ary[0];

      vx_ary = vx_ary + ARRAY_HEADER_OFFSET;
      vy_ary = vy_ary + ARRAY_HEADER_OFFSET;

      Sint16 x_values[POLY_MAX];
      Sint16 y_values[POLY_MAX];

      for(int i = 0; i < n; ++i) {
        x_values[i] = (Sint16)vx_ary[i];
        y_values[i] = (Sint16)vy_ary[i];
      }

      const int return_value = aapolygonRGBA(renderer, x_values, y_values, n, color.r, color.g, color.b, color.a);

      APITools_SetIntValue(context, 0, return_value);
    }
    else {
      APITools_SetIntValue(context, 0, -1);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_character_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const char c = (char)APITools_GetIntValue(context, 4);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 5);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = characterRGBA(renderer, x, y, c, color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_string_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int x = (int)APITools_GetIntValue(context, 2);
    const int y = (int)APITools_GetIntValue(context, 3);
    const std::wstring ws = APITools_GetStringValue(context, 4);
    const  std::string s = UnicodeToBytes(ws);

    SDL_Color color;
    size_t* color_obj = APITools_GetObjectValue(context, 5);
    sdl_color_raw_write(&color, color_obj);

    const int return_value = stringRGBA(renderer, x, y, s.c_str(), color.r, color.g, color.b, color.a);
    APITools_SetIntValue(context, 0, return_value);
  }

  // ==========================================================================
  // OpenGL ("Game.OpenGL", core/compiler/lib_src/sdl_gl.obs)
  // ==========================================================================
  //
  // This lives in sdl.cpp rather than its own gl_sdl.cpp because lib_api.h
  // defines its helpers as NON-INLINE free functions in the header, so a second
  // translation unit that includes it fails to link with LNK2005 on every
  // APITools_* symbol. That is why all eight native libraries in this repo are
  // exactly one .cpp file each. Splitting the GL layer out would mean either
  // making lib_api.h inline (it is shared by all eight) or shipping a separate
  // libobjk_gl with its own seven per-platform build definitions.
  //
  // --------------------------------------------------------------------------
  // ADDING A GL CALL -- the whole recipe
  // --------------------------------------------------------------------------
  // 1. Add one `void name(VMContext&)` below, with the #ifdef _WIN32
  //    __declspec(dllexport) prologue every function in this file has.
  // 2. Add one method in sdl_gl.obs that calls it by that exact string through
  //    Proxy->GetDllProxy()->CallFunction.
  //
  // Two rules, both load-bearing:
  //
  // SLOT INDICES ARE POSITIONAL. Slot 0 is the return value ONLY if the Objeck
  // method returns something; otherwise the first argument sits at slot 0. Both
  // conventions already exist above -- sdl_window_gl_swap reads its window from
  // slot 0, sdl_window_get_display_index reads its window from slot 1 -- so
  // match the two sides deliberately rather than by habit.
  //
  // MAKE EACH CALL DO REAL WORK. StackInterpreter::SharedLibraryCall resolves
  // the symbol by string on EVERY call (GetProcAddress/dlsym, see
  // core/vm/interpreter.cpp:2981) and each call allocates a Base[] plus a boxed
  // holder per argument. A 1:1 wrapper over GL would be thousands of dlsym
  // calls per frame. So "compile a program from two shader sources" is ONE
  // function here, not the five GL calls it decomposes into.
  //
  // Only GL 1.1 entry points are linked directly (opengl32.lib / -lGL /
  // -framework OpenGL). Everything from GL 2.0 on is resolved through
  // SDL_GL_GetProcAddress into a function table, which is why this needs no
  // GLEW/GLAD dependency and why no platform's GL headers constrain us at all:
  // SDL_opengl.h embeds a verbatim copy of Mesa's gl.h and never includes
  // <OpenGL/gl.h> or any other system GL header, on any platform. The only GL
  // declarations we ever see are that GL 1.1 set plus our own typedefs below.
  //

  //
  // Reports the GL version string of the CURRENT context, or "" when there is
  // no current context. glGetString is GL 1.1, so it is linked rather than
  // loaded -- which makes this double as the proof that each platform's build
  // definition really does link a GL library.
  //
  // Until a context exists this correctly returns "". Once one does, this is
  // the assertion macOS needs: macOS hands back a legacy 2.1 context unless a
  // core + forward-compatible profile was requested, and offers no 3.3
  // compatibility profile at all, so silently getting 2.1 is the failure mode
  // to catch here rather than at first shader compile.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_version_string(VMContext& context) {
    const GLubyte* version = glGetString(GL_VERSION);
    const std::string return_value = version ? reinterpret_cast<const char*>(version) : "";
    const std::wstring w_return_value = BytesToUnicode(return_value);
    APITools_SetStringValue(context, 0, w_return_value);
  }

  //
  // Creates a GL context for a window and makes it current. SDL_GLContext is a
  // typedef for void*, so it crosses as an Int exactly like every other SDL
  // handle here -- invisible to the collector, freed explicitly.
  //
  // Returns 0 on failure; the caller reads Core->GetError() for the reason.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_gl_create_context(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    SDL_GLContext gl_context = window ? SDL_GL_CreateContext(window) : nullptr;
    APITools_SetIntValue(context, 0, (size_t)gl_context);
  }

  //
  // Binds a context to a window on the calling thread. A GL context is current
  // per-thread, so this must be called on whichever thread issues the GL calls.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_window_gl_make_current(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    SDL_GLContext gl_context = (SDL_GLContext)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_GL_MakeCurrent(window, gl_context));
  }

  //
  // Destroys a context. Returns Nil, so the handle is at slot 0 rather than 1.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_delete_context(VMContext& context) {
    SDL_GLContext gl_context = (SDL_GLContext)APITools_GetIntValue(context, 0);
    if(gl_context) {
      SDL_GL_DeleteContext(gl_context);
    }
  }

  //
  // Framebuffer state. glClearColor/glClear/glViewport are all GL 1.1, so they
  // link directly and need no proc-address lookup. Colors are GLclampf (32-bit)
  // while Objeck Float is a 64-bit double, hence the narrowing casts -- the same
  // conversion every buffer upload will need.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_clear_color(VMContext& context) {
    const GLclampf r = (GLclampf)APITools_GetFloatValue(context, 0);
    const GLclampf g = (GLclampf)APITools_GetFloatValue(context, 1);
    const GLclampf b = (GLclampf)APITools_GetFloatValue(context, 2);
    const GLclampf a = (GLclampf)APITools_GetFloatValue(context, 3);
    glClearColor(r, g, b, a);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_clear(VMContext& context) {
    glClear((GLbitfield)APITools_GetIntValue(context, 0));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_viewport(VMContext& context) {
    const GLint x = (GLint)APITools_GetIntValue(context, 0);
    const GLint y = (GLint)APITools_GetIntValue(context, 1);
    const GLsizei w = (GLsizei)APITools_GetIntValue(context, 2);
    const GLsizei h = (GLsizei)APITools_GetIntValue(context, 3);
    glViewport(x, y, w, h);
  }

  //
  // glGetError, drained in a loop by the Objeck side. GL accumulates errors in a
  // queue and only clears one per call, so a single check can hide others.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_get_error(VMContext& context) {
    APITools_SetIntValue(context, 0, glGetError());
  }
}

// ==========================================================================
// GL 2.0+ entry points
// ==========================================================================
//
// Resolved once through SDL_GL_GetProcAddress and cached here. Objeck never
// sees a proc address, so there is no GLEW/GLAD dependency and no reliance on
// the platform shipping a modern GL header -- SDL_opengl.h carries its own
// embedded copy of Mesa's gl.h and has no per-platform conditionals, so every
// platform compiles against exactly the typedefs below.
//
// Which is why macOS capping OpenGL at 4.1 is a RUNTIME limit here and not a
// compile-time one: the declaration is always present, and an entry point Apple
// does not implement simply comes back null from SDL_GL_GetProcAddress.
//
// Deliberately NOT inside extern "C": these are C++ statics, not exports.

#define GL_FN(name) static PFNGL##name##PROC objk_gl##name = nullptr;
GL_FN(CREATESHADER) GL_FN(SHADERSOURCE) GL_FN(COMPILESHADER)
GL_FN(GETSHADERIV) GL_FN(GETSHADERINFOLOG) GL_FN(DELETESHADER)
GL_FN(CREATEPROGRAM) GL_FN(ATTACHSHADER) GL_FN(LINKPROGRAM)
GL_FN(GETPROGRAMIV) GL_FN(GETPROGRAMINFOLOG) GL_FN(USEPROGRAM)
GL_FN(DELETEPROGRAM) GL_FN(GETUNIFORMLOCATION) GL_FN(UNIFORMMATRIX4FV)
GL_FN(UNIFORM1I) GL_FN(GENVERTEXARRAYS) GL_FN(BINDVERTEXARRAY)
GL_FN(DELETEVERTEXARRAYS) GL_FN(GENBUFFERS) GL_FN(BINDBUFFER)
GL_FN(BUFFERDATA) GL_FN(DELETEBUFFERS) GL_FN(VERTEXATTRIBPOINTER)
GL_FN(ENABLEVERTEXATTRIBARRAY) GL_FN(ACTIVETEXTURE)
GL_FN(GENERATEMIPMAP) GL_FN(UNIFORM1F) GL_FN(UNIFORM3FV)
#undef GL_FN

static bool objk_gl_loaded = false;

// Why the last GL call did nothing.
//
// Most functions below can only fail by doing nothing: the entry points are not
// loaded, a handle is null, a uniform name does not exist in the program. None
// of those raise a GL error, so glGetError stays clean and the program renders a
// black window while exiting 0. That is the single most confusing failure this
// binding has, so every silent bail records its reason here and GL->GetLastError
// reports it.
//
// One shared slot rather than a return value per function: it needs no signature
// changes, it works for the failures that have nowhere to return to (Bind, Draw),
// and a black screen is diagnosed by one question rather than six.
static std::string objk_gl_last_error;

// Records why a call bailed. Keeps the FIRST reason until it is read: the first
// one is the cause and the rest are usually its consequences.
static void objk_gl_fail(const std::string& reason) {
  if(objk_gl_last_error.empty()) {
    objk_gl_last_error = reason;
  }
}

// The reason every function below gives when nothing is loaded. Named once so
// the six call sites cannot drift apart.
static const char OBJK_GL_NOT_LOADED[] =
  "GL functions are not loaded -- call GL->LoadFunctions() after the context is current";

// Every entry point is resolved and checked up front, so a driver missing one
// is reported once by name instead of crashing on first use.
static std::string objk_gl_load_functions() {
  std::string missing;

#define OBJK_GL_LOAD(lower, NAME)                                                   \
  objk_gl##NAME = (PFNGL##NAME##PROC)SDL_GL_GetProcAddress(lower);             \
  if(!objk_gl##NAME) { missing += missing.empty() ? "" : ", "; missing += lower; }

  OBJK_GL_LOAD("glCreateShader", CREATESHADER)
  OBJK_GL_LOAD("glShaderSource", SHADERSOURCE)
  OBJK_GL_LOAD("glCompileShader", COMPILESHADER)
  OBJK_GL_LOAD("glGetShaderiv", GETSHADERIV)
  OBJK_GL_LOAD("glGetShaderInfoLog", GETSHADERINFOLOG)
  OBJK_GL_LOAD("glDeleteShader", DELETESHADER)
  OBJK_GL_LOAD("glCreateProgram", CREATEPROGRAM)
  OBJK_GL_LOAD("glAttachShader", ATTACHSHADER)
  OBJK_GL_LOAD("glLinkProgram", LINKPROGRAM)
  OBJK_GL_LOAD("glGetProgramiv", GETPROGRAMIV)
  OBJK_GL_LOAD("glGetProgramInfoLog", GETPROGRAMINFOLOG)
  OBJK_GL_LOAD("glUseProgram", USEPROGRAM)
  OBJK_GL_LOAD("glDeleteProgram", DELETEPROGRAM)
  OBJK_GL_LOAD("glGetUniformLocation", GETUNIFORMLOCATION)
  OBJK_GL_LOAD("glUniformMatrix4fv", UNIFORMMATRIX4FV)
  OBJK_GL_LOAD("glUniform1i", UNIFORM1I)
  OBJK_GL_LOAD("glGenVertexArrays", GENVERTEXARRAYS)
  OBJK_GL_LOAD("glBindVertexArray", BINDVERTEXARRAY)
  OBJK_GL_LOAD("glDeleteVertexArrays", DELETEVERTEXARRAYS)
  OBJK_GL_LOAD("glGenBuffers", GENBUFFERS)
  OBJK_GL_LOAD("glBindBuffer", BINDBUFFER)
  OBJK_GL_LOAD("glBufferData", BUFFERDATA)
  OBJK_GL_LOAD("glDeleteBuffers", DELETEBUFFERS)
  OBJK_GL_LOAD("glVertexAttribPointer", VERTEXATTRIBPOINTER)
  OBJK_GL_LOAD("glEnableVertexAttribArray", ENABLEVERTEXATTRIBARRAY)
  OBJK_GL_LOAD("glActiveTexture", ACTIVETEXTURE)
  // GL 3.0. Required, not optional: a 3.3 core context that cannot resolve this
  // is broken in a way worth failing loudly on rather than silently declining
  // mipmaps.
  OBJK_GL_LOAD("glGenerateMipmap", GENERATEMIPMAP)
  // GL 2.0, like every other uniform setter here
  OBJK_GL_LOAD("glUniform1f", UNIFORM1F)
  OBJK_GL_LOAD("glUniform3fv", UNIFORM3FV)
#undef OBJK_GL_LOAD

  objk_gl_loaded = missing.empty();
  return missing;
}

// A mesh is four values (VAO, VBO, EBO, index count), so the native side owns a
// small record and hands back its pointer -- the same convention every SDL
// handle in this file uses.
struct ObjkMesh {
  GLuint vao;
  GLuint vbo;
  GLuint ebo;
  GLsizei index_count;
};

// Objeck arrays arrive as a holder object whose field 0 is the array; the
// elements start ARRAY_HEADER_OFFSET words in. Returns nullptr when absent.
static size_t* objk_array_from_holder(size_t* holder, size_t& count) {
  count = 0;
  if(!holder || !holder[0]) {
    return nullptr;
  }
  size_t* array = (size_t*)holder[0];
  count = array[0];
  return array + ARRAY_HEADER_OFFSET;
}

// Uniform locations, cached per program.
//
// glGetUniformLocation is a driver call that takes a STRING, and every uniform
// set was making one -- plus a UnicodeToBytes conversion of the Objeck string --
// on every single call, in the per-frame path. Scene->Draw sets one matrix per
// box per frame, so a scene of two hundred boxes was two hundred string lookups
// a frame for a value that cannot change: a program's uniform locations are
// fixed once it is linked.
//
// The name still crosses the boundary as a string, because that is the readable
// API and the conversion is not the expensive half. What this removes is the
// driver call.
static std::map<GLuint, std::map<std::string, GLint> > objk_gl_uniforms;

// -1 when the program has no such uniform, exactly as glGetUniformLocation
// reports it -- including that a name GLSL optimised out as unused is
// indistinguishable from a misspelling.
static GLint objk_gl_uniform_location(GLuint program, const std::string& name) {
  std::map<std::string, GLint>& cache = objk_gl_uniforms[program];
  std::map<std::string, GLint>::const_iterator found = cache.find(name);
  if(found != cache.end()) {
    return found->second;
  }

  const GLint location = objk_glGETUNIFORMLOCATION(program, name.c_str());
  cache[name] = location;
  return location;
}

// Compiles one stage and returns 0 plus a log on failure.
static GLuint objk_gl_compile_stage(GLenum type, const std::string& source, std::string& log) {
  const GLuint shader = objk_glCREATESHADER(type);
  if(!shader) {
    log = "glCreateShader returned 0";
    return 0;
  }

  const char* text = source.c_str();
  objk_glSHADERSOURCE(shader, 1, &text, nullptr);
  objk_glCOMPILESHADER(shader);

  GLint ok = GL_FALSE;
  objk_glGETSHADERIV(shader, GL_COMPILE_STATUS, &ok);
  if(ok != GL_TRUE) {
    GLint length = 0;
    objk_glGETSHADERIV(shader, GL_INFO_LOG_LENGTH, &length);
    if(length > 0) {
      std::string buffer((size_t)length, '\0');
      objk_glGETSHADERINFOLOG(shader, length, nullptr, &buffer[0]);
      log = buffer;
    }
    else {
      log = "shader compilation failed with no log";
    }
    objk_glDELETESHADER(shader);
    return 0;
  }

  return shader;
}

extern "C" {
  //
  // Resolves the GL 2.0+ entry points. Call once after the context is current.
  // Returns "" on success, or a comma-separated list of the functions that could
  // not be resolved -- which is what a too-old context looks like.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_load_functions(VMContext& context) {
    const std::string missing = objk_gl_load_functions();
    APITools_SetStringValue(context, 0, BytesToUnicode(missing));
  }

  //
  // Reports and clears the reason the last GL call silently did nothing. See the
  // objk_gl_last_error comment: these are the failures that raise no GL error, so
  // glGetError cannot find them and only this can.
  //
  // Reading CLEARS it, so a caller can check a sequence of calls and know the
  // report belongs to the one it just made rather than to something earlier.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_last_error(VMContext& context) {
    APITools_SetStringValue(context, 0, BytesToUnicode(objk_gl_last_error));
    objk_gl_last_error.clear();
  }

  //
  // Compiles a vertex + fragment shader and links them into a program: five GL
  // calls per stage plus the link, collapsed into ONE native call, because the
  // VM resolves the symbol by string on every call.
  //
  // slot 0 = program id (0 on failure), 1 = vertex source, 2 = fragment source,
  // slot 3 receives the compile/link log.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_program_create(VMContext& context) {
    if(!objk_gl_loaded) {
      objk_gl_fail(std::string("Shader->New: ") + OBJK_GL_NOT_LOADED);
      APITools_SetIntValue(context, 0, 0);
      APITools_SetStringValue(context, 3, BytesToUnicode(OBJK_GL_NOT_LOADED));
      return;
    }

    const std::string vertex_source = UnicodeToBytes(APITools_GetStringValue(context, 1));
    const std::string fragment_source = UnicodeToBytes(APITools_GetStringValue(context, 2));

    std::string log;
    const GLuint vertex = objk_gl_compile_stage(GL_VERTEX_SHADER, vertex_source, log);
    if(!vertex) {
      APITools_SetIntValue(context, 0, 0);
      APITools_SetStringValue(context, 3, BytesToUnicode("vertex shader: " + log));
      return;
    }

    const GLuint fragment = objk_gl_compile_stage(GL_FRAGMENT_SHADER, fragment_source, log);
    if(!fragment) {
      objk_glDELETESHADER(vertex);
      APITools_SetIntValue(context, 0, 0);
      APITools_SetStringValue(context, 3, BytesToUnicode("fragment shader: " + log));
      return;
    }

    const GLuint program = objk_glCREATEPROGRAM();
    objk_glATTACHSHADER(program, vertex);
    objk_glATTACHSHADER(program, fragment);
    objk_glLINKPROGRAM(program);

    // the stages are linked into the program now; the objects themselves are no
    // longer needed whether or not the link succeeded
    objk_glDELETESHADER(vertex);
    objk_glDELETESHADER(fragment);

    GLint linked = GL_FALSE;
    objk_glGETPROGRAMIV(program, GL_LINK_STATUS, &linked);
    if(linked != GL_TRUE) {
      GLint length = 0;
      objk_glGETPROGRAMIV(program, GL_INFO_LOG_LENGTH, &length);
      std::string buffer;
      if(length > 0) {
        buffer.assign((size_t)length, '\0');
        objk_glGETPROGRAMINFOLOG(program, length, nullptr, &buffer[0]);
      }
      else {
        buffer = "program link failed with no log";
      }
      objk_glDELETEPROGRAM(program);
      APITools_SetIntValue(context, 0, 0);
      APITools_SetStringValue(context, 3, BytesToUnicode("link: " + buffer));
      return;
    }

    APITools_SetIntValue(context, 0, program);
    APITools_SetStringValue(context, 3, BytesToUnicode(""));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_program_use(VMContext& context) {
    if(objk_gl_loaded) {
      objk_glUSEPROGRAM((GLuint)APITools_GetIntValue(context, 0));
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_program_delete(VMContext& context) {
    if(objk_gl_loaded) {
      const GLuint program = (GLuint)APITools_GetIntValue(context, 0);
      if(program) {
        objk_glDELETEPROGRAM(program);
        // Drop the cached locations with it. GL reuses names, so a later program
        // handed this same id would otherwise inherit locations belonging to a
        // program that no longer exists -- uniforms silently written to the wrong
        // slot, which is about the worst failure mode available here.
        objk_gl_uniforms.erase(program);
      }
    }
  }

  //
  // Uploads a 4x4 matrix. Objeck Float is a 64-bit double and GL wants 32-bit
  // floats, so the 16 elements are narrowed here -- the conversion every buffer
  // upload in this file has to do.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_uniform_matrix4(VMContext& context) {
    if(!objk_gl_loaded) {
      objk_gl_fail(std::string("Shader->SetMatrix4: ") + OBJK_GL_NOT_LOADED);
      return;
    }

    const GLuint program = (GLuint)APITools_GetIntValue(context, 0);
    const std::string name = UnicodeToBytes(APITools_GetStringValue(context, 1));

    size_t count = 0;
    size_t* values = objk_array_from_holder(APITools_GetObjectValue(context, 2), count);
    if(!values || count < 16) {
      objk_gl_fail("Shader->SetMatrix4(\"" + name + "\"): a mat4 needs 16 floats, got " +
                   std::to_string(count));
      return;
    }

    const double* source = (double*)values;
    GLfloat matrix[16];
    for(int i = 0; i < 16; ++i) {
      matrix[i] = (GLfloat)source[i];
    }

    // glUniform* writes to whatever program is CURRENT, not to the one whose
    // location was just looked up. Nothing used to make this program current, so
    // a second shader in the same program silently received the first one's
    // uniforms -- the demos got away with it only because none of them ever
    // called Use twice. Binding here makes each setter correct on its own.
    objk_glUSEPROGRAM(program);

    const GLint location = objk_gl_uniform_location(program, name);
    if(location >= 0) {
      objk_glUNIFORMMATRIX4FV(location, 1, GL_FALSE, matrix);
    }
    else {
      // A name the linked program has no location for. GL raises no error for
      // this, so without the report it is a black screen from correct-looking
      // code. Note GLSL strips uniforms it can prove are unused, so a spelling
      // mistake and a genuinely-unused uniform look identical from here.
      objk_gl_fail("Shader->SetMatrix4: no uniform named \"" + name +
                   "\" in this program (misspelled, or optimised out as unused)");
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_uniform_int(VMContext& context) {
    if(!objk_gl_loaded) {
      objk_gl_fail(std::string("Shader->SetInt: ") + OBJK_GL_NOT_LOADED);
      return;
    }

    const GLuint program = (GLuint)APITools_GetIntValue(context, 0);
    const std::string name = UnicodeToBytes(APITools_GetStringValue(context, 1));
    const GLint value = (GLint)APITools_GetIntValue(context, 2);

    objk_glUSEPROGRAM(program);

    const GLint location = objk_gl_uniform_location(program, name);
    if(location >= 0) {
      objk_glUNIFORM1I(location, value);
    }
    else {
      objk_gl_fail("Shader->SetInt: no uniform named \"" + name +
                   "\" in this program (misspelled, or optimised out as unused)");
    }
  }

  //
  // Whether a linked program has a uniform of this name, without recording a
  // failure when it does not.
  //
  // Scene sets a "model" matrix for lit shaders and must not set it for unlit
  // ones -- and since a missing uniform is now REPORTED, blindly setting it
  // would file a diagnostic every frame for every object. So this is the
  // question to ask first. It reads the same cache the setters use, so asking is
  // free after the first time.
  //
  // slot 0 = 1 or 0, 1 = program, 2 = name.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_uniform_exists(VMContext& context) {
    APITools_SetIntValue(context, 0, 0);
    if(!objk_gl_loaded) {
      return;
    }

    const GLuint program = (GLuint)APITools_GetIntValue(context, 1);
    const std::string name = UnicodeToBytes(APITools_GetStringValue(context, 2));

    APITools_SetIntValue(context, 0, objk_gl_uniform_location(program, name) >= 0 ? 1 : 0);
  }

  //
  // A single float uniform. slot 0 = program, 1 = name, 2 = value.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_uniform_float(VMContext& context) {
    if(!objk_gl_loaded) {
      objk_gl_fail(std::string("Shader->SetFloat: ") + OBJK_GL_NOT_LOADED);
      return;
    }

    const GLuint program = (GLuint)APITools_GetIntValue(context, 0);
    const std::string name = UnicodeToBytes(APITools_GetStringValue(context, 1));
    const GLfloat value = (GLfloat)APITools_GetFloatValue(context, 2);

    objk_glUSEPROGRAM(program);

    const GLint location = objk_gl_uniform_location(program, name);
    if(location >= 0) {
      objk_glUNIFORM1F(location, value);
    }
    else {
      objk_gl_fail("Shader->SetFloat: no uniform named \"" + name +
                   "\" in this program (misspelled, or optimised out as unused)");
    }
  }

  //
  // A vec3 uniform: light directions, colours, positions. Three separate floats
  // rather than an array, because at this size boxing three doubles beats boxing
  // an array holder, and every caller has three named quantities anyway.
  //
  // slot 0 = program, 1 = name, 2..4 = x, y, z.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_uniform_vec3(VMContext& context) {
    if(!objk_gl_loaded) {
      objk_gl_fail(std::string("Shader->SetVec3: ") + OBJK_GL_NOT_LOADED);
      return;
    }

    const GLuint program = (GLuint)APITools_GetIntValue(context, 0);
    const std::string name = UnicodeToBytes(APITools_GetStringValue(context, 1));

    GLfloat value[3];
    value[0] = (GLfloat)APITools_GetFloatValue(context, 2);
    value[1] = (GLfloat)APITools_GetFloatValue(context, 3);
    value[2] = (GLfloat)APITools_GetFloatValue(context, 4);

    objk_glUSEPROGRAM(program);

    const GLint location = objk_gl_uniform_location(program, name);
    if(location >= 0) {
      objk_glUNIFORM3FV(location, 1, value);
    }
    else {
      objk_gl_fail("Shader->SetVec3: no uniform named \"" + name +
                   "\" in this program (misspelled, or optimised out as unused)");
    }
  }

  //
  // Uploads geometry once into a VAO/VBO/EBO and returns a handle to draw it
  // many times. This is the shape that keeps the native boundary cheap: the
  // per-frame cost is one draw call, not one call per vertex.
  //
  // slot 0 = mesh handle (0 on failure), 1 = Float[] interleaved vertices,
  // 2 = Int[] indices, 3 = Int[] attribute component counts (e.g. [3,2] for
  // position + texcoord).
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_mesh_create(VMContext& context) {
    APITools_SetIntValue(context, 0, 0);
    if(!objk_gl_loaded) {
      objk_gl_fail(std::string("Mesh->New: ") + OBJK_GL_NOT_LOADED);
      return;
    }

    size_t vertex_count = 0, index_count = 0, layout_count = 0;
    size_t* vertices = objk_array_from_holder(APITools_GetObjectValue(context, 1), vertex_count);
    size_t* indices = objk_array_from_holder(APITools_GetObjectValue(context, 2), index_count);
    size_t* layout = objk_array_from_holder(APITools_GetObjectValue(context, 3), layout_count);

    if(!vertices || !indices || !layout || !vertex_count || !index_count || !layout_count) {
      return;
    }

    // double -> GLfloat, and Objeck's 64-bit Int -> GLuint
    const double* vertex_source = (double*)vertices;
    std::vector<GLfloat> vertex_data(vertex_count);
    for(size_t i = 0; i < vertex_count; ++i) {
      vertex_data[i] = (GLfloat)vertex_source[i];
    }

    std::vector<GLuint> index_data(index_count);
    for(size_t i = 0; i < index_count; ++i) {
      index_data[i] = (GLuint)indices[i];
    }

    GLsizei stride = 0;
    for(size_t i = 0; i < layout_count; ++i) {
      stride += (GLsizei)layout[i];
    }
    if(stride <= 0) {
      return;
    }

    ObjkMesh* mesh = new ObjkMesh();
    mesh->vao = mesh->vbo = mesh->ebo = 0;
    mesh->index_count = (GLsizei)index_count;

    objk_glGENVERTEXARRAYS(1, &mesh->vao);
    objk_glBINDVERTEXARRAY(mesh->vao);

    objk_glGENBUFFERS(1, &mesh->vbo);
    objk_glBINDBUFFER(GL_ARRAY_BUFFER, mesh->vbo);
    objk_glBUFFERDATA(GL_ARRAY_BUFFER,
                      (GLsizeiptr)(vertex_data.size() * sizeof(GLfloat)),
                      vertex_data.data(), GL_STATIC_DRAW);

    objk_glGENBUFFERS(1, &mesh->ebo);
    objk_glBINDBUFFER(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    objk_glBUFFERDATA(GL_ELEMENT_ARRAY_BUFFER,
                      (GLsizeiptr)(index_data.size() * sizeof(GLuint)),
                      index_data.data(), GL_STATIC_DRAW);

    size_t offset = 0;
    for(size_t i = 0; i < layout_count; ++i) {
      const GLint components = (GLint)layout[i];
      objk_glVERTEXATTRIBPOINTER((GLuint)i, components, GL_FLOAT, GL_FALSE,
                                 stride * (GLsizei)sizeof(GLfloat),
                                 (const void*)(offset * sizeof(GLfloat)));
      objk_glENABLEVERTEXATTRIBARRAY((GLuint)i);
      offset += (size_t)components;
    }

    // unbind the VAO so later state changes cannot accidentally mutate it
    objk_glBINDVERTEXARRAY(0);

    APITools_SetIntValue(context, 0, (size_t)mesh);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_mesh_draw(VMContext& context) {
    if(!objk_gl_loaded) {
      objk_gl_fail(std::string("Mesh->Draw: ") + OBJK_GL_NOT_LOADED);
      return;
    }

    ObjkMesh* mesh = (ObjkMesh*)APITools_GetIntValue(context, 0);
    if(mesh && mesh->vao) {
      objk_glBINDVERTEXARRAY(mesh->vao);
      glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT, nullptr);
      objk_glBINDVERTEXARRAY(0);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_mesh_delete(VMContext& context) {
    ObjkMesh* mesh = (ObjkMesh*)APITools_GetIntValue(context, 0);
    if(mesh) {
      if(objk_gl_loaded) {
        if(mesh->ebo) { objk_glDELETEBUFFERS(1, &mesh->ebo); }
        if(mesh->vbo) { objk_glDELETEBUFFERS(1, &mesh->vbo); }
        if(mesh->vao) { objk_glDELETEVERTEXARRAYS(1, &mesh->vao); }
      }
      delete mesh;
    }
  }

  //
  // Builds a GL texture from an SDL_Surface, so image loading stays SDL's job
  // (Image->Load already handles PNG/JPEG) and this adds no decoding
  // dependency. The surface is converted to a known 32-bit RGBA layout first
  // rather than trusting whatever format the file happened to produce.
  //
  // slot 0 = texture handle (0 on failure), 1 = SDL_Surface*, 2 = magnification
  // filter, 3 = wrap mode for both S and T, 4 = non-zero to build mipmaps.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_texture_from_surface(VMContext& context) {
    APITools_SetIntValue(context, 0, 0);
    if(!objk_gl_loaded) {
      objk_gl_fail(std::string("Texture2D->New: ") + OBJK_GL_NOT_LOADED);
      return;
    }

    SDL_Surface* source = (SDL_Surface*)APITools_GetIntValue(context, 1);
    if(!source) {
      objk_gl_fail("Texture2D->New: the surface is null");
      return;
    }

    // Filter, wrap and mipmapping arrive from Objeck rather than being fixed
    // here. GL_REPEAT in particular was unreachable, which is what a wall or a
    // floor with a tiled texture actually needs.
    const GLint mag_filter = (GLint)APITools_GetIntValue(context, 2);
    const GLint wrap = (GLint)APITools_GetIntValue(context, 3);
    const bool mipmap = APITools_GetIntValue(context, 4) != 0;

    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_ABGR8888, 0);
    if(!rgba) {
      return;
    }

    // FLIP THE ROWS. An SDL surface stores row 0 at the TOP of the image, while
    // GL treats t=0 as the BOTTOM of the texture -- upload as-is and every image
    // renders upside down. A symmetric test pattern hides this completely, which
    // is exactly how it survived until someone loaded a real picture.
    //
    // Flipping here rather than asking callers to invert their texture
    // coordinates: an image should look like the file, and a caller mixing
    // hand-written geometry with loaded images should not have to remember which
    // convention each one follows.
    const int row_bytes = rgba->w * 4;
    std::vector<unsigned char> flipped((size_t)row_bytes * (size_t)rgba->h);
    const unsigned char* src = (const unsigned char*)rgba->pixels;
    for(int y = 0; y < rgba->h; ++y) {
      // rgba->pitch may exceed w*4 (row padding), so step by pitch, copy w*4
      const unsigned char* src_row = src + (size_t)y * (size_t)rgba->pitch;
      unsigned char* dest_row = flipped.data() + (size_t)(rgba->h - 1 - y) * (size_t)row_bytes;
      memcpy(dest_row, src_row, (size_t)row_bytes);
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());

    // The MINIFICATION filter is the one that has to change for mipmapping:
    // GL_LINEAR samples the base level only, so a mipmapped texture asked to
    // minify with it would build the chain and then never read it. Magnification
    // has no mip levels to choose between, so it takes the caller's filter as-is.
    GLint min_filter = mag_filter;
    if(mipmap) {
      objk_glGENERATEMIPMAP(GL_TEXTURE_2D);
      min_filter = (mag_filter == GL_NEAREST) ? GL_NEAREST_MIPMAP_NEAREST
                                              : GL_LINEAR_MIPMAP_LINEAR;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glBindTexture(GL_TEXTURE_2D, 0);

    SDL_FreeSurface(rgba);
    APITools_SetIntValue(context, 0, texture);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_texture_bind(VMContext& context) {
    if(!objk_gl_loaded) {
      objk_gl_fail(std::string("Texture2D->Bind: ") + OBJK_GL_NOT_LOADED);
      return;
    }

    const GLuint texture = (GLuint)APITools_GetIntValue(context, 0);
    const GLint unit = (GLint)APITools_GetIntValue(context, 1);
    objk_glACTIVETEXTURE(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_texture_delete(VMContext& context) {
    const GLuint texture = (GLuint)APITools_GetIntValue(context, 0);
    if(texture) {
      glDeleteTextures(1, &texture);
    }
  }

  //
  // glEnable/glDisable are GL 1.1, so they need no loaded pointer.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_enable(VMContext& context) {
    glEnable((GLenum)APITools_GetIntValue(context, 0));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_disable(VMContext& context) {
    glDisable((GLenum)APITools_GetIntValue(context, 0));
  }

  //
  // Reads one pixel out of the current framebuffer as packed 0xAARRGGBB.
  //
  // This is what lets a test assert that something was actually DRAWN rather
  // than merely that the program ran without crashing -- the distinction that
  // matters for a headless CI check. glReadPixels is GL 1.1.
  //
  // Note GL's origin is bottom-left, so y counts up from the bottom.
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_gl_read_pixel(VMContext& context) {
    const GLint x = (GLint)APITools_GetIntValue(context, 1);
    const GLint y = (GLint)APITools_GetIntValue(context, 2);

    GLubyte rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    const size_t packed = ((size_t)rgba[3] << 24) | ((size_t)rgba[0] << 16) |
                          ((size_t)rgba[1] << 8) | (size_t)rgba[2];
    APITools_SetIntValue(context, 0, packed);
  }
}
