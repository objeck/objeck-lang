#include <gtk/gtk.h>
#include <iostream>
#include "../../lib_api.h"

using namespace std;

extern "C" {
  static gboolean delete_callback_handler(GtkWidget* widget, GdkEvent* event, gpointer args);
  static void callback_handler(GtkWidget *widget, gpointer data);
  
  //
  // callback holder
  //
  typedef struct _callback_data {
    int cls_id;
    int mthd_id;
    long* op_stack;
    long* stack_pos;
    long* self;
    APITools_MethodCall_Ptr callback;
  } callback_data;
  
  //
  // loading and unloading of library
  //
  void load_lib() {
#ifdef _DEBUG
    cout << "@@@ Loading shared library @@@" << endl;
#endif
    int argc = 0; char** argv = NULL;
    gtk_init(&argc, &argv);
  }
  
  void unload_lib() {
#ifdef _DEBUG
    cout << "@@@ Unloading shared library @@@" << endl;
#endif
  }
  
  //
  // button functions
  //
  void og_button_new_with_label(VMContext& context) {
    char* name = APITools_GetStringValue(context, 1);
    GtkWidget* button = gtk_button_new_with_label(name);
    APITools_SetIntValue(context, 0, (long)button);
  }
  
  //
  // window functions
  //
  void og_window_set_title(VMContext& context) {
    GtkWidget* window = (GtkWidget*)APITools_GetIntValue(context, 0);
    char* name = APITools_GetStringValue(context, 1);    
    gtk_window_set_title(GTK_WINDOW(window), name);
  }

  void og_window_new(VMContext& context) {
    GtkWidget* window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    APITools_SetIntValue(context, 0, (long)window);
  }
  
  //
  // widget functions
  //
  void og_widget_show(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_show(widget);
  }

  void og_widget_hide(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_hide(widget);
  }
  
  void og_signal_handler_disconnect(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0);
    glong id = APITools_GetIntValue(context, 1);
    g_signal_handler_disconnect(widget, id);
  }

  void og_signal_connect(VMContext& context) {
    long* self = (long*)APITools_GetIntValue(context, 1);
    int signal = APITools_GetIntValue(context, 2);
    int cls_id = APITools_GetFunctionValue(context, 3, CLS_ID);
    int mthd_id = APITools_GetFunctionValue(context, 3, MTHD_ID);
    
    callback_data* data = new callback_data;
    data->cls_id = cls_id;
    data->mthd_id = mthd_id;
    data->op_stack = context.op_stack;
    data->stack_pos = context.stack_pos;
    data->self = (long*)context.data_array[ARRAY_HEADER_OFFSET];
    data->callback = context.method_call;
    
#ifdef _DEBUG
    cout << "@@@ Handler: cls_id=" << cls_id << ", mthd_id=" << mthd_id 
	 << ", signal=" << signal << ", self=" << data->self <<   " @@@" << endl;
#endif
    
    // find right handler
    glong id;
    switch(signal) {
    case -100:
      id = g_signal_connect((GtkWidget*)self, "delete-event",
			    G_CALLBACK(delete_callback_handler), data);
      break;
      
    case -99:
      id = g_signal_connect((GtkWidget*)self, "destroy", 
			    G_CALLBACK(callback_handler), data);
      break;
      
    case -98:
      id = g_signal_connect((GtkWidget*)self, "clicked", 
			    G_CALLBACK(callback_handler), data);
      break;
    }
    // set return
    APITools_SetIntValue(context, 0, id);
  }
  
  void og_signal_connect_swapped(VMContext& context) {
    long* self = (long*)APITools_GetIntValue(context, 0);
    long* widget = (long*)APITools_GetIntValue(context, 1);
    int signal = APITools_GetIntValue(context, 2);
    int cls_id = APITools_GetIntValue(context, 3);
    int mthd_id = APITools_GetIntValue(context, 4);
    
    callback_data* data = new callback_data;
    data->cls_id = cls_id;
    data->mthd_id = mthd_id;
    data->op_stack = context.op_stack;
    data->stack_pos = context.stack_pos;
    data->self = (long*)context.data_array[ARRAY_HEADER_OFFSET + 1];
    data->callback = context.method_call;
    
#ifdef _DEBUG
    cout << "@@@ Handler: cls_id=" << cls_id << ", mthd_id=" << mthd_id 
	 << ", signal=" << signal << ", self=" << data->self <<   " @@@" << endl;
#endif
    
    // find right handler
    switch(signal) {
    case -100:
      g_signal_connect((GtkWidget*)self, "destroy", 
		       G_CALLBACK(callback_handler), data);
      break;
      
    case -99:
      g_signal_connect((GtkWidget*)self, "clicked", 
		       G_CALLBACK(callback_handler), data);
      break;
    }
  }

  //
  // container functions
  //
  void og_container_set_border_width(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0);
    int width = APITools_GetIntValue(context, 1);
    gtk_container_set_border_width(GTK_CONTAINER (widget), width);
  }

  void og_container_get_border_width(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0);
    gint width =gtk_container_get_border_width(GTK_CONTAINER (widget));
    APITools_SetIntValue(context, 1, width);
  }

  void og_container_add(VMContext& context) {
    GtkWidget* container = (GtkWidget*)APITools_GetIntValue(context, 0);
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 1);
    gtk_container_add(GTK_CONTAINER (container), widget);
  }

  void og_container_remove(VMContext& context) {
    GtkWidget* container = (GtkWidget*)APITools_GetIntValue(context, 0);
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 1);
    gtk_container_remove(GTK_CONTAINER (container), widget);
  }
  
  //
  // application functions
  //
  void og_main(VMContext& context) {
    gtk_main();
  }
  
  void og_main_quit(VMContext& context) {
    gtk_main_quit();
  }  

  void og_widget_destroy(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_destroy(widget);
  }
  
  //
  // callbacks
  //
  gboolean delete_callback_handler(GtkWidget* widget, GdkEvent* event, gpointer args) {
    callback_data* data = (callback_data*)args;
    APITools_MethodCall_Ptr callback = data->callback;

#ifdef _DEBUG
    cout << "@@@ Delete callback: cls_id=" << data->cls_id << ", mthd_id=" 
	 << data->mthd_id << ", self=" << data->self << " @@@" << endl;
#endif
    
//	APITools_PushInt(data->op_stack, data->stack_pos, (long)data->self);
//    (*callback)(data->op_stack, data->stack_pos, NULL, data->cls_id, data->mthd_id);
    
    return TRUE;
  }

  void callback_handler(GtkWidget* widget, gpointer args) {
    callback_data* data = (callback_data*)args;
    APITools_MethodCall_Ptr callback = data->callback;

#ifdef _DEBUG
    cout << "@@@ Callback: cls_id=" << data->cls_id << ", mthd_id=" 
	 << data->mthd_id << ", self=" << data->self << " @@@" << endl;
#endif
    
//    APITools_PushInt(data->op_stack, data->stack_pos, (long)data->self);
//    (*callback)(data->op_stack, data->stack_pos, NULL, data->cls_id, data->mthd_id);
    
    // TODO: "data" memory is freed when the application exits 
    // and need to stay allocated for the live of the appliction
  }
}
