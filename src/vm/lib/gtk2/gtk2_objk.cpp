#include <gtk/gtk.h>
#include <iostream>
#include "../../lib_api.h"

using namespace std;

extern "C" {
  static gboolean event_callback_handler(GtkWidget* widget, GdkEvent* event, gpointer args);
  static void signal_callback_handler(GtkWidget *widget, gpointer data);
  
  //
  // callback holder
  //
  typedef struct _callback_data {
    VMContext context;
    long* widget;
    int cls_id;
    int mthd_id;
    long* params;
    APITools_MethodCallId_Ptr callback;
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
  // hbox functions
  //
  void og_gtk_hbox_new(VMContext& context) {
    gboolean homo = APITools_GetIntValue(context, 1);
    gint spacing = APITools_GetIntValue(context, 2);
    GtkWidget* hbox = gtk_hbox_new(homo, spacing);

#ifdef _DEBUG
    cout << "@@@ hbox_new: new=" << hbox << " @@@" << endl;
#endif

    APITools_SetIntValue(context, 0, (long)hbox);
  }

  void og_gtk_box_pack_start(VMContext& context) {
    GtkBox* box = (GtkBox*)APITools_GetIntValue(context, 0);
    GtkWidget* child = (GtkWidget*)APITools_GetIntValue(context, 1);
    gboolean expand = APITools_GetIntValue(context, 2);
    gboolean fill = APITools_GetIntValue(context, 3);
    guint padding  = APITools_GetIntValue(context, 4);
    
#ifdef _DEBUG
    cout << "@@@ hbox_pack_start box=" << box 
	 << ", child=" << child << " @@@" << endl;
#endif
    
    gtk_box_pack_start(box, child, expand, fill, padding);
  }
  
  //
  // button functions
  //
  void og_button_new_with_label(VMContext& context) {
    char* name = APITools_GetStringValue(context, 1);
    GtkWidget* button = gtk_button_new_with_label(name);
    APITools_SetIntValue(context, 0, (long)button);
  }

  void og_button_get_label(VMContext& context) {
    GtkButton* button = (GtkButton*)APITools_GetIntValue(context, 1); // raw widget
    const gchar* label = gtk_button_get_label(button);
    APITools_SetStringValue(context, 0, (char*)label);
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

  void og_widget_show_all(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_show_all(widget);
  }

  void og_widget_hide(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_hide(widget);
  }
  
  void og_signal_handler_disconnect(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0); // raw widget
    glong id = APITools_GetIntValue(context, 1);
    g_signal_handler_disconnect(widget, id);
  }

  void og_signal_connect(VMContext& context) {
    long* widget = (long*)APITools_GetObjectValue(context, 1); // widget
    const char* name = APITools_GetStringValue(context, 2); // name
    int cls_id = APITools_GetFunctionValue(context, 3, CLS_ID); // function
    int mthd_id = APITools_GetFunctionValue(context, 3, MTHD_ID); // function
    long* params = (long*)APITools_GetObjectValue(context, 4); // function params
    
    callback_data* data = new callback_data;
    data->context = context;
    data->callback = context.call_method_by_id;
    data->widget = widget;
    data->cls_id = cls_id;
    data->mthd_id = mthd_id;
    data->params = params;   
    
#ifdef _DEBUG
    cout << "@@@ Handler: data=" << data << "; widget=" << widget << "; name=" << name << "; mthd=(" 
	 << cls_id << "," << mthd_id << "); params=" << params << " @@@" << endl;
#endif
    
    // widget id
    glong id;
    if(strstr(name, "event")) {
      id = g_signal_connect((GtkWidget*)widget[0], name, G_CALLBACK(event_callback_handler), data);
    }
    else {
      id = g_signal_connect((GtkWidget*)widget[0], name, G_CALLBACK(signal_callback_handler), data);
    }
    
    // set return
    APITools_SetIntValue(context, 0, id);
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
  gboolean event_callback_handler(GtkWidget* widget, GdkEvent* event, gpointer args) {
    callback_data* data = (callback_data*)args;
    APITools_MethodCallId_Ptr callback = data->callback;
    
#ifdef _DEBUG
    cout << "@@@ Event: cls_id=" << data->cls_id << ", mthd_id=" << data->mthd_id 
	 << ", params=" << data->params << ", event=" << event << " @@@" << endl;
#endif
    
    APITools_PushInt(data->context, (long)data->params);
    APITools_PushInt(data->context, (long)data->widget);
    APITools_CallMethod(data->context, NULL, data->cls_id, data->mthd_id);

    // TODO: free memory
    
    return TRUE;
  }

  void signal_callback_handler(GtkWidget* widget, gpointer args) {
    callback_data* data = (callback_data*)args;
    APITools_MethodCallId_Ptr callback = data->callback;
    
#ifdef _DEBUG
    cout << "@@@ Signal: data=" << data << "; cls_id=" << data->cls_id << "; mthd_id=" 
	 << data->mthd_id << "; widget=" << data->widget << "; params=" 
	 << data->params << " @@@" << endl;
#endif
    
    APITools_PushInt(data->context, (long)data->params);
    APITools_PushInt(data->context, (long)data->widget);
    APITools_CallMethod(data->context, NULL, data->cls_id, data->mthd_id);
    
    // TODO: free memory
  }
}

