#include <gtk/gtk.h>
#include <iostream>
#include "../../../shared/dll_tools.h"

using namespace std;

extern "C" {
  // callback holder
  typedef struct _callback_data {
    int cls_id;
    int mthd_id;
    long* op_stack;
    long* stack_pos;
    long* self;
    DLLTools_MethodCall_Ptr callback;
  } callback_data;

  static void destroy_callback_handler(GtkWidget *widget, gpointer data);
  
  //
  // loading and unloading of library
  //
  void load_lib() {
    int argc = 0; char** argv = NULL;
    gtk_init(&argc, &argv);
  }
  
  void unload_lib() {
  }

  //
  // button functions
  //
  void og_button_new_with_label(long* data_array, long* op_stack, long* stack_pos, 
				DLLTools_MethodCall_Ptr callback) {
    char* name = DLLTools_GetStringValue(data_array, 1);
    GtkWidget* button = gtk_button_new_with_label(name);
    DLLTools_SetIntValue(data_array, 0, (long)button);
  }
  
  //
  // window functions
  //
  void og_window_new(long* data_array, long* op_stack, long* stack_pos, 
		     DLLTools_MethodCall_Ptr callback) {
    GtkWidget* window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    DLLTools_SetIntValue(data_array, 0, (long)window);
  }

  //
  // widget functions
  //
  void og_widget_show(long* data_array, long* op_stack, long* stack_pos, 
		      DLLTools_MethodCall_Ptr callback) {
    GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    gtk_widget_show(widget);
  }
  
  void og_signal_connect(long* data_array, long* op_stack, long* stack_pos, 
			 DLLTools_MethodCall_Ptr callback) {
    long* self = (long*)DLLTools_GetIntValue(data_array, 0);
    int signal = DLLTools_GetIntValue(data_array, 1);
    int cls_id = DLLTools_GetIntValue(data_array, 2);
    int mthd_id = DLLTools_GetIntValue(data_array, 3);
    
    callback_data* data = new callback_data;
    data->cls_id = cls_id;
    data->mthd_id = mthd_id;
    data->op_stack = op_stack;
    data->stack_pos = stack_pos;
    data->self = (long*)data_array[0];
    data->callback = callback;
    
    // find right handler
    switch(signal) {
    case -100:
      g_signal_connect((GtkWidget*)self, "destroy", 
		       G_CALLBACK(destroy_callback_handler), data);
      break;
    }
  }

  //
  // container functions
  //
  void og_container_width(long* data_array, long* op_stack, long* stack_pos, 
			  DLLTools_MethodCall_Ptr callback) {
    GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    int width = DLLTools_GetIntValue(data_array, 1);
    gtk_container_set_border_width(GTK_CONTAINER (widget), width);
  }

  void og_container_add(long* data_array, long* op_stack, long* stack_pos, 
			DLLTools_MethodCall_Ptr callback) {
    GtkWidget* container = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 1);
    gtk_container_add(GTK_CONTAINER (container), widget);
  }
  
  //
  // application functions
  //
  void og_main(long* data_array, long* op_stack, long* stack_pos, 
	       DLLTools_MethodCall_Ptr callback) {
    gtk_main();
  }
  
  //
  // callbacks
  //
  void destroy_callback_handler(GtkWidget* widget, gpointer args) {
    callback_data* data = (callback_data*)args;

    DLLTools_MethodCall_Ptr callback = data->callback;
    DLLTools_PushInt(data->op_stack, data->stack_pos, (long)data->self);
    (*callback)(data->op_stack, data->stack_pos, NULL, data->cls_id, data->mthd_id);
    
    delete data;
    data = NULL;
  }
}
