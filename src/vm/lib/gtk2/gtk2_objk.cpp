#include <gtk/gtk.h>
#include <cairo.h>
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
  // GdkRegion functions
  //  
  void og_gdk_region_new(VMContext& context) {
    APITools_SetIntValue(context, 0, (long)gdk_region_new());
  }
  
  //
  // GtkAccelGroup functions
  //
  void og_gtk_accel_group_new(VMContext& context) {
    APITools_SetIntValue(context, 0, (long)gtk_accel_group_new());
  }
  
  void og_gtk_accel_group_get_modifier_mask(VMContext& context) {
    GtkAccelGroup* param_1 = (GtkAccelGroup*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, gtk_accel_group_get_modifier_mask(param_1));
  }

  void og_gtk_accel_group_lock(VMContext& context) {
    GtkAccelGroup* param_0 = (GtkAccelGroup*)APITools_GetIntValue(context, 0);
    gtk_accel_group_lock(param_0);
  }
  
  void og_gtk_accel_group_unlock(VMContext& context) {
    GtkAccelGroup* param_0 = (GtkAccelGroup*)APITools_GetIntValue(context, 0);
    gtk_accel_group_unlock(param_0);
  }

  void og_gtk_accel_group_connect(VMContext& context) {
    GtkAccelGroup* param_0 = (GtkAccelGroup*)APITools_GetIntValue(context, 0);
    guint param_1 = APITools_GetIntValue(context, 1);
    GdkModifierType param_2 = (GdkModifierType)APITools_GetIntValue(context, 2);
    GtkAccelFlags param_3 = (GtkAccelFlags)APITools_GetIntValue(context, 3);
    long* param_4 = (long*)APITools_GetObjectValue(context, 4);
    
    GClosure closure;
    closure.in_marshal = param_4[0];
    closure.is_invalid = param_4[1];
    
    gtk_accel_group_connect(param_0, param_1, param_2, param_3, &closure);

    param_4[0] = closure.in_marshal;
    param_4[1] = closure.is_invalid;
  }

  void og_gtk_accel_group_connect_by_path(VMContext& context) {
    GtkAccelGroup* param_0 = (GtkAccelGroup*)APITools_GetIntValue(context, 0);
    gchar* param_1 = APITools_GetStringValue(context, 1);
    long* param_2 = (long*)APITools_GetIntValue(context, 2);

    GClosure closure;
    closure.in_marshal = param_2[0];
    closure.is_invalid = param_2[1];

    gtk_accel_group_connect_by_path(param_0, param_1, &closure);
    
    param_2[0] = closure.in_marshal;
    param_2[1] = closure.is_invalid;
  }

  void og_gtk_accel_group_query(VMContext& context) {
    GtkAccelGroup* param_1 = (GtkAccelGroup*)APITools_GetObjectValue(context, 1);
    guint param_2 = APITools_GetIntValue(context, 2);
    GdkModifierType param_3 = (GdkModifierType)APITools_GetIntValue(context, 3);
    guint param_4 = APITools_GetIntValue(context, 4);
        
    GtkAccelGroupEntry* entry = gtk_accel_group_query(param_1, param_2, param_3, &param_4);
    
    APITools_SetIntValue(context, 4, param_4);
    APITools_SetIntValue(context, 0, (long)entry);
  }
  
  //
  // GtkWidget functions
  //
  void og_gtk_widget_destroy(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_destroy(param_0);
  }

  void og_gtk_widget_destroyed(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    GtkWidget* param_1 = (GtkWidget*)APITools_GetIntValue(context, 1);
    gtk_widget_destroyed(param_0, &param_1);
  }

  void og_gtk_widget_unref(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_unref(param_0);
  }
  
  void og_gtk_widget_hide_all(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_hide_all(param_0);
  }

  void og_gtk_widget_unparent(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_unparent(param_0);
  }

  void og_gtk_widget_show(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_show(param_0);
  }
  
  void og_gtk_widget_show_now(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_show_now(param_0);
  }
  
  void og_gtk_widget_hide(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_hide(param_0);
  }

  void og_gtk_widget_show_all(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_show_all(param_0);
  }
  
  void og_gtk_widget_set_no_show_all(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_no_show_all(param_0, param_1);
  }
  
  void og_gtk_widget_map(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_map(param_0);
  }

  void og_gtk_widget_unmap(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_unmap(param_0);
  }
  
  void og_gtk_widget_realize(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_realize(param_0);
  }
  
  void og_gtk_widget_unrealize(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_unrealize(param_0);
  }
  
  void og_gtk_widget_queue_draw(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_queue_draw(param_0);
  }
  
  void og_gtk_widget_queue_draw_area(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gint param_1 = APITools_GetIntValue(context, 1);
    gint param_2 = APITools_GetIntValue(context, 2);
    gint param_3 = APITools_GetIntValue(context, 3);
    gint param_4 = APITools_GetIntValue(context, 4);
    gtk_widget_queue_draw_area(param_0, param_1, param_2, param_3, param_4);
  }
  
  void og_gtk_widget_queue_clear_area(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gint param_1 = APITools_GetIntValue(context, 1);
    gint param_2 = APITools_GetIntValue(context, 2);
    gint param_3 = APITools_GetIntValue(context, 3);
    gint param_4 = APITools_GetIntValue(context, 4);
    gtk_widget_queue_clear_area(param_0, param_1, param_2, param_3, param_4);
  }

  void og_gtk_widget_queue_resize(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_queue_resize(param_0);
  }
  
  void og_gtk_widget_queue_resize_no_redraw(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_queue_resize_no_redraw(param_0);
  }

  void og_gtk_widget_size_request(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    long* param_1 = (long*)APITools_GetObjectValue(context, 1);
    
    // set in/out
    GtkRequisition requisition;       
    gtk_widget_size_request(param_0, &requisition);    
    param_1[0] = requisition.width;
    param_1[1] = requisition.height;
  }

  void og_gtk_widget_size_allocate(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    GtkAllocation* param_1 = (GtkAllocation*)APITools_GetIntValue(context, 1);
    gtk_widget_size_allocate(param_0, param_1);
  }

  void og_gtk_widget_add_accelerator(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gchar* param_1 = APITools_GetStringValue(context, 1);
    GtkAccelGroup* param_2 = (GtkAccelGroup*)APITools_GetIntValue(context, 2);
    guint param_3 = APITools_GetIntValue(context, 3);
    GdkModifierType param_4 = (GdkModifierType)APITools_GetIntValue(context, 4);
    GtkAccelFlags param_5 = (GtkAccelFlags)APITools_GetIntValue(context, 5);
    gtk_widget_add_accelerator(param_0, param_1, param_2, param_3, param_4, param_5);
  }

  void og_gtk_widget_set_accel_path(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gchar* param_1 = APITools_GetStringValue(context, 1);
    GtkAccelGroup* param_2 = (GtkAccelGroup*)APITools_GetIntValue(context, 2);
    gtk_widget_set_accel_path(param_0, param_1, param_2);
  }

  void og_gtk_widget_reparent(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    GtkWidget* param_1 = (GtkWidget*)APITools_GetIntValue(context, 1);
    gtk_widget_reparent(param_0, param_1);
  }

  void og_gtk_widget_region_intersect(VMContext& context) {
    GtkWidget* param_1 = (GtkWidget*)APITools_GetIntValue(context, 1);
    const GdkRegion* param_2 = (const GdkRegion*)APITools_GetIntValue(context, 2);    
    APITools_SetIntValue(context, 0, (long)gtk_widget_region_intersect(param_1, param_2));
  }
  
  void og_gtk_widget_freeze_child_notify(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_freeze_child_notify(param_0);
  }

  void og_gtk_widget_child_notify(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gchar* param_1 = APITools_GetStringValue(context, 1);
    gtk_widget_child_notify(param_0, param_1);
  }
  
  void og_gtk_widget_thaw_child_notify(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_thaw_child_notify(param_0);
  }

  void og_gtk_widget_set_can_focus(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_can_focus(param_0, param_1);
  }

  void og_gtk_widget_grab_focus(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_grab_focus(param_0);
  }
  
  //
  // signals and events
  //

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
    gint width = gtk_container_get_border_width(GTK_CONTAINER (widget));
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
  
  //
  // events
  //
  void og_gdk_get_event_type(VMContext& context) {
    GdkEvent* event = (GdkEvent*)APITools_GetIntValue(context, 1);
#ifdef _DEBUG
    cout << "@@@ Event: id=" << event << ", type=" << event->type << " @@@" << endl;
#endif
    APITools_SetIntValue(context, 0, event->type);
  }
  
  void og_gdk_get_event_key(VMContext& context) {
    GdkEvent* event = (GdkEvent*)APITools_GetIntValue(context, 1);
#ifdef _DEBUG
    cout << "@@@ Event: id=" << event << ", type=" << event->type << " @@@" << endl;
#endif

    long* event_obj = context.alloc_obj("Gtk2.GdkEventKey", 
					(long*)context.op_stack, 
					*context.stack_pos, false);
    event_obj[0] = (long)event;
    
    APITools_SetObjectValue(context, 0, event_obj);
  }

  //
  // callbacks
  //
  gboolean event_callback_handler(GtkWidget* widget, GdkEvent* event, gpointer args) {
    callback_data* data = (callback_data*)args;
    
#ifdef _DEBUG
    cout << "@@@ Event: cls_id=" << data->cls_id << ", mthd_id=" << data->mthd_id 
	 << ", params=" << data->params << ", event_type=" << event->type << " @@@" << endl;
#endif
    
    long* event_obj = data->context.alloc_obj("Gtk2.GdkEvent", 
					      (long*)data->context.op_stack, 
					      *data->context.stack_pos, false);
    event_obj[0] = (long)event;
    
    APITools_PushInt(data->context, (long)data->params);
    APITools_PushInt(data->context, (long)event_obj);
    APITools_PushInt(data->context, (long)data->widget);
    APITools_CallMethod(data->context, NULL, data->cls_id, data->mthd_id);

    // TODO: free memory
    
    return TRUE;
  }

  void signal_callback_handler(GtkWidget* widget, gpointer args) {
    callback_data* data = (callback_data*)args;
    
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

