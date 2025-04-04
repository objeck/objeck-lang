/***************************************************************************
 * GTK3 PoC support for Objeck
 *
 * Copyright (c) 2023, Randy Hollines
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

#include "gtk3_test.h"

extern "C" {
  static ResourceManager* res_manager = nullptr;

  //
  // initialize library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib(VMContext& context) {
    if(!res_manager) {
      res_manager = new ResourceManager(context.alloc_managed_obj, context.call_method_by_id);
    }
  }

  //
  // release library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void unload_lib() {
    if(res_manager) {
      delete res_manager;
      res_manager = nullptr;
    }
  }

  //
  // GObject
  //
  static void callback_handler(GObject* handler, gpointer callback_data) {
    // well, want to reduce the code below... however what I learned from the SLD2
    // bindings is that UIs are order of magnitude slower then supporting program logic
    if(handler && callback_data) {
      std::pair<size_t, size_t*>* callback_params = (std::pair<size_t, size_t*>*)callback_data;
      size_t const mthd_cls_id = callback_params->first;
      const int cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
      const int mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;
      size_t* callback_data = callback_params->second;

      GType handler_ctype = G_TYPE_FROM_INSTANCE(handler);
      if(G_TYPE_IS_CLASSED(handler_ctype)) {
        const std::string handler_cname(g_type_name(handler_ctype));
        const char prefix_str[] = "Gtk";
        size_t handler_cname_prefix_offset = handler_cname.find(prefix_str);
        if(handler_cname_prefix_offset != std::string::npos) {
          std::pair<size_t*, long*> exec_stack_mem = res_manager->GetOpStackMemory();

          size_t* op_stack = exec_stack_mem.first;
          long* stack_pos = exec_stack_mem.second;
          
          const std::string post_objk_name = handler_cname.substr(handler_cname_prefix_offset + strlen(prefix_str));
          const std::string handler_objk_name("Gtk3." + post_objk_name);

          const APITools_AllocateObject_Ptr alloc_obj = res_manager->GetAllocateObject();
          size_t* gobject_obj = alloc_obj(BytesToUnicode(handler_objk_name).c_str(), op_stack, *stack_pos, true);
          if(gobject_obj) {
            gobject_obj[0] = (size_t)handler;

            // set stack
            op_stack[0] = (size_t)gobject_obj;
            op_stack[1] = (size_t)callback_data;
            (*stack_pos) = 2;

            // call method
            const APITools_MethodCallById_Ptr mthd_call_id = res_manager->GetMethodCallById();
            mthd_call_id(op_stack, stack_pos, nullptr, cls_id, mthd_id);
          }

          // clean up
          res_manager->ReleaseOpStackMemory(exec_stack_mem);
        }
      }
    }
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void gtk3_gobject_signal_connect(VMContext& context) {
    GObject* gobject = (GObject*)APITools_GetIntValue(context, 0);
    const std::string detailed_signal = UnicodeToBytes(APITools_GetStringValue(context, 1));
    size_t* handler_ptr = APITools_GetObjectValue(context, 2);
    size_t* data_ptr = APITools_GetObjectValue(context, 3);

    std::pair<size_t, size_t*>* user_callback_data = new std::pair<size_t, size_t*>;
    user_callback_data->first = handler_ptr[0];
    user_callback_data->second = data_ptr;

    g_signal_connect(gobject, detailed_signal.c_str(), G_CALLBACK(callback_handler), user_callback_data);
  }

  //
  // GtkApplication
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void gtk3_application_new(VMContext& context) {
    const std::string application_id = UnicodeToBytes(APITools_GetStringValue(context, 1));
    const GApplicationFlags flags = (GApplicationFlags)APITools_GetIntValue(context, 2);
    APITools_SetIntValue(context, 0, (size_t)gtk_application_new(application_id.c_str(), flags));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void gtk3_application_window_new(VMContext& context) {
    GtkApplication* application = (GtkApplication*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (size_t)gtk_application_window_new(application));
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void gtk3_application_run(VMContext& context) {
    GtkApplication* application = (GtkApplication*)APITools_GetIntValue(context, 0);
    
    std::vector<std::wstring> values = APITools_GetStringsValues(context, 1);
    if(values.empty()) {
      const int argc = 1;
      char** argv = new char*[argc + 1];
#ifdef _WIN32      
      argv[0] = _strdup("libobjk_gtk3.dll");
#else
      argv[0] = strdup("libobjk_gtk3.dll");
#endif
      res_manager->AddAllocation(argv[0]);

      argv[1] = 0;
      
      const int status = g_application_run(G_APPLICATION(application), argc, argv);
      APITools_SetIntValue(context, 0, status);
    }
    else {
      const size_t argc = values.size() + 1;
      char** argv = new char*[argc + 1];
#ifdef _WIN32      
      argv[0] = _strdup("libobjk_gtk3.dll");
#else
      argv[0] = strdup("libobjk_gtk3.dll");
#endif
      res_manager->AddAllocation(argv[0]);

      for(size_t i = 0; i < values.size(); ++i) {
#ifdef _WIN32        
        argv[i + 1] = _strdup(UnicodeToBytes(values[i]).c_str());
#else
        argv[i + 1] = strdup(UnicodeToBytes(values[i]).c_str());
#endif   
        res_manager->AddAllocation(argv[i + 1]);
      }
      argv[argc] = 0;

      const int status = g_application_run(G_APPLICATION(application), 1, argv);
      APITools_SetIntValue(context, 0, status);

      // free memory
      if(res_manager) {
        delete res_manager;
        res_manager = nullptr;
      }
    }
  }

  //
  // GtkWidget
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void gtk3_widget_show_all(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_show_all(widget);
  }

  //
  // GtkWindow
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void gtk3_window_set_title(VMContext& context) {
    GtkWindow* window = (GtkWindow*)APITools_GetIntValue(context, 0);
    const std::string title = UnicodeToBytes(APITools_GetStringValue(context, 1));
    gtk_window_set_title(window, title.c_str());
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void gtk3_window_set_default(VMContext& context) {
    GtkWindow* window = (GtkWindow*)APITools_GetIntValue(context, 0);
    const gint width = (gint)APITools_GetIntValue(context, 1);
    const gint height = (gint)APITools_GetIntValue(context, 1);
    gtk_window_set_default_size(window, width, height);
  }
}
