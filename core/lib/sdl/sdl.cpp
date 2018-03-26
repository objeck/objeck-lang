/***************************************************************************
 * SDL support for Objeck
 *
 * Copyright (c) 2015-2017, Randy Hollines
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
#include <SDL_image.h>
#include <stdio.h>
#include "../../vm/lib_api.h"

using namespace std;

extern "C" {
  void sdl_point_raw_read(SDL_Point* point, long* point_obj);
  void sdl_point_raw_write(SDL_Point* point, long* point_obj);
  void sdl_rect_raw_read(SDL_Rect* rect, long* rect_obj);
  void sdl_rect_raw_write(SDL_Rect* rect, long* rect_obj);
  void sdl_pixel_format_raw_read(SDL_PixelFormat* pixel_format, long* pixel_format_obj);
  void sdl_pixel_format_raw_write(SDL_PixelFormat* pixel_format, long* pixel_format_obj);
  void sdl_palette_raw_read(SDL_Palette* palette_format, long* palette_format_obj);
  void sdl_palette_raw_write(SDL_Palette* palette_format, long* palette_format_obj);

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
  void sdl_core_gl_get_swap_interval(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GL_GetSwapInterval());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_set_swap_interval(VMContext& context) {
    const int interval = APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GL_SetSwapInterval(interval));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_get_current(VMContext& context) {
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    APITools_SetIntValue(context, 0, (long)window);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_get_attribute(VMContext& context) {
    const int attr = APITools_GetIntValue(context, 1);
    
    int value;
    if(!SDL_GL_GetAttribute((SDL_GLattr)attr, &value)) {
      APITools_SetIntValue(context, 0, value);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_set_attribute(VMContext& context) {
    const int attr = APITools_GetIntValue(context, 1);
    const int value = APITools_GetIntValue(context, 2);
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
    const wstring w_extension = APITools_GetStringValue(context, 1);
    const string extension(w_extension.begin(), w_extension.end());
    const int return_value = SDL_GL_ExtensionSupported(extension.c_str());
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_core_gl_load_library(VMContext& context) {
    const wstring w_path = APITools_GetStringValue(context, 1);
    const string path(w_path.begin(), w_path.end());
    const int return_value = SDL_GL_LoadLibrary(path.c_str());
    APITools_SetIntValue(context, 0, return_value);
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

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_creatergb(VMContext& context) {
    const Uint32 flags = APITools_GetIntValue(context, 1);
    const int width = APITools_GetIntValue(context, 2);
    const int height = APITools_GetIntValue(context, 3);
    const int depth = APITools_GetIntValue(context, 4);
    const Uint32 Rmask = APITools_GetIntValue(context, 5);
    const Uint32 Gmask = APITools_GetIntValue(context, 6);
    const Uint32 Bmask = APITools_GetIntValue(context, 7);
    const Uint32 Amask = APITools_GetIntValue(context, 8);

    SDL_Surface* surface = SDL_CreateRGBSurface(flags, width, height, depth, Rmask, Gmask, Bmask, Amask);
    APITools_SetIntValue(context, 0, (long)surface);
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_free(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    SDL_FreeSurface(surface);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_get_pixel_format(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    if(surface) {
      APITools_SetIntValue(context, 0, (long)surface->format);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_set_palette(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    const long* palette_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Palette* palette = palette_obj ? (SDL_Palette*)palette_obj[0] : NULL;

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
  void sdl_surface_loadbmp(VMContext& context) {
    const wstring w_file = APITools_GetStringValue(context, 1);
    const string file(w_file.begin(), w_file.end());

    SDL_Surface* surface = SDL_LoadBMP(file.c_str());
    APITools_SetIntValue(context, 0, (long)surface);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_savebmp(VMContext& context) {
    size_t* surface_obj = APITools_GetObjectValue(context, 1);
    SDL_Surface* surface = surface_obj ? (SDL_Surface*)surface_obj[0] : NULL;
    const wstring w_file = APITools_GetStringValue(context, 2);
    const string file(w_file.begin(), w_file.end());

    APITools_SetIntValue(context, 0, SDL_SaveBMP(surface, file.c_str()));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_setrle(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int flag = APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetSurfaceRLE(surface, flag));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_set_color_key(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int flag = APITools_GetIntValue(context, 2);
    const int key = APITools_GetIntValue(context, 3);
    APITools_SetIntValue(context, 0, SDL_SetColorKey(surface, flag, key));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_get_color_key(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    Uint32 key = APITools_GetIntValue(context, 2);
    if(!SDL_GetColorKey(surface, &key)) {
      APITools_SetIntValue(context, 0, key);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_set_color_mod(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int r = APITools_GetIntValue(context, 2);
    const int g = APITools_GetIntValue(context, 3);
    const int b = APITools_GetIntValue(context, 4);
    APITools_SetIntValue(context, 0, SDL_SetSurfaceColorMod(surface, r, g, b));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_get_color_mod(VMContext& context) {
    Uint8 r, g, b;
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GetSurfaceColorMod(surface, &r, &g, &b));
    APITools_SetIntValue(context, 1, r);
    APITools_SetIntValue(context, 2, g);
    APITools_SetIntValue(context, 3, b);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_set_alpha_mod(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    const int alpha = APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetSurfaceAlphaMod(surface, alpha));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_get_alpha_mod(VMContext& context) {
    Uint8 alpha;
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    if(!SDL_GetSurfaceAlphaMod(surface, &alpha)) {
      APITools_SetIntValue(context, 0, alpha);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
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
    SDL_BlendMode blendMode;
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    if(!SDL_GetSurfaceBlendMode(surface, &blendMode)) {
      APITools_SetIntValue(context, 0, blendMode);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_get_clip_rect(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 0);

    const long* rect_obj = (long*)APITools_GetObjectValue(context, 1);
    SDL_Rect* rect = rect_obj ? (SDL_Rect*)rect_obj[0] : NULL;
        
    SDL_GetClipRect(surface, rect);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_set_clip_rect(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    const long* rect_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* rect = rect_obj ? (SDL_Rect*)rect_obj[0] : NULL;

    APITools_SetIntValue(context, 0, SDL_SetClipRect(surface, rect));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_surface_convert(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    const long* fmt_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_PixelFormat* fmt = fmt_obj ? (SDL_PixelFormat*)fmt_obj[0] : NULL;
    
    const int flags = APITools_GetIntValue(context, 3);
    
    APITools_SetIntValue(context, 0, (long)SDL_ConvertSurface(surface, fmt, flags));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_fill_rect(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);
    
    const long* rect_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* rect = rect_obj ? (SDL_Rect*)rect_obj[0] : NULL;

    const int color = APITools_GetIntValue(context, 3);
    
    APITools_SetIntValue(context, 0, SDL_FillRect(surface, rect, color));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_upper_blit(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    const long* srcrect_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* srcrect = srcrect_obj ? (SDL_Rect*)srcrect_obj[0] : NULL;

    const long* dst_obj = (long*)APITools_GetObjectValue(context, 3);
    SDL_Surface* dst = dst_obj ? (SDL_Surface*)dst_obj[0] : NULL;

    const long* dstrect_obj = (long*)APITools_GetObjectValue(context, 4);
    SDL_Rect* dstrect = dstrect_obj ? (SDL_Rect*)dstrect_obj[0] : NULL;

    APITools_SetIntValue(context, 0, SDL_UpperBlit(surface, srcrect, dst, dstrect));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_lower_blit(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    const long* srcrect_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* srcrect = srcrect_obj ? (SDL_Rect*)srcrect_obj[0] : NULL;

    const long* dst_obj = (long*)APITools_GetObjectValue(context, 3);
    SDL_Surface* dst = dst_obj ? (SDL_Surface*)dst_obj[0] : NULL;

    const long* dstrect_obj = (long*)APITools_GetObjectValue(context, 4);
    SDL_Rect* dstrect = dstrect_obj ? (SDL_Rect*)dstrect_obj[0] : NULL;

    APITools_SetIntValue(context, 0, SDL_LowerBlit(surface, srcrect, dst, dstrect));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_soft_stretch(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    const long* srcrect_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* srcrect = srcrect_obj ? (SDL_Rect*)srcrect_obj[0] : NULL;

    const long* dst_obj = (long*)APITools_GetObjectValue(context, 3);
    SDL_Surface* dst = dst_obj ? (SDL_Surface*)dst_obj[0] : NULL;

    const long* dstrect_obj = (long*)APITools_GetObjectValue(context, 4);
    SDL_Rect* dstrect = dstrect_obj ? (SDL_Rect*)dstrect_obj[0] : NULL;

    APITools_SetIntValue(context, 0, SDL_LowerBlit(surface, srcrect, dst, dstrect));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_upper_blit_scaled(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    const long* srcrect_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* srcrect = srcrect_obj ? (SDL_Rect*)srcrect_obj[0] : NULL;

    const long* dst_obj = (long*)APITools_GetObjectValue(context, 3);
    SDL_Surface* dst = dst_obj ? (SDL_Surface*)dst_obj[0] : NULL;

    const long* dstrect_obj = (long*)APITools_GetObjectValue(context, 4);
    SDL_Rect* dstrect = dstrect_obj ? (SDL_Rect*)dstrect_obj[0] : NULL;

    APITools_SetIntValue(context, 0, SDL_UpperBlitScaled(surface, srcrect, dst, dstrect));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_surface_lower_blit_scaled(VMContext& context) {
    SDL_Surface* surface = (SDL_Surface*)APITools_GetIntValue(context, 1);

    const long* srcrect_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* srcrect = srcrect_obj ? (SDL_Rect*)srcrect_obj[0] : NULL;

    const long* dst_obj = (long*)APITools_GetObjectValue(context, 3);
    SDL_Surface* dst = dst_obj ? (SDL_Surface*)dst_obj[0] : NULL;

    const long* dstrect_obj = (long*)APITools_GetObjectValue(context, 4);
    SDL_Rect* dstrect = dstrect_obj ? (SDL_Rect*)dstrect_obj[0] : NULL;

    APITools_SetIntValue(context, 0, SDL_LowerBlitScaled(surface, srcrect, dst, dstrect));
  }

  //
  // SDL_PixelFormat
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_pixel_format_new(VMContext& context) {
    SDL_PixelFormat* pixel_format = new SDL_PixelFormat;
    APITools_SetIntValue(context, 0, (long)pixel_format);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_pixel_format_free(VMContext& context) {
    SDL_PixelFormat* pixel_format = (SDL_PixelFormat*)APITools_GetIntValue(context, 0);
    delete pixel_format;
  }

  void sdl_pixel_format_raw_read(SDL_PixelFormat* pixel_format, long* pixel_format_obj) {
    if(pixel_format_obj) {
      pixel_format_obj[1] = pixel_format->format;
      if(pixel_format->palette) {
        long* palette_obj = (long*)pixel_format_obj[2];
        sdl_palette_raw_read(pixel_format->palette, palette_obj);
      }
      else {
        pixel_format_obj[2] = 0;
      }
      pixel_format_obj[3] = pixel_format->BitsPerPixel;
      pixel_format_obj[4] = pixel_format->BytesPerPixel;
      pixel_format_obj[5] = pixel_format->Rmask;
      pixel_format_obj[6] = pixel_format->Gmask;
      pixel_format_obj[7] = pixel_format->Bmask;
      pixel_format_obj[8] = pixel_format->Amask;
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_pixel_format_read(VMContext& context) {
    SDL_PixelFormat* pixel_format = (SDL_PixelFormat*)APITools_GetIntValue(context, 0);
    long* pixel_format_obj = (long*)APITools_GetObjectValue(context, 1);
    sdl_pixel_format_raw_read(pixel_format, pixel_format_obj);
  }

  void sdl_pixel_format_raw_write(SDL_PixelFormat* pixel_format, long* pixel_format_obj) {
    if(pixel_format_obj) {
      if(pixel_format_obj) {
        pixel_format_obj[1] = pixel_format->format;
        if(pixel_format->palette) {
          long* palette_obj = (long*)pixel_format_obj[2];
          sdl_palette_raw_write(pixel_format->palette, palette_obj);
        }
        pixel_format->BitsPerPixel = (Uint8)pixel_format_obj[3];
        pixel_format->BytesPerPixel = (Uint8)pixel_format_obj[4];
        pixel_format->Rmask = pixel_format_obj[5];
        pixel_format->Gmask = pixel_format_obj[6];
        pixel_format->Bmask = pixel_format_obj[7];
        pixel_format->Amask = pixel_format_obj[8];
      }
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_pixel_format_write(VMContext& context) {
    SDL_PixelFormat* pixel_format = (SDL_PixelFormat*)APITools_GetIntValue(context, 0);
    long* pixel_format_obj = (long*)APITools_GetObjectValue(context, 1);
    sdl_pixel_format_raw_write(pixel_format, pixel_format_obj);
  }

  //
  // SDL_Point
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_point_new(VMContext& context) {
    SDL_Point* point = new SDL_Point;
    APITools_SetIntValue(context, 0, (long)point);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_point_free(VMContext& context) {
    SDL_Point* point = (SDL_Point*)APITools_GetIntValue(context, 0);
    delete point;
  }
  
  void sdl_point_raw_read(SDL_Point* point, long* point_obj) {
    if(point_obj) {
      point_obj[1] = point->x;
      point_obj[2] = point->y;
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_point_read(VMContext& context) {
    SDL_Point* point = (SDL_Point*)APITools_GetIntValue(context, 0);
    long* point_obj = (long*)APITools_GetObjectValue(context, 1);
    sdl_point_raw_read(point, point_obj);
  }

  void sdl_point_raw_write(SDL_Point* point, long* point_obj) {
    if(point_obj) {
      point->x = point_obj[1];
      point->y = point_obj[2];
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_point_write(VMContext& context) {
    SDL_Point* point = (SDL_Point*)APITools_GetIntValue(context, 0);
    long* point_obj = (long*)APITools_GetObjectValue(context, 1);
    sdl_point_raw_write(point, point_obj);
  }
  
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_point_x(VMContext& context) {
    SDL_Point* point = (SDL_Point*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, point->x);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_point_y(VMContext& context) {
    SDL_Point* point = (SDL_Point*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, point->y);
  }

  //
  // SDL_Rect
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_rect_new(VMContext& context) {
    SDL_Rect* rect = new SDL_Rect;
    APITools_SetIntValue(context, 0, (long)rect);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_rect_free(VMContext& context) {
    SDL_Rect* rect = new SDL_Rect;
    delete rect;
  }

  void sdl_rect_raw_read(SDL_Rect* rect, long* rect_obj) {
    if(rect_obj) {
      rect_obj[1] = rect->x;
      rect_obj[2] = rect->y;
      rect_obj[3] = rect->w;
      rect_obj[4] = rect->h;
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_rect_read(VMContext& context) {
    SDL_Rect* rect = (SDL_Rect*)APITools_GetIntValue(context, 0);
    long* rect_obj = (long*)APITools_GetObjectValue(context, 1);
    sdl_rect_raw_read(rect, rect_obj);
  }
  
  void sdl_rect_raw_write(SDL_Rect* rect, long* rect_obj) {
    if(rect_obj) {
      rect->x = rect_obj[1];
      rect->y = rect_obj[2];
      rect->w = rect_obj[3];
      rect->h = rect_obj[4];
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_rect_write(VMContext& context) {
    SDL_Rect* rect = (SDL_Rect*)APITools_GetIntValue(context, 0);
    long* rect_obj = (long*)APITools_GetObjectValue(context, 1);
    sdl_rect_raw_write(rect, rect_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_rect_x(VMContext& context) {
    SDL_Rect* rect = (SDL_Rect*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, rect->x);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_rect_y(VMContext& context) {
    SDL_Rect* rect = (SDL_Rect*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, rect->y);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_rect_h(VMContext& context) {
    SDL_Rect* rect = (SDL_Rect*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, rect->w);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_rect_has_intersection(VMContext& context) {
    SDL_Rect* rect = (SDL_Rect*)APITools_GetIntValue(context, 1);

    const long* B_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* B = B_obj ? (SDL_Rect*)B_obj[0] : NULL;

    APITools_SetIntValue(context, 0, SDL_HasIntersection(rect, B));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_rect_intersect(VMContext& context) {
    SDL_Rect* rect = (SDL_Rect*)APITools_GetIntValue(context, 1);

    const long* B_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* B = B_obj ? (SDL_Rect*)B_obj[0] : NULL;

    SDL_Rect* C = new SDL_Rect;
    if(SDL_IntersectRect(rect, B, C)) {
      APITools_SetIntValue(context, 0, (long)C);
    }
    else {
      APITools_SetIntValue(context, 0, 0);
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_rect_union(VMContext& context) {
    SDL_Rect* rect = (SDL_Rect*)APITools_GetIntValue(context, 1);
    
    const long* B_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* B = B_obj ? (SDL_Rect*)B_obj[0] : NULL;

    SDL_Rect* C = new SDL_Rect;
    SDL_UnionRect(rect, B, C);
    APITools_SetIntValue(context, 0, (long)C);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_rect_enclose_points(VMContext& context) {
    const long* points_obj = (long*)APITools_GetObjectValue(context, 1);
    SDL_Point* points = points_obj ? (SDL_Point*)points_obj[0] : NULL;

    const int count = APITools_GetIntValue(context, 2);
    
    const long* clip_obj = (long*)APITools_GetObjectValue(context, 3);
    SDL_Rect* clip = clip_obj ? (SDL_Rect*)clip_obj[0] : NULL;

    SDL_Rect* result = new SDL_Rect;
    const int return_value = SDL_EnclosePoints(points, count, clip, result);
    APITools_SetIntValue(context, 0, (long)result);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_rect_intersect_and_line(VMContext& context) {
    SDL_Rect* rect = (SDL_Rect*)APITools_GetIntValue(context, 1);
    int X1 = APITools_GetIntValue(context, 2);
    int Y1 = APITools_GetIntValue(context, 3);
    int X2 = APITools_GetIntValue(context, 4);
    int Y2 = APITools_GetIntValue(context, 5);

    APITools_SetIntValue(context, 0, SDL_IntersectRectAndLine(rect, &X1, &Y1, &X2, &Y2));

    APITools_SetIntValue(context, 2, X1);
    APITools_SetIntValue(context, 3, Y1);
    APITools_SetIntValue(context, 4, X2);
    APITools_SetIntValue(context, 5, Y2);
  }

  //
  // Display
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_num_video_drivers(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GetNumVideoDrivers());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_video_driver(VMContext& context) {
    const int index = APITools_GetIntValue(context, 1);
    const string value = SDL_GetVideoDriver(index);
    const wstring w_value(value.begin(), value.end());
    APITools_SetStringValue(context, 0, w_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_video_init(VMContext& context) {
    const wstring w_driver_name = APITools_GetStringValue(context, 1);
    const string driver_name(w_driver_name.begin(), w_driver_name.end());
    APITools_SetIntValue(context, 0, SDL_VideoInit(driver_name.c_str()));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_video_quit(VMContext& context) {
    SDL_VideoQuit();
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_current_video_driver(VMContext& context) {
    const string value = SDL_GetCurrentVideoDriver();
    const wstring w_value(value.begin(), value.end());
    APITools_SetStringValue(context, 0, w_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_num_video_displays(VMContext& context) {
    APITools_SetIntValue(context, 0, SDL_GetNumVideoDisplays());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_display_name(VMContext& context) {
    const int displayIndex = APITools_GetIntValue(context, 1);
    const string value = SDL_GetDisplayName(displayIndex);
    const wstring w_value(value.begin(), value.end());
    APITools_SetStringValue(context, 0, w_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_display_bounds(VMContext& context) {
    const int displayIndex = APITools_GetIntValue(context, 1);

    const long* rect_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* rect = rect_obj ? (SDL_Rect*)rect_obj[0] : NULL;
        
    APITools_SetIntValue(context, 0, SDL_GetDisplayBounds(displayIndex, rect));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_num_display_modes(VMContext& context) {
    const int displayIndex = APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_GetNumDisplayModes(displayIndex));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_display_mode(VMContext& context) {
    const int displayIndex = APITools_GetIntValue(context, 1);
    const int modeIndex = APITools_GetIntValue(context, 2);

    const long* mode_obj = (long*)APITools_GetObjectValue(context, 3);
    SDL_DisplayMode* mode = mode_obj ? (SDL_DisplayMode*)mode_obj[0] : NULL;
    
    APITools_SetIntValue(context, 0, SDL_GetDisplayMode(displayIndex, modeIndex, mode));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_current_display_mode(VMContext& context) {
    const int displayIndex = APITools_GetIntValue(context, 1);

    const long* mode_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_DisplayMode* mode = mode_obj ? (SDL_DisplayMode*)mode_obj[0] : NULL;
    
    APITools_SetIntValue(context, 0, SDL_GetCurrentDisplayMode(displayIndex, mode));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_closest_display_mode(VMContext& context) {
    const int displayIndex = APITools_GetIntValue(context, 1);

    const long* mode_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_DisplayMode* mode = mode_obj ? (SDL_DisplayMode*)mode_obj[0] : NULL;

    const long* closest_obj = (long*)APITools_GetObjectValue(context, 3);
    SDL_DisplayMode* closest = closest_obj ? (SDL_DisplayMode*)closest_obj[0] : NULL;
    
    APITools_SetIntValue(context, 0, (long)SDL_GetClosestDisplayMode(displayIndex, mode, closest));
  }

  //
  // Window
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_create(VMContext& context) {
    const wstring w_title = APITools_GetStringValue(context, 1);
    const string title(w_title.begin(), w_title.end());
    const int x = APITools_GetIntValue(context, 2);
    const int y = APITools_GetIntValue(context, 3);
    const int w = APITools_GetIntValue(context, 4);
    const int h = APITools_GetIntValue(context, 5);
    const Uint32 flags = APITools_GetIntValue(context, 6);
    SDL_Window* window = SDL_CreateWindow(title.c_str(), x, y, w, h, flags);
    APITools_SetIntValue(context, 0, (long)window);
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
    const long* mode_obj = (long*)APITools_GetObjectValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetWindowDisplayMode(window, (SDL_DisplayMode*)mode_obj));
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
    const Uint32 id = APITools_GetIntValue(context, 1);
    SDL_Window* window = SDL_GetWindowFromID(id);
    APITools_SetIntValue(context, 0, (long)window);
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
    const wstring w_title = APITools_GetStringValue(context, 1);
    const string title(w_title.begin(), w_title.end());
    SDL_SetWindowTitle(window, title.c_str());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_set_icon(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);

    const long* icon_obj = (long*)APITools_GetObjectValue(context, 1);
    SDL_Surface* icon = icon_obj ? (SDL_Surface*)icon_obj[0] : NULL;
    
    SDL_SetWindowIcon(window, icon);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_set_position(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 0);
    const int x = APITools_GetIntValue(context, 1);
    const int y = APITools_GetIntValue(context, 2);
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
    const int w = APITools_GetIntValue(context, 1);
    const int h = APITools_GetIntValue(context, 2);
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
    const int min_w = APITools_GetIntValue(context, 1);
    const int min_h = APITools_GetIntValue(context, 2);
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
    const int max_w = APITools_GetIntValue(context, 1);
    const int max_h = APITools_GetIntValue(context, 2);
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
    const int flags = APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_SetWindowFullscreen(window, flags));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_surface(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);
    SDL_Surface* surface = SDL_GetWindowSurface(window);
    APITools_SetIntValue(context, 0, (long)surface);
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
    void sdl_window_update_surface_rects(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);

    const long* rects_obj = (long*)APITools_GetObjectValue(context, 2);
    SDL_Rect* rects = rects_obj ? (SDL_Rect*)rects_obj[0] : NULL;

    const int numrects = APITools_GetIntValue(context, 3);
    
    APITools_SetIntValue(context, 0, SDL_UpdateWindowSurfaceRects(window, rects, numrects));
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
    APITools_SetIntValue(context, 1, red);
    APITools_SetIntValue(context, 2, green);
    APITools_SetIntValue(context, 3, blue);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_window_get_gamma_ramp(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetIntValue(context, 1);

    Uint16 red, green, blue;
    APITools_SetIntValue(context, 0, SDL_GetWindowGammaRamp(window, &red, &green, &blue));
    APITools_SetIntValue(context, 1, red);
    APITools_SetIntValue(context, 2, green);
    APITools_SetIntValue(context, 3, blue);
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

  //
  // Event
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_event_new(VMContext& context) {
    SDL_Event* event = new SDL_Event;
    APITools_SetIntValue(context, 0, (long)event);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_free(VMContext& context) {
    SDL_Event* events = (SDL_Event*)APITools_GetIntValue(context, 1);
    delete events;
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_type(VMContext& context) {
    SDL_Event* events = (SDL_Event*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, events->type);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_event_key(VMContext& context) {
    SDL_Event* events = (SDL_Event*)APITools_GetIntValue(context, 0);
    long* key_obj = (long*)APITools_GetObjectValue(context, 1);
    if(key_obj) {
      key_obj[0] = events->key.type;
      key_obj[1] = events->key.timestamp;
      key_obj[2] = events->key.windowID;
      key_obj[3] = events->key.state;
      key_obj[4] = events->key.repeat;
      long* key_sym_obj = (long*)key_obj[5];
      if(key_sym_obj) {
        key_sym_obj[0] = events->key.keysym.scancode;
        key_sym_obj[1] = events->key.keysym.sym;
        key_sym_obj[2] = events->key.keysym.mod;
      }
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_event_peeps(VMContext& context) {
    SDL_Event* events = (SDL_Event*)APITools_GetIntValue(context, 1);
    const int numevents = APITools_GetIntValue(context, 2);
    const SDL_eventaction action = (SDL_eventaction)APITools_GetIntValue(context, 3);
    const int minType = APITools_GetIntValue(context, 4);
    const int maxType = APITools_GetIntValue(context, 5);
    APITools_SetIntValue(context, 0, SDL_PeepEvents(events, numevents, action, minType, maxType));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_event_has(VMContext& context) {
    const int type = APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_HasEvent(type));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_event_flush(VMContext& context) {
    SDL_FlushEvent(APITools_GetIntValue(context, 0));
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
    const int timeout = APITools_GetIntValue(context, 2);
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
    const int type = APITools_GetIntValue(context, 1);
    const int state = APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, SDL_EventState(type, state));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
    void sdl_event_registers(VMContext& context) {
    const int numevents = APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, SDL_RegisterEvents(numevents));
  }

  //
  // Palette
  //
  void sdl_palette_raw_read(SDL_Palette* palette, long* palette_obj) {
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
    long* palette_obj = (long*)APITools_GetObjectValue(context, 1);
    sdl_palette_raw_read(palette, palette_obj);
  }
  
  void sdl_palette_raw_write(SDL_Palette* palette, long* palette_obj) {
    if(palette_obj) {
      palette->ncolors = palette_obj[0];
//      palette->format = palette_obj[1];
      palette->version = (Uint8)palette_obj[2];
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_palette_write(VMContext& context) {
    SDL_Palette* palette = (SDL_Palette*)APITools_GetIntValue(context, 0);
    long* palette_obj = (long*)APITools_GetObjectValue(context, 1);
    sdl_palette_raw_read(palette, palette_obj);
  }

  //
  // Image library
  // 
  // 
  #ifdef _WIN32
  __declspec(dllexport)
  #endif
  void sdl_image_init(VMContext& context) {
    const int flags = APITools_GetIntValue(context, 1);
    const int return_value = IMG_Init(flags);
    APITools_SetIntValue(context, 0, return_value);
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
    const wstring wfile = APITools_GetStringValue(context, 1);
    const string file(wfile.begin(), wfile.end());
    APITools_SetIntValue(context, 0, (long)IMG_Load(file.c_str()));
  }

  //
  // Renderer
  //
  #ifdef _WIN32
  __declspec(dllexport)
  #endif
  void sdl_renderer_create(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetObjectValue(context, 1);
    const int index = APITools_GetIntValue(context, 2);
    const Uint32 flags = APITools_GetIntValue(context, 3);
    SDL_Renderer* return_value = SDL_CreateRenderer(window, index, flags);
    APITools_SetIntValue(context, 0, (long)return_value);
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
   void sdl_renderer_get_num_render_drivers(VMContext& context) {
    const int return_value = SDL_GetNumRenderDrivers();
    APITools_SetIntValue(context, 0, return_value);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_get_render_driver_info(VMContext& context) {
    const int index = APITools_GetIntValue(context, 1);
    long* info_obj = (long*)APITools_GetObjectValue(context, 2);

    if(info_obj) {
      SDL_RendererInfo info;
      const int return_value = SDL_GetRenderDriverInfo(index, &info);

      string name(info.name);
      wstring wname(name.begin(), name.end());

      info_obj[0] = (long)APITools_CreateStringValue(context, wname);
      info_obj[1] = info.flags;
      info_obj[2] = info.num_texture_formats;

      long* dest_formats = (long*)info_obj[3];
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
    SDL_Surface* surface = (SDL_Surface*)APITools_GetObjectValue(context, 1);
    APITools_SetIntValue(context, 0, (long)SDL_CreateSoftwareRenderer(surface));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_get(VMContext& context) {
    SDL_Window* window = (SDL_Window*)APITools_GetObjectValue(context, 1);
    APITools_SetIntValue(context, 0, (long)SDL_GetRenderer(window));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_renderer_get_info(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    long* info_obj = (long*)APITools_GetObjectValue(context, 2);

    if(info_obj) {
      SDL_RendererInfo info;
      const int return_value = SDL_GetRendererInfo(renderer, &info);

      string name(info.name);
      wstring wname(name.begin(), name.end());

      info_obj[0] = (long)APITools_CreateStringValue(context, wname);
      info_obj[1] = info.flags;
      info_obj[2] = info.num_texture_formats;

      long* dest_formats = (long*)info_obj[3];
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
  void sdl_renderer_set_render_draw_color(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetIntValue(context, 1);
    const int r = APITools_GetIntValue(context, 2);
    const int g = APITools_GetIntValue(context, 3);
    const int b = APITools_GetIntValue(context, 4);
    const int a = APITools_GetIntValue(context, 5);
    const int return_value = SDL_SetRenderDrawColor(renderer, r, g, b, a);
    APITools_SetIntValue(context, 0, return_value);
  }

  //
  // texture
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_create(VMContext& context) {
    SDL_Renderer* renderer = (SDL_Renderer*)APITools_GetObjectValue(context, 1);
    const int format = APITools_GetIntValue(context, 2);
    const int access = APITools_GetIntValue(context, 3);
    const int w = APITools_GetIntValue(context, 4);
    const int h = APITools_GetIntValue(context, 5);
    APITools_SetIntValue(context, 0, (long)SDL_CreateTexture(renderer, format, access, w, h));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_query(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);
    
    Uint32 format; int access, w, h;
    APITools_SetIntValue(context, 0, SDL_QueryTexture(texture, &format, &access, &w, &h));

    APITools_SetIntValue(context, 2, format);
    APITools_SetIntValue(context, 3, access);
    APITools_SetIntValue(context, 4, w);
    APITools_SetIntValue(context, 5, h);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void sdl_texture_set_color_mod(VMContext& context) {
    SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);
    const int r = APITools_GetIntValue(context, 2);
    const int g = APITools_GetIntValue(context, 3);
    const int b = APITools_GetIntValue(context, 4);
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
    const int alpha = APITools_GetIntValue(context, 2);
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


}