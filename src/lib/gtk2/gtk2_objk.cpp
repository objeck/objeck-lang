#include <gtk/gtk.h>
#include <cairo.h>
#include <iostream>
#include "../../lib_api.h"

using namespace std;

extern "C" {
  static void signal_connect_handler(VMContext& context, int id);
  static gboolean event_callback_handler(GtkWidget* widget, GdkEvent* event, gpointer args);
  static void signal_callback_handler(GtkWidget *widget, gpointer data);
  static gboolean signal_callback_id_handler(GtkWidget *widget, guint signal_id, gpointer data);
  static void signal_callback_param_handler(GtkWidget *widget, GParamSpec *pspec, gpointer user_data);
  static void signal_callback_drag_handler(GtkWidget *widget, GdkDragContext* drag, gpointer args);
  static void signal_callback_style_handler(GtkWidget *widget, GtkStyle* style, gpointer args);
  static void signal_callback_drag_get_handler(GtkWidget* widget, GdkDragContext* drag_context, 
					       GtkSelectionData* data, guint info, guint time, gpointer args);
  
  //
  // callback holder
  //
  typedef struct _callback_data {
    VMContext context;
    long* widget;
    int cls_id;
    int mthd_id;
    long* params;
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
    GtkWindowType type = (GtkWindowType)APITools_GetIntValue(context, 0);
    APITools_SetIntValue(context, 0, (long)gtk_window_new(type));
  }
  
  void og_gtk_window_set_wmclass(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gchar* param_1 = APITools_GetStringValue(context, 1);
    gchar* param_2 = APITools_GetStringValue(context, 2);
    gtk_window_set_wmclass(param_0, param_1, param_2);
  }

  void og_gtk_window_set_role(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gchar* param_1 = APITools_GetStringValue(context, 1);
    gtk_window_set_role(param_0, param_1);
  }
  
  void og_gtk_window_set_startup_id(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gchar* param_1 = APITools_GetStringValue(context, 1);
    gtk_window_set_startup_id(param_0, param_1);
  }

  void og_gtk_window_add_accel_group(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    GtkAccelGroup* param_1 = (GtkAccelGroup*)APITools_GetIntValue(context, 1);
    gtk_window_add_accel_group(param_0, param_1);
  }
  
  void og_gtk_window_remove_accel_group(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    GtkAccelGroup* param_1 = (GtkAccelGroup*)APITools_GetIntValue(context, 1);
    gtk_window_remove_accel_group(param_0, param_1);
  }

  void og_gtk_window_set_focus(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    GtkWidget* param_1 = (GtkWidget*)APITools_GetIntValue(context, 1);
    gtk_window_set_focus(param_0, param_1);
  }
  
  void og_gtk_window_get_focus(VMContext& context) {
    GtkWindow* param_1 = (GtkWindow*)APITools_GetIntValue(context, 1);    
    APITools_SetIntValue(context, 0, (long)gtk_window_get_focus(param_1));
  }
  
  void og_gtk_window_set_default(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    GtkWidget* param_1 = (GtkWidget*)APITools_GetIntValue(context, 1);
    gtk_window_set_default(param_0, param_1);
  }

  void og_gtk_window_get_default_widget(VMContext& context) {
    GtkWindow* param_1 = (GtkWindow*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (long)gtk_window_get_default_widget(param_1));
  }

  void og_gtk_window_set_transient_for(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    GtkWindow* param_1 = (GtkWindow*)APITools_GetIntValue(context, 1);
    gtk_window_set_transient_for(param_0, param_1);
  }

  void og_gtk_window_get_transient_for(VMContext& context) {
    GtkWindow* param_1 = (GtkWindow*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (long)gtk_window_get_transient_for(param_1));
  }
  
  void og_gtk_window_set_opacity(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gdouble param_1 = APITools_GetFloatValue(context, 1);
    gtk_window_set_opacity(param_0, param_1);
  }
  
  void og_gtk_window_set_type_hint(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    GdkWindowTypeHint param_1 = (GdkWindowTypeHint)APITools_GetIntValue(context, 1);
    gtk_window_set_type_hint(param_0, param_1);
  }

  void og_gtk_window_get_type_hint(VMContext& context) {
    GtkWindow* param_1 = (GtkWindow*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (long)gtk_window_get_type_hint(param_1));
  }

  void og_gtk_window_set_skip_taskbar_hint(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_window_set_skip_taskbar_hint(param_0, param_1);
  }
  
  void og_gtk_window_set_skip_pager_hint(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_window_set_skip_pager_hint(param_0, param_1);
  }
  
  void og_gtk_window_set_urgency_hint(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_window_set_urgency_hint(param_0, param_1);
  }
  
  void og_gtk_window_set_focus_on_map(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_window_set_focus_on_map(param_0, param_1);
  }
  
  void og_gtk_window_set_destroy_with_parent(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_window_set_destroy_with_parent(param_0, param_1);
  }
  
  void og_gtk_window_set_mnemonics_visible(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_window_set_mnemonics_visible(param_0, param_1);
  }
  
  void og_gtk_window_set_resizable(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_window_set_resizable(param_0, param_1);
  }
  
  void og_gtk_window_set_gravity(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    GdkGravity param_1 = (GdkGravity)APITools_GetIntValue(context, 1);
    gtk_window_set_gravity(param_0, param_1);
  }
  
  void og_gtk_window_get_gravity(VMContext& context) {
    GtkWindow* param_1 = (GtkWindow*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (long)gtk_window_get_gravity(param_1));
  }

  void og_gtk_window_set_geometry_hints(VMContext& context) {
    GtkWindow* param_0 = (GtkWindow*)APITools_GetIntValue(context, 0);
    GtkWidget* param_1 = (GtkWidget*)APITools_GetIntValue(context, 1);
    long* param_2 = (long*)APITools_GetObjectValue(context, 2);
    GdkWindowHints param_3 = (GdkWindowHints)APITools_GetIntValue(context, 3);

    int i = 0;
    GdkGeometry geo;
    geo.min_width = param_2[i++];
    geo.min_height = param_2[i++];
    geo.max_width = param_2[i++];
    geo.max_height = param_2[i++];
    geo.base_width = param_2[i++];
    geo.base_height = param_2[i++];
    geo.width_inc = param_2[i++];
    geo.height_inc = param_2[i++];
    // set float
    memcpy(&geo.min_aspect, &param_2[i], sizeof(geo.min_aspect));
#ifdef _X64
    i++;
#else
    i += 2;
#endif
    // set float
    memcpy(&geo.max_aspect, &param_2[i], sizeof(geo.min_aspect));
#ifdef _X64
    i++;
#else
    i += 2;
#endif
    // set last element
    geo.win_gravity = (GdkGravity)param_2[i++];
    
    gtk_window_set_geometry_hints(param_0, param_1, &geo, param_3);
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

  void og_gtk_widget_set_can_default(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_can_default(param_0, param_1);
  }
  
  void og_gtk_widget_grab_default(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gtk_widget_grab_default(param_0);
  }
  
  void og_gtk_widget_set_receives_default(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_receives_default(param_0, param_1);
  }
  
  void og_gtk_widget_set_name(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gchar* param_1 = APITools_GetStringValue(context, 1);
    gtk_widget_set_name(param_0, param_1);
  }
  
  void og_gtk_widget_set_state(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    GtkStateType param_1 = (GtkStateType)APITools_GetIntValue(context, 1);
    gtk_widget_set_state(param_0, param_1);
  }
  
  void og_gtk_widget_get_state(VMContext& context) {
    GtkWidget* param_1 = (GtkWidget*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, gtk_widget_get_state(param_1));
  }
  
  void og_gtk_widget_set_sensitive(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_sensitive(param_0, param_1);
  }
  
  void og_gtk_widget_set_visible(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_visible(param_0, param_1);
  }
  
  void og_gtk_widget_set_realized(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_realized(param_0, param_1);
  }
  
  //
  // container class functions
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

  void og_gtk_widget_set_mapped(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_mapped(param_0, param_1);
  }

  void og_gtk_widget_set_app_paintable(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_app_paintable(param_0, param_1);
  }
  
  void og_gtk_widget_set_double_buffered(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_double_buffered(param_0, param_1);
  }
  
  void og_gtk_widget_set_redraw_on_allocate(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    gboolean param_1 = APITools_GetIntValue(context, 1);
    gtk_widget_set_redraw_on_allocate(param_0, param_1);
  }

  void og_gtk_widget_set_parent(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    GtkWidget* param_1 = (GtkWidget*)APITools_GetIntValue(context, 1);
    gtk_widget_set_parent(param_0, param_1);
  }
  
  void og_gtk_widget_get_parent(VMContext& context) {
    GtkWidget* param_1 = (GtkWidget*)APITools_GetIntValue(context, 1);
    APITools_SetIntValue(context, 0, (long)gtk_widget_get_parent(param_1));
  }

  void og_gtk_widget_set_parent_window(VMContext& context) {
    GtkWidget* param_0 = (GtkWidget*)APITools_GetIntValue(context, 0);
    GdkWindow* param_1 = (GdkWindow*)APITools_GetIntValue(context, 1);
    gtk_widget_set_parent_window(param_0, param_1);
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
  // signals and events
  //
  
  void og_signal_connect(VMContext& context) {
    signal_connect_handler(context, 0);
  }

  void og_signal_id_connect(VMContext& context) {
    signal_connect_handler(context, 1);
  }
  
  void og_event_connect(VMContext& context) {
    signal_connect_handler(context, 2);
  }

  void og_event_param_connect(VMContext& context) {
    signal_connect_handler(context, 3);
  }

  void og_event_drag_connect(VMContext& context) {
    signal_connect_handler(context, 4);
  }
  
  void og_event_style_connect(VMContext& context) {
    signal_connect_handler(context, 5);
  }
  
  void og_event_drag_get_connect(VMContext& context) {
    signal_connect_handler(context, 6);
  }
  
  void signal_connect_handler(VMContext& context, int type) {
    long* widget = (long*)APITools_GetObjectValue(context, 1); // widget
    const char* name = APITools_GetStringValue(context, 2); // name
    int cls_id = APITools_GetFunctionValue(context, 3, CLS_ID); // function
    int mthd_id = APITools_GetFunctionValue(context, 3, MTHD_ID); // function
    long* params = (long*)APITools_GetObjectValue(context, 4); // function params
    
    callback_data* data = new callback_data;
    data->context = context;
    data->widget = widget;
    data->cls_id = cls_id;
    data->mthd_id = mthd_id;
    data->params = params;   
    
#ifdef _DEBUG
    cout << "@@@ Handler: data=" << data << "; widget=" << widget << "; name=" << name << "; mthd=(" 
	 << cls_id << "," << mthd_id << "); params=" << params << " @@@" << endl;
#endif
    
    // widget id
    gulong id;
    switch(type) {
    case 0:
      id = g_signal_connect((GtkWidget*)widget[0], name, G_CALLBACK(signal_callback_handler), data);
      break;
      
    case 1:
      id = g_signal_connect((GtkWidget*)widget[0], name, G_CALLBACK(signal_callback_id_handler), data);
      break;
      
    case 2:
      id = g_signal_connect((GtkWidget*)widget[0], name, G_CALLBACK(event_callback_handler), data);
      break;
      
    case 3:
      id = g_signal_connect((GtkWidget*)widget[0], name, G_CALLBACK(signal_callback_param_handler), data);
      break;
      
    case 4:
      id = g_signal_connect((GtkWidget*)widget[0], name, G_CALLBACK(signal_callback_drag_handler), data);
      break;

    case 5:
      id = g_signal_connect((GtkWidget*)widget[0], name, G_CALLBACK(signal_callback_style_handler), data);
      break;
      
    case 6:
      id = g_signal_connect((GtkWidget*)widget[0], name, G_CALLBACK(signal_callback_drag_get_handler), data);
      break;
    }
    
    // set return
    APITools_SetIntValue(context, 0, id);
  }
  
  void og_signal_handler_disconnect(VMContext& context) {
    GtkWidget* widget = (GtkWidget*)APITools_GetIntValue(context, 0); // raw widget
    glong id = APITools_GetIntValue(context, 1);
    g_signal_handler_disconnect(widget, id);
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
  gboolean event_callback_handler(GtkWidget* widget, GdkEvent* event, gpointer args) 
  {
    callback_data* data = (callback_data*)args;
    
#ifdef _DEBUG
    cout << "@@@ Event: cls_id=" << data->cls_id << ", mthd_id=" << data->mthd_id 
	 << ", params=" << data->params << ", event_type=" << event->type << " @@@" << endl;
#endif
    
    long* event_obj = NULL;
    switch(event->type) {
    case GDK_GRAB_BROKEN:
      break;

    case GDK_DAMAGE:
      break;
      
    case GDK_NOTHING:
      break;

    case GDK_DELETE:
    case GDK_DESTROY:
      event_obj = data->context.alloc_obj("Gtk2.GdkEvent", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;

    case GDK_EXPOSE:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventExpose", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;

    case GDK_MOTION_NOTIFY:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventMotion", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;

    case GDK_BUTTON_PRESS:
    case GDK_2BUTTON_PRESS:
    case GDK_3BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventKey", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;
      
    case GDK_ENTER_NOTIFY:
    case GDK_LEAVE_NOTIFY:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventCrossing", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;
      
    case GDK_FOCUS_CHANGE:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventFocus", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;
      
    case GDK_CONFIGURE:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventConfigure", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;
      
    case GDK_MAP:
    case GDK_UNMAP:
      event_obj = data->context.alloc_obj("Gtk2.GdkEvent", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;
      
    case GDK_PROPERTY_NOTIFY:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventProperty", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;

    case GDK_SELECTION_CLEAR:
    case GDK_SELECTION_REQUEST:
    case GDK_SELECTION_NOTIFY:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventSelection", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;
      
    case GDK_PROXIMITY_IN:
    case GDK_PROXIMITY_OUT:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventProximity", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;
      
    case GDK_DRAG_ENTER:
      break;

    case GDK_DRAG_LEAVE:
      break;

    case GDK_DRAG_MOTION:
      break;

    case GDK_DRAG_STATUS:
      break;

    case GDK_DROP_START:
      break;

    case GDK_DROP_FINISHED:
      break;

    case GDK_CLIENT_EVENT:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventClient", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;

    case GDK_EVENT_LAST:
      break;
      
    case GDK_VISIBILITY_NOTIFY:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventVisibility", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;

    case GDK_NO_EXPOSE:
      break;

    case GDK_SCROLL:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventScroll", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;
      
    case GDK_WINDOW_STATE:
      event_obj = data->context.alloc_obj("Gtk2.GdkEventWindowState", 
					  (long*)data->context.op_stack, 
					  *data->context.stack_pos, false);
      break;

    case GDK_SETTING:
      break;
      
    case GDK_OWNER_CHANGE:
      break;
    }    
	
    if(event_obj) {
      event_obj[0] = (long)event;
		
      APITools_PushInt(data->context, (long)data->params);
      APITools_PushInt(data->context, (long)event_obj);
      APITools_PushInt(data->context, (long)data->widget);
      APITools_CallMethod(data->context, NULL, data->cls_id, data->mthd_id);
		
      return TRUE;
    }
	
    return FALSE;
  }

  void signal_callback_handler(GtkWidget* widget, gpointer args) 
  {
    callback_data* data = (callback_data*)args;
    
#ifdef _DEBUG
    cout << "@@@ Signal: data=" << data << "; cls_id=" << data->cls_id << "; mthd_id=" 
	 << data->mthd_id << "; widget=" << data->widget << "; params=" 
	 << data->params << " @@@" << endl;
#endif
    
    APITools_PushInt(data->context, (long)data->params);
    APITools_PushInt(data->context, (long)data->widget);
    APITools_CallMethod(data->context, NULL, data->cls_id, data->mthd_id);
  }
  
  gboolean signal_callback_id_handler(GtkWidget *widget, guint signal_id, gpointer args) 
  {
    callback_data* data = (callback_data*)args;
    
#ifdef _DEBUG
    cout << "@@@ Signal ID: data=" << data << "; cls_id=" << data->cls_id << "; mthd_id=" 
	 << data->mthd_id << "; widget=" << data->widget << "; params=" 
	 << data->params << " @@@" << endl;
#endif
    
    APITools_PushInt(data->context, (long)data->params);
    APITools_PushInt(data->context, (long)signal_id);
    APITools_PushInt(data->context, (long)data->widget);
    APITools_CallMethod(data->context, NULL, data->cls_id, data->mthd_id);
    
    return TRUE;
  }
  
  void signal_callback_param_handler(GtkWidget* widget, GParamSpec* pspec, gpointer args)
  {
    callback_data* data = (callback_data*)args;
    
#ifdef _DEBUG
    cout << "@@@ Signal params: data=" << data << "; cls_id=" << data->cls_id << "; mthd_id=" 
	 << data->mthd_id << "; widget=" << data->widget << "; params="  << data->params 
	 << " @@@" << endl;
#endif
    
    long* event_obj = data->context.alloc_obj("Gtk2.GParamSpec", 
					      (long*)data->context.op_stack, 
					      *data->context.stack_pos, false);
    event_obj[0] = (long)pspec;
    
    APITools_PushInt(data->context, (long)data->params);
    APITools_PushInt(data->context, (long)event_obj);
    APITools_PushInt(data->context, (long)data->widget);
    APITools_CallMethod(data->context, NULL, data->cls_id, data->mthd_id);
  }

  void signal_callback_drag_handler(GtkWidget *widget, GdkDragContext* drag, gpointer args) 
  {
    callback_data* data = (callback_data*)args;
    
#ifdef _DEBUG
    cout << "@@@ Signal drag: data=" << data << "; cls_id=" << data->cls_id << "; mthd_id=" 
	 << data->mthd_id << "; widget=" << data->widget << "; params="  << data->params 
	 << " @@@" << endl;
#endif
    
    long* event_obj = data->context.alloc_obj("Gtk2.GdkDragContext", 
					      (long*)data->context.op_stack, 
					      *data->context.stack_pos, false);
    event_obj[0] = (long)drag;
    
    APITools_PushInt(data->context, (long)data->params);
    APITools_PushInt(data->context, (long)event_obj);
    APITools_PushInt(data->context, (long)data->widget);
    APITools_CallMethod(data->context, NULL, data->cls_id, data->mthd_id);
  }

  void signal_callback_drag_get_handler(GtkWidget* widget, GdkDragContext* drag, GtkSelectionData* sel, 
					guint info, guint time, gpointer args)
  {
    callback_data* data = (callback_data*)args;
    
#ifdef _DEBUG
    cout << "@@@ Signal drag get: data=" << data << "; cls_id=" << data->cls_id << "; mthd_id=" 
	 << data->mthd_id << "; widget=" << data->widget << "; params="  << data->params 
	 << " @@@" << endl;
#endif
    
    long* event_obj_1 = data->context.alloc_obj("Gtk2.GdkDragContext", 
					      (long*)data->context.op_stack, 
					      *data->context.stack_pos, false);
    event_obj_1[0] = (long)drag;

    long* event_obj_2 = data->context.alloc_obj("Gtk2.GtkSelectionData", 
					      (long*)data->context.op_stack, 
					      *data->context.stack_pos, false);
    event_obj_2[0] = (long)sel;
    
    APITools_PushInt(data->context, (long)data->params);
    APITools_PushInt(data->context, time);
    APITools_PushInt(data->context, info);
    APITools_PushInt(data->context, (long)event_obj_2);
    APITools_PushInt(data->context, (long)event_obj_1);
    APITools_PushInt(data->context, (long)data->widget);
    APITools_CallMethod(data->context, NULL, data->cls_id, data->mthd_id);
  }
  
  void signal_callback_style_handler(GtkWidget *widget, GtkStyle* style, gpointer args) 
  {
    callback_data* data = (callback_data*)args;
    
#ifdef _DEBUG
    cout << "@@@ Signal style: data=" << data << "; cls_id=" << data->cls_id << "; mthd_id=" 
	 << data->mthd_id << "; widget=" << data->widget << "; params="  << data->params 
	 << " @@@" << endl;
#endif
    
    long* event_obj = data->context.alloc_obj("Gtk2.GdkStyleContext", 
					      (long*)data->context.op_stack, 
					      *data->context.stack_pos, false);
    event_obj[0] = (long)style;
    
    APITools_PushInt(data->context, (long)data->params);
    APITools_PushInt(data->context, (long)event_obj);
    APITools_PushInt(data->context, (long)data->widget);
    APITools_CallMethod(data->context, NULL, data->cls_id, data->mthd_id);
  }
}

