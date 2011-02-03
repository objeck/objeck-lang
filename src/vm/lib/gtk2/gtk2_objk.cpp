#include <gtk/gtk.h>
#include <iostream>
#include "../../../utilities/dll_tools.h"

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
    DLLTools_MethodCall_Ptr callback;
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
  void og_button_new_with_label(long* data_array, long* op_stack, long* stack_pos, 
				DLLTools_MethodCall_Ptr callback) {
    char* name = DLLTools_GetStringValue(data_array, 1);
    GtkWidget* button = gtk_button_new_with_label(name);
    DLLTools_SetIntValue(data_array, 0, (long)button);
  }
  
  //
  // window functions
  //
  void og_window_set_title(long* data_array, long* op_stack, long* stack_pos, 
			   DLLTools_MethodCall_Ptr callback) {
    GtkWidget* window = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    char* name = DLLTools_GetStringValue(data_array, 1);    
    gtk_window_set_title(GTK_WINDOW(window), name);
  }

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

  void og_widget_hide(long* data_array, long* op_stack, long* stack_pos, 
		      DLLTools_MethodCall_Ptr callback) {
    GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    gtk_widget_hide(widget);
  }
  
  void og_signal_handler_disconnect(long* data_array, long* op_stack, long* stack_pos, 
				    DLLTools_MethodCall_Ptr callback) {
    GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    glong id = DLLTools_GetIntValue(data_array, 1);
    g_signal_handler_disconnect(widget, id);
  }

  void og_signal_connect(long* data_array, long* op_stack, long* stack_pos, 
			 DLLTools_MethodCall_Ptr callback) {
    long* self = (long*)DLLTools_GetIntValue(data_array, 1);
    int signal = DLLTools_GetIntValue(data_array, 2);
    int cls_id = DLLTools_GetFunctionValue(data_array, 3, CLS_ID);
    int mthd_id = DLLTools_GetFunctionValue(data_array, 3, MTHD_ID);
    
    callback_data* data = new callback_data;
    data->cls_id = cls_id;
    data->mthd_id = mthd_id;
    data->op_stack = op_stack;
    data->stack_pos = stack_pos;
    data->self = (long*)data_array[ARRAY_HEADER_OFFSET];
    data->callback = callback;
    
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
    DLLTools_SetIntValue(data_array, 0, id);
  }
  
  void og_signal_connect_swapped(long* data_array, long* op_stack, long* stack_pos, 
			 DLLTools_MethodCall_Ptr callback) {
    long* self = (long*)DLLTools_GetIntValue(data_array, 0);
    long* widget = (long*)DLLTools_GetIntValue(data_array, 1);
    int signal = DLLTools_GetIntValue(data_array, 2);
    int cls_id = DLLTools_GetIntValue(data_array, 3);
    int mthd_id = DLLTools_GetIntValue(data_array, 4);
    
    callback_data* data = new callback_data;
    data->cls_id = cls_id;
    data->mthd_id = mthd_id;
    data->op_stack = op_stack;
    data->stack_pos = stack_pos;
    data->self = (long*)data_array[ARRAY_HEADER_OFFSET + 1];
    data->callback = callback;
    
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
  void og_container_set_border_width(long* data_array, long* op_stack, long* stack_pos, 
				     DLLTools_MethodCall_Ptr callback) {
    GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    int width = DLLTools_GetIntValue(data_array, 1);
    gtk_container_set_border_width(GTK_CONTAINER (widget), width);
  }

  void og_container_get_border_width(long* data_array, long* op_stack, long* stack_pos, 
				     DLLTools_MethodCall_Ptr callback) {
    GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    gint width =gtk_container_get_border_width(GTK_CONTAINER (widget));
    DLLTools_SetIntValue(data_array, 1, width);
  }

  void og_container_add(long* data_array, long* op_stack, long* stack_pos, 
			DLLTools_MethodCall_Ptr callback) {
    GtkWidget* container = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 1);
    gtk_container_add(GTK_CONTAINER (container), widget);
  }

  void og_container_remove(long* data_array, long* op_stack, long* stack_pos, 
			DLLTools_MethodCall_Ptr callback) {
    GtkWidget* container = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 1);
    gtk_container_remove(GTK_CONTAINER (container), widget);
  }
  
  //
  // application functions
  //
  void og_main(long* data_array, long* op_stack, long* stack_pos, 
	       DLLTools_MethodCall_Ptr callback) {
    gtk_main();
  }
  
  void og_main_quit(long* data_array, long* op_stack, long* stack_pos, 
		    DLLTools_MethodCall_Ptr callback) {
    gtk_main_quit();
  }  

  void og_widget_destroy(long* data_array, long* op_stack, long* stack_pos, 
			 DLLTools_MethodCall_Ptr callback) {
    GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
    gtk_widget_destroy(widget);
  }
  
  //
  // callbacks
  //
  gboolean delete_callback_handler(GtkWidget* widget, GdkEvent* event, gpointer args) {
    callback_data* data = (callback_data*)args;
    DLLTools_MethodCall_Ptr callback = data->callback;

#ifdef _DEBUG
    cout << "@@@ Delete callback: cls_id=" << data->cls_id << ", mthd_id=" 
	 << data->mthd_id << ", self=" << data->self << " @@@" << endl;
#endif
    
    DLLTools_PushInt(data->op_stack, data->stack_pos, (long)data->self);
    (*callback)(data->op_stack, data->stack_pos, NULL, data->cls_id, data->mthd_id);
    
    return TRUE;
  }

  void callback_handler(GtkWidget* widget, gpointer args) {
    callback_data* data = (callback_data*)args;
    DLLTools_MethodCall_Ptr callback = data->callback;

#ifdef _DEBUG
    cout << "@@@ Callback: cls_id=" << data->cls_id << ", mthd_id=" 
	 << data->mthd_id << ", self=" << data->self << " @@@" << endl;
#endif
    
    DLLTools_PushInt(data->op_stack, data->stack_pos, (long)data->self);
    (*callback)(data->op_stack, data->stack_pos, NULL, data->cls_id, data->mthd_id);
    
    // TODO: "data" memory is freed when the application exits 
    // and need to stay allocated for the live of the appliction
  }
}
