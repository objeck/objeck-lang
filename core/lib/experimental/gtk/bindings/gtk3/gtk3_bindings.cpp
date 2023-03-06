#include "gtk3_bindings.h"

extern "C" {
	static ResourceManager* res_manager = nullptr;
	
	//
	// Initialize library
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
	// Release library
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


	//
	// Cursor
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_cursor_new_for_display(VMContext& context) {
		const size_t* p1_obj = APITools_GetObjectValue(context, 1);
		GdkDisplay* p1 = (GdkDisplay*)p1_obj[0];

		const GdkCursorType p2 = (GdkCursorType)APITools_GetIntValue(context, 2);

		const auto status = (size_t)gdk_cursor_new_for_display(p1, p2);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_cursor_new_from_name(VMContext& context) {
		const size_t* p1_obj = APITools_GetObjectValue(context, 1);
		GdkDisplay* p1 = (GdkDisplay*)p1_obj[0];

		const gchar* p2 = (gchar*)UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

		const auto status = (size_t)gdk_cursor_new_from_name(p1, p2);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_cursor_get_cursor_type(VMContext& context) {
		GdkCursor* p1 = (GdkCursor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_cursor_get_cursor_type(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_cursor_get_display(VMContext& context) {
		GdkCursor* p1 = (GdkCursor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_cursor_get_display(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}


	//
	// Device
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_associated_device(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_associated_device(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_axes(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_axes(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_axis_use(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);

		const auto status = gdk_device_get_axis_use(p1, p2);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_device_type(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_device_type(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_display(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_display(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_has_cursor(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_has_cursor(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_key(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		guint keyval;
		GdkModifierType modifiers;
		const auto status = gdk_device_get_key(p1, p2, &keyval, &modifiers);
		APITools_SetIntValue(context, 3, keyval);
		APITools_SetIntValue(context, 4, modifiers);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_last_event_window(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_last_event_window(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_mode(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_mode(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_n_axes(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_n_axes(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_n_keys(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_n_keys(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_name(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_name(p1);

		APITools_SetStringValue(context, 0, BytesToUnicode(status));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_position(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);
		size_t* p2_obj = APITools_GetObjectValue(context, 2);
		
		GdkScreen* screen; gint x, y;
		gdk_device_get_position(p1, &screen, &x, &y);

		p2_obj[0] = (size_t)screen;
		APITools_SetIntValue(context, 3, x);
		APITools_SetIntValue(context, 4, y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_position_double(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkScreen** p2 = (GdkScreen**)p2_obj[0];

		gdouble x, y;
		gdk_device_get_position_double(p1, p2, &x, &y);
		APITools_SetFloatValue(context, 3, x);
		APITools_SetFloatValue(context, 4, y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_product_id(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_product_id(p1);

		APITools_SetStringValue(context, 0, BytesToUnicode(status));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_seat(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_seat(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_source(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_source(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_vendor_id(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_get_vendor_id(p1);

		APITools_SetStringValue(context, 0, BytesToUnicode(status));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_window_at_position(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);
				
		gint win_x, win_y;
		const auto status = gdk_device_get_window_at_position(p1, &win_x, &win_y);

		APITools_SetIntValue(context, 3, win_x);
		APITools_SetIntValue(context, 4, win_y);
		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_get_window_at_position_double(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);
		
		gdouble win_x, win_y;
		const auto status = gdk_device_get_window_at_position_double(p1, &win_x, &win_y);

		APITools_SetFloatValue(context, 3, win_x);
		APITools_SetFloatValue(context, 4, win_y);
		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_set_axis_use(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const GdkAxisUse p3 = (GdkAxisUse)APITools_GetIntValue(context, 3);

		gdk_device_set_axis_use(p1, p2, p3);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_set_key(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);
		const GdkModifierType p4 = (GdkModifierType)APITools_GetIntValue(context, 4);

		gdk_device_set_key(p1, p2, p3, p4);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_set_mode(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);
		const GdkInputMode p2 = (GdkInputMode)APITools_GetIntValue(context, 2);

		const auto status = gdk_device_set_mode(p1, p2);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_warp(VMContext& context) {
		GdkDevice* p1 = (GdkDevice*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkScreen* p2 = (GdkScreen*)p2_obj[0];

		const gint p3 = (gint)APITools_GetIntValue(context, 3);
		const gint p4 = (gint)APITools_GetIntValue(context, 4);

		gdk_device_warp(p1, p2, p3, p4);
	}


	//
	// DeviceManager
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_device_manager_get_display(VMContext& context) {
		GdkDeviceManager* p1 = (GdkDeviceManager*)APITools_GetIntValue(context, 1);

		const auto status = gdk_device_manager_get_display(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}


	//
	// Display
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_beep(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		gdk_display_beep(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_close(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		gdk_display_close(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_device_is_grabbed(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		const auto status = gdk_display_device_is_grabbed(p1, p2);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_flush(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		gdk_display_flush(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_app_launch_context(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_get_app_launch_context(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_default_cursor_size(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_get_default_cursor_size(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_default_group(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_get_default_group(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_default_screen(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_get_default_screen(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_default_seat(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_get_default_seat(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_event(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_get_event(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_maximal_cursor_size(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);

		guint width, height;
		gdk_display_get_maximal_cursor_size(p1, &width, &height);
		APITools_SetIntValue(context, 2, width);
		APITools_SetIntValue(context, 3, height);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_monitor(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);

		const auto status = gdk_display_get_monitor(p1, p2);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_monitor_at_point(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);

		const auto status = gdk_display_get_monitor_at_point(p1, p2, p3);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_monitor_at_window(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkWindow* p2 = (GdkWindow*)p2_obj[0];

		const auto status = gdk_display_get_monitor_at_window(p1, p2);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_n_monitors(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_get_n_monitors(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_name(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_get_name(p1);

		APITools_SetStringValue(context, 0, BytesToUnicode(status));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_primary_monitor(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_get_primary_monitor(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_has_pending(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_has_pending(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_is_closed(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_is_closed(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_notify_startup_complete(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const gchar* p2 = (gchar*)UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

		gdk_display_notify_startup_complete(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_peek_event(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_peek_event(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_put_event(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		const GdkEvent* p2 = (const GdkEvent*)p2_obj[0];

		gdk_display_put_event(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_request_selection_notification(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkAtom p2 = (GdkAtom)p2_obj[0];

		const auto status = gdk_display_request_selection_notification(p1, p2);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_set_double_click_distance(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);

		gdk_display_set_double_click_distance(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_set_double_click_time(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);

		gdk_display_set_double_click_time(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_store_clipboard(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkWindow* p2 = (GdkWindow*)p2_obj[0];

		const gint p3 = (gint)APITools_GetIntValue(context, 3);
		size_t* targets_obj = APITools_GetObjectValue(context, 4);
		const size_t* targets_array = APITools_GetArray(targets_obj);
		const size_t targets_array_size = APITools_GetArraySize(targets_obj);

		GdkAtom** atoms = new GdkAtom*[targets_array_size];
		for(size_t i = 0; i < targets_array_size; ++i) {
			size_t* target_obj = (size_t*)targets_array[i];
			atoms[i] = (GdkAtom*)target_obj[0];
		}
		const gint p5 = (gint)APITools_GetIntValue(context, 5);

		gdk_display_store_clipboard(p1, p2, p3, *atoms, p5);
		delete[] atoms;
		atoms = nullptr;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_supports_clipboard_persistence(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_supports_clipboard_persistence(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_supports_cursor_alpha(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_supports_cursor_alpha(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_supports_cursor_color(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_supports_cursor_color(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_supports_input_shapes(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_supports_input_shapes(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_supports_selection_notification(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_supports_selection_notification(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_supports_shapes(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		const auto status = gdk_display_supports_shapes(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_sync(VMContext& context) {
		GdkDisplay* p1 = (GdkDisplay*)APITools_GetIntValue(context, 1);

		gdk_display_sync(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_get_default(VMContext& context) {
		auto status = gdk_display_get_default();

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_display_open(VMContext& context) {
		const gchar* p1 = UnicodeToBytes(APITools_GetStringValue(context, 1)).c_str();

		const auto status = gdk_display_open(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}


	//
	// DragContext
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_get_actions(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);

		const auto status = gdk_drag_context_get_actions(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_get_dest_window(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);

		const auto status = gdk_drag_context_get_dest_window(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_get_device(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);

		const auto status = gdk_drag_context_get_device(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_get_drag_window(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);

		const auto status = gdk_drag_context_get_drag_window(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_get_protocol(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);

		const auto status = gdk_drag_context_get_protocol(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_get_selected_action(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);

		const auto status = gdk_drag_context_get_selected_action(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_get_source_window(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);

		const auto status = gdk_drag_context_get_source_window(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_get_suggested_action(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);

		const auto status = gdk_drag_context_get_suggested_action(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_manage_dnd(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkWindow* p2 = (GdkWindow*)p2_obj[0];

		const GdkDragAction p3 = (GdkDragAction)APITools_GetIntValue(context, 3);

		const auto status = gdk_drag_context_manage_dnd(p1, p2, p3);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_set_device(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		gdk_drag_context_set_device(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_drag_context_set_hotspot(VMContext& context) {
		GdkDragContext* p1 = (GdkDragContext*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);

		gdk_drag_context_set_hotspot(p1, p2, p3);
	}


	//
	// Monitor
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_get_display(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_monitor_get_display(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_get_geometry(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkRectangle* p2 = (GdkRectangle*)p2_obj[0];

		gdk_monitor_get_geometry(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_get_height_mm(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_monitor_get_height_mm(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_get_manufacturer(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_monitor_get_manufacturer(p1);

		APITools_SetStringValue(context, 0, BytesToUnicode(status));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_get_model(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_monitor_get_model(p1);

		APITools_SetStringValue(context, 0, BytesToUnicode(status));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_get_refresh_rate(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_monitor_get_refresh_rate(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_get_scale_factor(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_monitor_get_scale_factor(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_get_subpixel_layout(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_monitor_get_subpixel_layout(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_get_width_mm(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_monitor_get_width_mm(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_get_workarea(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkRectangle* p2 = (GdkRectangle*)p2_obj[0];

		gdk_monitor_get_workarea(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_monitor_is_primary(VMContext& context) {
		GdkMonitor* p1 = (GdkMonitor*)APITools_GetIntValue(context, 1);

		const auto status = gdk_monitor_is_primary(p1);

		APITools_SetIntValue(context, 0, status);
	}


	//
	// Screen
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_screen_get_display(VMContext& context) {
		GdkScreen* p1 = (GdkScreen*)APITools_GetIntValue(context, 1);

		const auto status = gdk_screen_get_display(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_screen_get_resolution(VMContext& context) {
		GdkScreen* p1 = (GdkScreen*)APITools_GetIntValue(context, 1);

		const auto status = gdk_screen_get_resolution(p1);

		APITools_SetFloatValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_screen_get_rgba_visual(VMContext& context) {
		GdkScreen* p1 = (GdkScreen*)APITools_GetIntValue(context, 1);

		const auto status = gdk_screen_get_rgba_visual(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_screen_get_root_window(VMContext& context) {
		GdkScreen* p1 = (GdkScreen*)APITools_GetIntValue(context, 1);

		const auto status = gdk_screen_get_root_window(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_screen_get_system_visual(VMContext& context) {
		GdkScreen* p1 = (GdkScreen*)APITools_GetIntValue(context, 1);

		const auto status = gdk_screen_get_system_visual(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_screen_is_composited(VMContext& context) {
		GdkScreen* p1 = (GdkScreen*)APITools_GetIntValue(context, 1);

		const auto status = gdk_screen_is_composited(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_screen_set_resolution(VMContext& context) {
		GdkScreen* p1 = (GdkScreen*)APITools_GetIntValue(context, 1);
		const gdouble p2 = (gdouble)APITools_GetFloatValue(context, 2);

		gdk_screen_set_resolution(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_screen_get_default(VMContext& context) {
		auto status = gdk_screen_get_default();

		APITools_SetIntValue(context, 0, (size_t)status);
	}


	//
	// Window
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_new(VMContext& context) {
		const size_t* p1_obj = APITools_GetObjectValue(context, 1);
		GdkWindow* p1 = (GdkWindow*)p1_obj[0];

		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkWindowAttr* p2 = (GdkWindowAttr*)p2_obj[0];

		const gint p3 = (gint)APITools_GetIntValue(context, 3);

		const auto status = (size_t)gdk_window_new(p1, p2, p3);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_beep(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_beep(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_begin_move_drag(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);
		const gint p4 = (gint)APITools_GetIntValue(context, 4);
		const gint p5 = (gint)APITools_GetIntValue(context, 5);

		gdk_window_begin_move_drag(p1, p2, p3, p4, p5);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_begin_move_drag_for_device(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		const gint p3 = (gint)APITools_GetIntValue(context, 3);
		const gint p4 = (gint)APITools_GetIntValue(context, 4);
		const gint p5 = (gint)APITools_GetIntValue(context, 5);
		const gint p6 = (gint)APITools_GetIntValue(context, 6);

		gdk_window_begin_move_drag_for_device(p1, p2, p3, p4, p5, p6);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_begin_resize_drag(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const GdkWindowEdge p2 = (GdkWindowEdge)APITools_GetIntValue(context, 2);

		const gint p3 = (gint)APITools_GetIntValue(context, 3);
		const gint p4 = (gint)APITools_GetIntValue(context, 4);
		const gint p5 = (gint)APITools_GetIntValue(context, 5);
		const gint p6 = (gint)APITools_GetIntValue(context, 6);

		gdk_window_begin_resize_drag(p1, p2, p3, p4, p5, p6);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_begin_resize_drag_for_device(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const GdkWindowEdge p2 = (GdkWindowEdge)APITools_GetIntValue(context, 2);

		const size_t* p3_obj = APITools_GetObjectValue(context, 3);
		GdkDevice* p3 = (GdkDevice*)p3_obj[0];

		const gint p4 = (gint)APITools_GetIntValue(context, 4);
		const gint p5 = (gint)APITools_GetIntValue(context, 5);
		const gint p6 = (gint)APITools_GetIntValue(context, 6);
		const gint p7 = (gint)APITools_GetIntValue(context, 7);

		gdk_window_begin_resize_drag_for_device(p1, p2, p3, p4, p5, p6, p7);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_coords_from_parent(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gdouble p2 = APITools_GetFloatValue(context, 2);
		const gdouble p3 = APITools_GetFloatValue(context, 3);
		const gdouble p4 = APITools_GetFloatValue(context, 4);
		const gdouble p5 = APITools_GetFloatValue(context, 5);

		gdouble x, y;
		gdk_window_coords_from_parent(p1, p2, p3, &x, &y);
		APITools_SetFloatValue(context, 4, x);
		APITools_SetFloatValue(context, 5, y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_coords_to_parent(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gdouble p2 = APITools_GetFloatValue(context, 2);
		const gdouble p3 = APITools_GetFloatValue(context, 3);
		const gdouble p4 = APITools_GetFloatValue(context, 4);
		const gdouble p5 = APITools_GetFloatValue(context, 5);

		gdouble parent_x, parent_y;
		gdk_window_coords_to_parent(p1, p2, p3, &parent_x, &parent_y);
		APITools_SetFloatValue(context, 4, parent_x);
		APITools_SetFloatValue(context, 5, parent_y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_create_gl_context(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		GError* error;
		const auto status = gdk_window_create_gl_context(p1, &error);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_deiconify(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_deiconify(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_destroy(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_destroy(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_end_draw_frame(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDrawingContext* p2 = (GdkDrawingContext*)p2_obj[0];

		gdk_window_end_draw_frame(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_ensure_native(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_ensure_native(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_focus(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);

		gdk_window_focus(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_freeze_updates(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_freeze_updates(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_fullscreen(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_fullscreen(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_fullscreen_on_monitor(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);

		gdk_window_fullscreen_on_monitor(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_geometry_changed(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_geometry_changed(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_accept_focus(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_accept_focus(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_cursor(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_cursor(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_decorations(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		
		GdkWMDecoration decorations;
		const auto status = gdk_window_get_decorations(p1, &decorations);

		APITools_SetIntValue(context, 2, decorations);
		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_device_cursor(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		const auto status = gdk_window_get_device_cursor(p1, p2);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_device_events(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		const auto status = gdk_window_get_device_events(p1, p2);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_device_position(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		gint x, y; GdkModifierType mask;
		const auto status = gdk_window_get_device_position(p1, p2, &x, &y, &mask);
		APITools_SetIntValue(context, 3, (size_t)x);
		APITools_SetIntValue(context, 4, (size_t)y);
		APITools_SetIntValue(context, 5, (size_t)mask);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_device_position_double(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		gdouble x, y; GdkModifierType mask;

		const auto status = gdk_window_get_device_position_double(p1, p2, &x, &y, &mask);

		APITools_SetFloatValue(context, 3, x);
		APITools_SetFloatValue(context, 4, y);
		APITools_SetFloatValue(context, 5, mask);
		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_display(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_display(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_drag_protocol(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkWindow** p2 = (GdkWindow**)p2_obj[0];

		const auto status = gdk_window_get_drag_protocol(p1, p2);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_effective_parent(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_effective_parent(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_effective_toplevel(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_effective_toplevel(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_event_compression(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_event_compression(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_events(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_events(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_focus_on_map(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_focus_on_map(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_frame_clock(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_frame_clock(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_frame_extents(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkRectangle* p2 = (GdkRectangle*)p2_obj[0];

		gdk_window_get_frame_extents(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_fullscreen_mode(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_fullscreen_mode(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_geometry(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		gint x, y, width, height;
		gdk_window_get_geometry(p1, &x, &y, &width, &height);
		APITools_SetIntValue(context, 2, x);
		APITools_SetIntValue(context, 3, y);
		APITools_SetIntValue(context, 4, width);
		APITools_SetIntValue(context, 5, height);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_group(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_group(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_height(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_height(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_modal_hint(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_modal_hint(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_origin(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		
		gint x, y;
		const auto status = gdk_window_get_origin(p1, &x, &y);

		APITools_SetIntValue(context, 2, x);
		APITools_SetIntValue(context, 3, y);
		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_parent(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_parent(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_pass_through(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_pass_through(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_position(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		gint x, y;
		gdk_window_get_position(p1, &x, &y);
		APITools_SetIntValue(context, 2, x);
		APITools_SetIntValue(context, 3, y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_root_coords(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);

		gint root_x, root_y;
		gdk_window_get_root_coords(p1, p2, p3, &root_x, &root_y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_root_origin(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gint x, y;
		gdk_window_get_root_origin(p1, &x, &y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_scale_factor(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_scale_factor(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_screen(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_screen(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_source_events(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const GdkInputSource p2 = (GdkInputSource)APITools_GetIntValue(context, 2);

		const auto status = gdk_window_get_source_events(p1, p2);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_state(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_state(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_support_multidevice(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_support_multidevice(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_toplevel(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_toplevel(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_type_hint(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_type_hint(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_visual(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_visual(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_width(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_width(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_get_window_type(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_get_window_type(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_has_native(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_has_native(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_hide(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_hide(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_iconify(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_iconify(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_invalidate_rect(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		const GdkRectangle* p2 = (const GdkRectangle*)p2_obj[0];

		const gboolean p3 = (gboolean)APITools_GetIntValue(context, 3) ? false : true;

		gdk_window_invalidate_rect(p1, p2, p3);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_is_destroyed(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_is_destroyed(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_is_input_only(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_is_input_only(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_is_shaped(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_is_shaped(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_is_viewable(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_is_viewable(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_is_visible(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		const auto status = gdk_window_is_visible(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_lower(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_lower(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_maximize(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_maximize(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_merge_child_input_shapes(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_merge_child_input_shapes(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_merge_child_shapes(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_merge_child_shapes(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_move(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);

		gdk_window_move(p1, p2, p3);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_move_resize(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);
		const gint p4 = (gint)APITools_GetIntValue(context, 4);
		const gint p5 = (gint)APITools_GetIntValue(context, 5);

		gdk_window_move_resize(p1, p2, p3, p4, p5);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_move_to_rect(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		const GdkRectangle* p2 = (const GdkRectangle*)p2_obj[0];

		const GdkGravity p3 = (GdkGravity)APITools_GetIntValue(context, 3);

		const GdkGravity p4 = (GdkGravity)APITools_GetIntValue(context, 4);

		const GdkAnchorHints p5 = (GdkAnchorHints)APITools_GetIntValue(context, 5);

		const gint p6 = (gint)APITools_GetIntValue(context, 6);
		const gint p7 = (gint)APITools_GetIntValue(context, 7);

		gdk_window_move_to_rect(p1, p2, p3, p4, p5, p6, p7);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_raise(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_raise(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_register_dnd(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_register_dnd(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_reparent(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkWindow* p2 = (GdkWindow*)p2_obj[0];

		const gint p3 = (gint)APITools_GetIntValue(context, 3);
		const gint p4 = (gint)APITools_GetIntValue(context, 4);

		gdk_window_reparent(p1, p2, p3, p4);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_resize(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);

		gdk_window_resize(p1, p2, p3);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_restack(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkWindow* p2 = (GdkWindow*)p2_obj[0];

		const gboolean p3 = (gboolean)APITools_GetIntValue(context, 3) ? false : true;

		gdk_window_restack(p1, p2, p3);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_scroll(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);

		gdk_window_scroll(p1, p2, p3);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_accept_focus(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_accept_focus(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_child_input_shapes(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_set_child_input_shapes(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_child_shapes(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_set_child_shapes(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_cursor(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkCursor* p2 = (GdkCursor*)p2_obj[0];

		gdk_window_set_cursor(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_decorations(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const GdkWMDecoration p2 = (GdkWMDecoration)APITools_GetIntValue(context, 2);

		gdk_window_set_decorations(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_device_cursor(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		const size_t* p3_obj = APITools_GetObjectValue(context, 3);
		GdkCursor* p3 = (GdkCursor*)p3_obj[0];

		gdk_window_set_device_cursor(p1, p2, p3);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_device_events(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		const GdkEventMask p3 = (GdkEventMask)APITools_GetIntValue(context, 3);

		gdk_window_set_device_events(p1, p2, p3);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_event_compression(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_event_compression(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_events(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const GdkEventMask p2 = (GdkEventMask)APITools_GetIntValue(context, 2);

		gdk_window_set_events(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_focus_on_map(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_focus_on_map(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_fullscreen_mode(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const GdkFullscreenMode p2 = (GdkFullscreenMode)APITools_GetIntValue(context, 2);

		gdk_window_set_fullscreen_mode(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_geometry_hints(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		const GdkGeometry* p2 = (const GdkGeometry*)p2_obj[0];

		const GdkWindowHints p3 = (GdkWindowHints)APITools_GetIntValue(context, 3);

		gdk_window_set_geometry_hints(p1, p2, p3);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_group(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkWindow* p2 = (GdkWindow*)p2_obj[0];

		gdk_window_set_group(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_icon_name(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gchar* p2 = (gchar*)UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

		gdk_window_set_icon_name(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_keep_above(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_keep_above(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_keep_below(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_keep_below(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_modal_hint(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_modal_hint(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_opacity(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gdouble p2 = (gdouble)APITools_GetFloatValue(context, 2);

		gdk_window_set_opacity(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_override_redirect(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_override_redirect(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_pass_through(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_pass_through(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_role(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gchar* p2 = (gchar*)UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

		gdk_window_set_role(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_shadow_width(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gint p2 = (gint)APITools_GetIntValue(context, 2);
		const gint p3 = (gint)APITools_GetIntValue(context, 3);
		const gint p4 = (gint)APITools_GetIntValue(context, 4);
		const gint p5 = (gint)APITools_GetIntValue(context, 5);

		gdk_window_set_shadow_width(p1, p2, p3, p4, p5);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_skip_pager_hint(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_skip_pager_hint(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_skip_taskbar_hint(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_skip_taskbar_hint(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_source_events(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const GdkInputSource p2 = (GdkInputSource)APITools_GetIntValue(context, 2);

		const GdkEventMask p3 = (GdkEventMask)APITools_GetIntValue(context, 3);

		gdk_window_set_source_events(p1, p2, p3);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_startup_id(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gchar* p2 = (gchar*)UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

		gdk_window_set_startup_id(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_support_multidevice(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_support_multidevice(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_title(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gchar* p2 = (gchar*)UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

		gdk_window_set_title(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_transient_for(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkWindow* p2 = (GdkWindow*)p2_obj[0];

		gdk_window_set_transient_for(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_type_hint(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const GdkWindowTypeHint p2 = (GdkWindowTypeHint)APITools_GetIntValue(context, 2);

		gdk_window_set_type_hint(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_urgency_hint(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_urgency_hint(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_show(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_show(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_show_unraised(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_show_unraised(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_show_window_menu(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkEvent* p2 = (GdkEvent*)p2_obj[0];

		const auto status = gdk_window_show_window_menu(p1, p2);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_stick(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_stick(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_thaw_updates(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_thaw_updates(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_unfullscreen(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_unfullscreen(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_unmaximize(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_unmaximize(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_unstick(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_unstick(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_withdraw(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);

		gdk_window_withdraw(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
		void gtk3_gdk_window_constrain_size(VMContext& context) {
		const size_t* p1_obj = APITools_GetObjectValue(context, 1);
		GdkGeometry* p1 = (GdkGeometry*)p1_obj[0];

		const GdkWindowHints p2 = (GdkWindowHints)APITools_GetIntValue(context, 2);

		const gint p3 = (gint)APITools_GetIntValue(context, 3);
		const gint p4 = (gint)APITools_GetIntValue(context, 4);

		gint new_width, new_height;
		gdk_window_constrain_size(p1, p2, p3, p4, &new_width, &new_height);
	}

	//
	// Atom
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_atom_name(VMContext& context) {
		GdkAtom p1 = (GdkAtom)APITools_GetIntValue(context, 1);

		const auto status = gdk_atom_name(p1);

		APITools_SetStringValue(context, 0, BytesToUnicode(status));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_atom_intern(VMContext& context) {
		const gchar* p1 = (gchar*)UnicodeToBytes(APITools_GetStringValue(context, 1)).c_str();
		const gboolean p2 = (gboolean)APITools_GetIntValue(context, 2) ? false : true;

		const auto status = gdk_atom_intern(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_atom_intern_static_string(VMContext& context) {
		const gchar* p1 = (gchar*)UnicodeToBytes(APITools_GetStringValue(context, 1)).c_str();

		const auto status = gdk_atom_intern_static_string(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}


	//
	// Color
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void color_get_pixel(VMContext& context) {
		const GdkColor* obj = (GdkColor*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->pixel);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void color_set_pixel(VMContext& context) {
		GdkColor* obj = (GdkColor*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->pixel = (guint32)value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void color_get_red(VMContext& context) {
		const GdkColor* obj = (GdkColor*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->red);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void color_set_red(VMContext& context) {
		GdkColor* obj = (GdkColor*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->red = (guint16)value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void color_get_green(VMContext& context) {
		const GdkColor* obj = (GdkColor*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->green);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void color_set_green(VMContext& context) {
		GdkColor* obj = (GdkColor*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->green = (guint16)value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void color_get_blue(VMContext& context) {
		const GdkColor* obj = (GdkColor*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->blue);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void color_set_blue(VMContext& context) {
		GdkColor* obj = (GdkColor*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->blue = (guint16)value;
	}


	//
	// EventAny
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventany_get_type(VMContext& context) {
		const GdkEventAny* obj = (GdkEventAny*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventany_set_type(VMContext& context) {
		GdkEventAny* obj = (GdkEventAny*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventany_get_window(VMContext& context) {
		const GdkEventAny* obj = (GdkEventAny*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventany_set_window(VMContext& context) {
		GdkEventAny* obj = (GdkEventAny*)APITools_GetIntValue(context, 0);
		size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventany_get_sendevent(VMContext& context) {
		const GdkEventAny* obj = (GdkEventAny*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventany_set_sendevent(VMContext& context) {
		GdkEventAny* obj = (GdkEventAny*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}


	//
	// EventButton
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_get_type(VMContext& context) {
		const GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_set_type(VMContext& context) {
		GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_get_window(VMContext& context) {
		const GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_set_window(VMContext& context) {
		GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_get_sendevent(VMContext& context) {
		const GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_set_sendevent(VMContext& context) {
		GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_get_time(VMContext& context) {
		const GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_set_time(VMContext& context) {
		GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_get_x(VMContext& context) {
		const GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_set_x(VMContext& context) {
		GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_get_y(VMContext& context) {
		const GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_set_y(VMContext& context) {
		GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_get_button(VMContext& context) {
		const GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->button);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_set_button(VMContext& context) {
		GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->button = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_get_device(VMContext& context) {
		const GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->device);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_set_device(VMContext& context) {
		GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->device = (Device)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_get_xroot(VMContext& context) {
		const GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_set_xroot(VMContext& context) {
		GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x_root = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_get_yroot(VMContext& context) {
		const GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventbutton_set_yroot(VMContext& context) {
		GdkEventButton* obj = (GdkEventButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y_root = value;
	}


	//
	// EventConfigure
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_get_type(VMContext& context) {
		const GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_set_type(VMContext& context) {
		GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_get_window(VMContext& context) {
		const GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_set_window(VMContext& context) {
		GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_get_sendevent(VMContext& context) {
		const GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_set_sendevent(VMContext& context) {
		GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_get_x(VMContext& context) {
		const GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->x);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_set_x(VMContext& context) {
		GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->x = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_get_y(VMContext& context) {
		const GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_set_y(VMContext& context) {
		GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->y = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_get_width(VMContext& context) {
		const GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->width);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_set_width(VMContext& context) {
		GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->width = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_get_height(VMContext& context) {
		const GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->height);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventconfigure_set_height(VMContext& context) {
		GdkEventConfigure* obj = (GdkEventConfigure*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->height = value;
	}


	//
	// EventCrossing
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_type(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_type(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_window(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_window(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_sendevent(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_sendevent(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_subwindow(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->subwindow);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_subwindow(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->subwindow = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_time(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_time(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_x(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_x(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_y(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_y(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_xroot(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_xroot(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x_root = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_yroot(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_yroot(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y_root = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_mode(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->mode);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_mode(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const auto value = (GdkCrossingMode)APITools_GetIntValue(context, 1);
		obj->mode = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_detail(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->detail);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_detail(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const auto value = (GdkNotifyType)APITools_GetIntValue(context, 1);
		obj->detail = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_get_focus(VMContext& context) {
		const GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->focus ? 1 : 0);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventcrossing_set_focus(VMContext& context) {
		GdkEventCrossing* obj = (GdkEventCrossing*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1) ? true : false;
		obj->focus = value;
	}


	//
	// EventDND
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_get_type(VMContext& context) {
		const GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_set_type(VMContext& context) {
		GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_get_window(VMContext& context) {
		const GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_set_window(VMContext& context) {
		GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_get_sendevent(VMContext& context) {
		const GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_set_sendevent(VMContext& context) {
		GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_get_context(VMContext& context) {
		const GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->context);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_set_context(VMContext& context) {
		GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->context = (DragContext)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_get_time(VMContext& context) {
		const GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_set_time(VMContext& context) {
		GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_get_xroot(VMContext& context) {
		const GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->x_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_set_xroot(VMContext& context) {
		GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->x_root = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_get_yroot(VMContext& context) {
		const GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->y_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventdnd_set_yroot(VMContext& context) {
		GdkEventDND* obj = (GdkEventDND*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->y_root = value;
	}


	//
	// EventExpose
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventexpose_get_type(VMContext& context) {
		const GdkEventExpose* obj = (GdkEventExpose*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventexpose_set_type(VMContext& context) {
		GdkEventExpose* obj = (GdkEventExpose*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventexpose_get_window(VMContext& context) {
		const GdkEventExpose* obj = (GdkEventExpose*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventexpose_set_window(VMContext& context) {
		GdkEventExpose* obj = (GdkEventExpose*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventexpose_get_sendevent(VMContext& context) {
		const GdkEventExpose* obj = (GdkEventExpose*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventexpose_set_sendevent(VMContext& context) {
		GdkEventExpose* obj = (GdkEventExpose*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventexpose_get_count(VMContext& context) {
		const GdkEventExpose* obj = (GdkEventExpose*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->count);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventexpose_set_count(VMContext& context) {
		GdkEventExpose* obj = (GdkEventExpose*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->count = value;
	}


	//
	// EventFocus
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventfocus_get_type(VMContext& context) {
		const GdkEventFocus* obj = (GdkEventFocus*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventfocus_set_type(VMContext& context) {
		GdkEventFocus* obj = (GdkEventFocus*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventfocus_get_window(VMContext& context) {
		const GdkEventFocus* obj = (GdkEventFocus*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventfocus_set_window(VMContext& context) {
		GdkEventFocus* obj = (GdkEventFocus*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventfocus_get_sendevent(VMContext& context) {
		const GdkEventFocus* obj = (GdkEventFocus*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventfocus_set_sendevent(VMContext& context) {
		GdkEventFocus* obj = (GdkEventFocus*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventfocus_get_in(VMContext& context) {
		const GdkEventFocus* obj = (GdkEventFocus*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->in);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventfocus_set_in(VMContext& context) {
		GdkEventFocus* obj = (GdkEventFocus*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->in = value;
	}


	//
	// EventGrabBroken
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_get_type(VMContext& context) {
		const GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_set_type(VMContext& context) {
		GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_get_window(VMContext& context) {
		const GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_set_window(VMContext& context) {
		GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_get_sendevent(VMContext& context) {
		const GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_set_sendevent(VMContext& context) {
		GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_get_keyboard(VMContext& context) {
		const GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->keyboard ? 1 : 0);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_set_keyboard(VMContext& context) {
		GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1) ? true : false;
		obj->keyboard = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_get_implicit(VMContext& context) {
		const GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->implicit ? 1 : 0);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_set_implicit(VMContext& context) {
		GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1) ? true : false;
		obj->implicit = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_get_grabwindow(VMContext& context) {
		const GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->grab_window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventgrabbroken_set_grabwindow(VMContext& context) {
		GdkEventGrabBroken* obj = (GdkEventGrabBroken*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->grab_window = (GdkWindow*)value_obj[0];
	}


	//
	// EventKey
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_get_type(VMContext& context) {
		const GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_set_type(VMContext& context) {
		GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_get_window(VMContext& context) {
		const GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_set_window(VMContext& context) {
		GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_get_sendevent(VMContext& context) {
		const GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_set_sendevent(VMContext& context) {
		GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_get_time(VMContext& context) {
		const GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_set_time(VMContext& context) {
		GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_get_keyval(VMContext& context) {
		const GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->keyval);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_set_keyval(VMContext& context) {
		GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->keyval = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_get_length(VMContext& context) {
		const GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->length);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_set_length(VMContext& context) {
		GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->length = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_get_string(VMContext& context) {
		const GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 1);
		APITools_SetStringValue(context, 0, obj->string);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_set_string(VMContext& context) {
		GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->string = (String)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_get_hardwarekeycode(VMContext& context) {
		const GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->hardware_keycode);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_set_hardwarekeycode(VMContext& context) {
		GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->hardware_keycode = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_get_group(VMContext& context) {
		const GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->group);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_set_group(VMContext& context) {
		GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->group = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_get_ismodifier(VMContext& context) {
		const GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->is_modifier);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventkey_set_ismodifier(VMContext& context) {
		GdkEventKey* obj = (GdkEventKey*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->is_modifier = value;
	}


	//
	// EventMotion
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_type(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_type(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_window(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_window(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_sendevent(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_sendevent(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_time(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_time(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_x(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_x(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_y(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_y(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_axes(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->axes);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_axes(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		*obj->axes = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_ishint(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->is_hint);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_ishint(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->is_hint = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_device(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->device);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_device(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->device = (Device)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_xroot(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_xroot(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x_root = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_get_yroot(VMContext& context) {
		const GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventmotion_set_yroot(VMContext& context) {
		GdkEventMotion* obj = (GdkEventMotion*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y_root = value;
	}


	//
	// EventOwnerChange
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_get_type(VMContext& context) {
		const GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_set_type(VMContext& context) {
		GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_get_window(VMContext& context) {
		const GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_set_window(VMContext& context) {
		GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_get_sendevent(VMContext& context) {
		const GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_set_sendevent(VMContext& context) {
		GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_get_owner(VMContext& context) {
		const GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->owner);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_set_owner(VMContext& context) {
		GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->owner = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_get_reason(VMContext& context) {
		const GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->reason);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_set_reason(VMContext& context) {
		GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 0);
		const auto value = (GdkOwnerChange)APITools_GetIntValue(context, 1);
		obj->reason = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_get_selection(VMContext& context) {
		const GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->selection);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_set_selection(VMContext& context) {
		GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->selection = (Atom)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_get_time(VMContext& context) {
		const GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_set_time(VMContext& context) {
		GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_get_selectiontime(VMContext& context) {
		const GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->selection_time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventownerchange_set_selectiontime(VMContext& context) {
		GdkEventOwnerChange* obj = (GdkEventOwnerChange*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->selection_time = value;
	}


	//
	// EventPadAxis
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_get_type(VMContext& context) {
		const GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_set_type(VMContext& context) {
		GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_get_window(VMContext& context) {
		const GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_set_window(VMContext& context) {
		GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_get_sendevent(VMContext& context) {
		const GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_set_sendevent(VMContext& context) {
		GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_get_time(VMContext& context) {
		const GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_set_time(VMContext& context) {
		GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_get_group(VMContext& context) {
		const GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->group);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_set_group(VMContext& context) {
		GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->group = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_get_index(VMContext& context) {
		const GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->index);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_set_index(VMContext& context) {
		GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->index = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_get_mode(VMContext& context) {
		const GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->mode);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_set_mode(VMContext& context) {
		GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->mode = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_get_value(VMContext& context) {
		const GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->value);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadaxis_set_value(VMContext& context) {
		GdkEventPadAxis* obj = (GdkEventPadAxis*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->value = value;
	}


	//
	// EventPadButton
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_get_type(VMContext& context) {
		const GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_set_type(VMContext& context) {
		GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_get_window(VMContext& context) {
		const GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_set_window(VMContext& context) {
		GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_get_sendevent(VMContext& context) {
		const GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_set_sendevent(VMContext& context) {
		GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_get_time(VMContext& context) {
		const GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_set_time(VMContext& context) {
		GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_get_group(VMContext& context) {
		const GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->group);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_set_group(VMContext& context) {
		GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->group = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_get_button(VMContext& context) {
		const GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->button);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_set_button(VMContext& context) {
		GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->button = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_get_mode(VMContext& context) {
		const GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->mode);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadbutton_set_mode(VMContext& context) {
		GdkEventPadButton* obj = (GdkEventPadButton*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->mode = value;
	}


	//
	// EventPadGroupMode
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_get_type(VMContext& context) {
		const GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_set_type(VMContext& context) {
		GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_get_window(VMContext& context) {
		const GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_set_window(VMContext& context) {
		GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_get_sendevent(VMContext& context) {
		const GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_set_sendevent(VMContext& context) {
		GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_get_time(VMContext& context) {
		const GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_set_time(VMContext& context) {
		GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_get_group(VMContext& context) {
		const GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->group);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_set_group(VMContext& context) {
		GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->group = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_get_mode(VMContext& context) {
		const GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->mode);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventpadgroupmode_set_mode(VMContext& context) {
		GdkEventPadGroupMode* obj = (GdkEventPadGroupMode*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->mode = value;
	}


	//
	// EventProperty
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproperty_get_type(VMContext& context) {
		const GdkEventProperty* obj = (GdkEventProperty*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproperty_set_type(VMContext& context) {
		GdkEventProperty* obj = (GdkEventProperty*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproperty_get_window(VMContext& context) {
		const GdkEventProperty* obj = (GdkEventProperty*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproperty_set_window(VMContext& context) {
		GdkEventProperty* obj = (GdkEventProperty*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproperty_get_sendevent(VMContext& context) {
		const GdkEventProperty* obj = (GdkEventProperty*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproperty_set_sendevent(VMContext& context) {
		GdkEventProperty* obj = (GdkEventProperty*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproperty_get_atom(VMContext& context) {
		const GdkEventProperty* obj = (GdkEventProperty*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->atom);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproperty_set_atom(VMContext& context) {
		GdkEventProperty* obj = (GdkEventProperty*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->atom = (Atom)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproperty_get_time(VMContext& context) {
		const GdkEventProperty* obj = (GdkEventProperty*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproperty_set_time(VMContext& context) {
		GdkEventProperty* obj = (GdkEventProperty*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}


	//
	// EventProximity
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproximity_get_type(VMContext& context) {
		const GdkEventProximity* obj = (GdkEventProximity*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproximity_set_type(VMContext& context) {
		GdkEventProximity* obj = (GdkEventProximity*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproximity_get_window(VMContext& context) {
		const GdkEventProximity* obj = (GdkEventProximity*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproximity_set_window(VMContext& context) {
		GdkEventProximity* obj = (GdkEventProximity*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproximity_get_sendevent(VMContext& context) {
		const GdkEventProximity* obj = (GdkEventProximity*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproximity_set_sendevent(VMContext& context) {
		GdkEventProximity* obj = (GdkEventProximity*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproximity_get_time(VMContext& context) {
		const GdkEventProximity* obj = (GdkEventProximity*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproximity_set_time(VMContext& context) {
		GdkEventProximity* obj = (GdkEventProximity*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproximity_get_device(VMContext& context) {
		const GdkEventProximity* obj = (GdkEventProximity*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->device);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventproximity_set_device(VMContext& context) {
		GdkEventProximity* obj = (GdkEventProximity*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->device = (Device)value_obj[0];
	}


	//
	// EventScroll
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_type(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_type(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_window(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_window(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_sendevent(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_sendevent(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_time(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_time(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_x(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_x(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_y(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_y(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_direction(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->direction);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_direction(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = (GdkScrollDirection)APITools_GetIntValue(context, 1);
		obj->direction = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_device(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->device);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_device(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->device = (Device)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_xroot(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_xroot(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x_root = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_yroot(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_yroot(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y_root = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_deltax(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->delta_x);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_deltax(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->delta_x = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_deltay(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->delta_y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_deltay(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->delta_y = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_get_isstop(VMContext& context) {
		const GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->is_stop);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventscroll_set_isstop(VMContext& context) {
		GdkEventScroll* obj = (GdkEventScroll*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->is_stop = value;
	}


	//
	// EventSelection
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_get_type(VMContext& context) {
		const GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_set_type(VMContext& context) {
		GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_get_window(VMContext& context) {
		const GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_set_window(VMContext& context) {
		GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_get_sendevent(VMContext& context) {
		const GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_set_sendevent(VMContext& context) {
		GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_get_selection(VMContext& context) {
		const GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->selection);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_set_selection(VMContext& context) {
		GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->selection = (Atom)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_get_target(VMContext& context) {
		const GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->target);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_set_target(VMContext& context) {
		GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->target = (Atom)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_get_property(VMContext& context) {
		const GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->property);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_set_property(VMContext& context) {
		GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->property = (Atom)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_get_time(VMContext& context) {
		const GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_set_time(VMContext& context) {
		GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_get_requestor(VMContext& context) {
		const GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->requestor);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventselection_set_requestor(VMContext& context) {
		GdkEventSelection* obj = (GdkEventSelection*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->requestor = (GdkWindow*)value_obj[0];
	}


	//
	// EventSequence
	//

	//
	// EventSetting
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventsetting_get_type(VMContext& context) {
		const GdkEventSetting* obj = (GdkEventSetting*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventsetting_set_type(VMContext& context) {
		GdkEventSetting* obj = (GdkEventSetting*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventsetting_get_window(VMContext& context) {
		const GdkEventSetting* obj = (GdkEventSetting*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventsetting_set_window(VMContext& context) {
		GdkEventSetting* obj = (GdkEventSetting*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventsetting_get_sendevent(VMContext& context) {
		const GdkEventSetting* obj = (GdkEventSetting*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventsetting_set_sendevent(VMContext& context) {
		GdkEventSetting* obj = (GdkEventSetting*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventsetting_get_action(VMContext& context) {
		const GdkEventSetting* obj = (GdkEventSetting*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->action);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventsetting_set_action(VMContext& context) {
		GdkEventSetting* obj = (GdkEventSetting*)APITools_GetIntValue(context, 0);
		const auto value = (GdkSettingAction)APITools_GetIntValue(context, 1);
		obj->action = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventsetting_get_name(VMContext& context) {
		const GdkEventSetting* obj = (GdkEventSetting*)APITools_GetIntValue(context, 1);
		APITools_SetStringValue(context, 0, obj->name);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventsetting_set_name(VMContext& context) {
		GdkEventSetting* obj = (GdkEventSetting*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->name = (String)value_obj[0];
	}


	//
	// EventTouch
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_get_type(VMContext& context) {
		const GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_set_type(VMContext& context) {
		GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_get_window(VMContext& context) {
		const GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_set_window(VMContext& context) {
		GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_get_sendevent(VMContext& context) {
		const GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_set_sendevent(VMContext& context) {
		GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_get_time(VMContext& context) {
		const GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_set_time(VMContext& context) {
		GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_get_x(VMContext& context) {
		const GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_set_x(VMContext& context) {
		GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_get_y(VMContext& context) {
		const GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_set_y(VMContext& context) {
		GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_get_emulatingpointer(VMContext& context) {
		const GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->emulating_pointer ? 1 : 0);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_set_emulatingpointer(VMContext& context) {
		GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1) ? true : false;
		obj->emulating_pointer = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_get_device(VMContext& context) {
		const GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->device);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_set_device(VMContext& context) {
		GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->device = (Device)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_get_xroot(VMContext& context) {
		const GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_set_xroot(VMContext& context) {
		GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x_root = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_get_yroot(VMContext& context) {
		const GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouch_set_yroot(VMContext& context) {
		GdkEventTouch* obj = (GdkEventTouch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y_root = value;
	}


	//
	// EventTouchpadPinch
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_type(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_type(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_window(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_window(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_sendevent(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_sendevent(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_phase(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->phase);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_phase(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->phase = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_nfingers(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->n_fingers);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_nfingers(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->n_fingers = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_time(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_time(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_x(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_x(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_y(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_y(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_dx(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->dx);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_dx(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->dx = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_dy(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->dy);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_dy(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->dy = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_angledelta(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->angle_delta);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_angledelta(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->angle_delta = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_scale(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->scale);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_scale(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->scale = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_xroot(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_xroot(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x_root = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_get_yroot(VMContext& context) {
		const GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadpinch_set_yroot(VMContext& context) {
		GdkEventTouchpadPinch* obj = (GdkEventTouchpadPinch*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y_root = value;
	}


	//
	// EventTouchpadSwipe
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_type(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_type(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_window(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_window(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_sendevent(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_sendevent(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_phase(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->phase);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_phase(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->phase = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_nfingers(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->n_fingers);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_nfingers(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->n_fingers = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_time(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->time);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_time(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->time = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_x(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_x(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_y(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_y(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_dx(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->dx);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_dx(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->dx = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_dy(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->dy);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_dy(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->dy = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_xroot(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->x_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_xroot(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->x_root = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_get_yroot(VMContext& context) {
		const GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->y_root);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventtouchpadswipe_set_yroot(VMContext& context) {
		GdkEventTouchpadSwipe* obj = (GdkEventTouchpadSwipe*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->y_root = value;
	}


	//
	// EventVisibility
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventvisibility_get_type(VMContext& context) {
		const GdkEventVisibility* obj = (GdkEventVisibility*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventvisibility_set_type(VMContext& context) {
		GdkEventVisibility* obj = (GdkEventVisibility*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventvisibility_get_window(VMContext& context) {
		const GdkEventVisibility* obj = (GdkEventVisibility*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventvisibility_set_window(VMContext& context) {
		GdkEventVisibility* obj = (GdkEventVisibility*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventvisibility_get_sendevent(VMContext& context) {
		const GdkEventVisibility* obj = (GdkEventVisibility*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventvisibility_set_sendevent(VMContext& context) {
		GdkEventVisibility* obj = (GdkEventVisibility*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventvisibility_get_state(VMContext& context) {
		const GdkEventVisibility* obj = (GdkEventVisibility*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->state);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventvisibility_set_state(VMContext& context) {
		GdkEventVisibility* obj = (GdkEventVisibility*)APITools_GetIntValue(context, 0);
		const auto value = (GdkVisibilityState)APITools_GetIntValue(context, 1);
		obj->state = value;
	}


	//
	// EventWindowState
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventwindowstate_get_type(VMContext& context) {
		const GdkEventWindowState* obj = (GdkEventWindowState*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventwindowstate_set_type(VMContext& context) {
		GdkEventWindowState* obj = (GdkEventWindowState*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventwindowstate_get_window(VMContext& context) {
		const GdkEventWindowState* obj = (GdkEventWindowState*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventwindowstate_set_window(VMContext& context) {
		GdkEventWindowState* obj = (GdkEventWindowState*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window = (GdkWindow*)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventwindowstate_get_sendevent(VMContext& context) {
		const GdkEventWindowState* obj = (GdkEventWindowState*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->send_event);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventwindowstate_set_sendevent(VMContext& context) {
		GdkEventWindowState* obj = (GdkEventWindowState*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->send_event = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventwindowstate_get_changedmask(VMContext& context) {
		const GdkEventWindowState* obj = (GdkEventWindowState*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->changed_mask);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventwindowstate_set_changedmask(VMContext& context) {
		GdkEventWindowState* obj = (GdkEventWindowState*)APITools_GetIntValue(context, 0);
		const auto value = (GdkWindowState)APITools_GetIntValue(context, 1);
		obj->changed_mask = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventwindowstate_get_newwindowstate(VMContext& context) {
		const GdkEventWindowState* obj = (GdkEventWindowState*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->new_window_state);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void eventwindowstate_set_newwindowstate(VMContext& context) {
		GdkEventWindowState* obj = (GdkEventWindowState*)APITools_GetIntValue(context, 0);
		const auto value = (GdkWindowState)APITools_GetIntValue(context, 1);
		obj->new_window_state = value;
	}


	//
	// RGBA
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rgba_get_red(VMContext& context) {
		const GdkRGBA* obj = (GdkRGBA*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->red);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rgba_set_red(VMContext& context) {
		GdkRGBA* obj = (GdkRGBA*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->red = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rgba_get_green(VMContext& context) {
		const GdkRGBA* obj = (GdkRGBA*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->green);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rgba_set_green(VMContext& context) {
		GdkRGBA* obj = (GdkRGBA*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->green = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rgba_get_blue(VMContext& context) {
		const GdkRGBA* obj = (GdkRGBA*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->blue);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rgba_set_blue(VMContext& context) {
		GdkRGBA* obj = (GdkRGBA*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->blue = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rgba_get_alpha(VMContext& context) {
		const GdkRGBA* obj = (GdkRGBA*)APITools_GetIntValue(context, 1);
		APITools_SetFloatValue(context, 0, obj->alpha);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rgba_set_alpha(VMContext& context) {
		GdkRGBA* obj = (GdkRGBA*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetFloatValue(context, 1);
		obj->alpha = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_rgba_copy(VMContext& context) {
		const GdkRGBA* p1 = (const GdkRGBA*)APITools_GetIntValue(context, 1);

		const auto status = gdk_rgba_copy(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_rgba_equal(VMContext& context) {
		gconstpointer p1 = (gconstpointer)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		gconstpointer p2 = (gconstpointer)p2_obj[0];

		const auto status = gdk_rgba_equal(p1, p2);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_rgba_free(VMContext& context) {
		GdkRGBA* p1 = (GdkRGBA*)APITools_GetIntValue(context, 1);

		gdk_rgba_free(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_rgba_hash(VMContext& context) {
		gconstpointer p1 = (gconstpointer)APITools_GetIntValue(context, 1);

		const auto status = gdk_rgba_hash(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_rgba_parse(VMContext& context) {
		GdkRGBA* p1 = (GdkRGBA*)APITools_GetIntValue(context, 1);
		const gchar* p2 = (gchar*)UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

		const auto status = gdk_rgba_parse(p1, p2);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_rgba_to_string(VMContext& context) {
		const GdkRGBA* p1 = (const GdkRGBA*)APITools_GetIntValue(context, 1);

		const auto status = gdk_rgba_to_string(p1);

		APITools_SetStringValue(context, 0, BytesToUnicode(status));
	}


	//
	// Rectangle
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rectangle_get_x(VMContext& context) {
		const GdkRectangle* obj = (GdkRectangle*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->x);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rectangle_set_x(VMContext& context) {
		GdkRectangle* obj = (GdkRectangle*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->x = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rectangle_get_y(VMContext& context) {
		const GdkRectangle* obj = (GdkRectangle*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->y);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rectangle_set_y(VMContext& context) {
		GdkRectangle* obj = (GdkRectangle*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->y = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rectangle_get_width(VMContext& context) {
		const GdkRectangle* obj = (GdkRectangle*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->width);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rectangle_set_width(VMContext& context) {
		GdkRectangle* obj = (GdkRectangle*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->width = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rectangle_get_height(VMContext& context) {
		const GdkRectangle* obj = (GdkRectangle*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, obj->height);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void rectangle_set_height(VMContext& context) {
		GdkRectangle* obj = (GdkRectangle*)APITools_GetIntValue(context, 0);
		const auto value = APITools_GetIntValue(context, 1);
		obj->height = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_rectangle_equal(VMContext& context) {
		const GdkRectangle* p1 = (const GdkRectangle*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		const GdkRectangle* p2 = (const GdkRectangle*)p2_obj[0];

		const auto status = gdk_rectangle_equal(p1, p2);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_rectangle_intersect(VMContext& context) {
		const GdkRectangle* p1 = (const GdkRectangle*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		const GdkRectangle* p2 = (const GdkRectangle*)p2_obj[0];

		const size_t* p3_obj = APITools_GetObjectValue(context, 3);
		GdkRectangle* p3 = (GdkRectangle*)p3_obj[0];

		const auto status = gdk_rectangle_intersect(p1, p2, p3);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_rectangle_union(VMContext& context) {
		const GdkRectangle* p1 = (const GdkRectangle*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		const GdkRectangle* p2 = (const GdkRectangle*)p2_obj[0];

		const size_t* p3_obj = APITools_GetObjectValue(context, 3);
		GdkRectangle* p3 = (GdkRectangle*)p3_obj[0];

		gdk_rectangle_union(p1, p2, p3);
	}


	//
	// Event
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_type(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->type);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_type(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const auto value = (GdkEventType)APITools_GetIntValue(context, 1);
		obj->type = value;
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_any(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->any);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_any(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->any = (GdkEventAny)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_expose(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->expose);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_expose(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->expose = (GdkEventExpose)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_visibility(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->visibility);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_visibility(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->visibility = (GdkEventVisibility)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_motion(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->motion);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_motion(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->motion = (GdkEventMotion)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_button(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->button);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_button(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->button = (GdkEventButton)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_touch(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->touch);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_touch(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->touch = (GdkEventTouch)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_scroll(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->scroll);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_scroll(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->scroll = (GdkEventScroll)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_key(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->key);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_key(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->key = (GdkEventKey)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_crossing(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->crossing);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_crossing(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->crossing = (GdkEventCrossing)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_focuschange(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->focus_change);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_focuschange(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->focus_change = (GdkEventFocus)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_configure(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->configure);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_configure(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->configure = (GdkEventConfigure)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_property(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->property);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_property(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->property = (GdkEventProperty)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_selection(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->selection);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_selection(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->selection = (GdkEventSelection)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_ownerchange(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->owner_change);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_ownerchange(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->owner_change = (GdkEventOwnerChange)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_proximity(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->proximity);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_proximity(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->proximity = (GdkEventProximity)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_dnd(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->dnd);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_dnd(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->dnd = (GdkEventDND)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_windowstate(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->window_state);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_windowstate(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->window_state = (GdkEventWindowState)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_setting(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->setting);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_setting(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->setting = (GdkEventSetting)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_grabbroken(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->grab_broken);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_grabbroken(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->grab_broken = (GdkEventGrabBroken)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_touchpadswipe(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->touchpad_swipe);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_touchpadswipe(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->touchpad_swipe = (GdkEventTouchpadSwipe)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_touchpadpinch(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->touchpad_pinch);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_touchpadpinch(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->touchpad_pinch = (GdkEventTouchpadPinch)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_padbutton(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->pad_button);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_padbutton(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->pad_button = (GdkEventPadButton)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_padaxis(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->pad_axis);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_padaxis(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->pad_axis = (GdkEventPadAxis)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_get_padgroupmode(VMContext& context) {
		const GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)obj->pad_group_mode);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void event_set_padgroupmode(VMContext& context) {
		GdkEvent* obj = (GdkEvent*)APITools_GetIntValue(context, 0);
		const size_t* value_obj = APITools_GetObjectValue(context, 1);
		obj->pad_group_mode = (GdkEventPadGroupMode)value_obj[0];
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_new(VMContext& context) {
		const GdkEventType p1 = (GdkEventType)APITools_GetIntValue(context, 1);

		const auto status = (size_t)gdk_event_new(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_events_get_angle(VMContext& context) {
		GdkEvent* p1 = (GdkEvent*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkEvent* p2 = (GdkEvent*)p2_obj[0];

		gdouble angle;
		const auto status = gdk_events_get_angle(p1, p2, &angle);

    APITools_SetFloatValue(context, 3, angle);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_events_get_center(VMContext& context) {
		GdkEvent* p1 = (GdkEvent*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkEvent* p2 = (GdkEvent*)p2_obj[0];

		gdouble x, y;
		const auto status = gdk_events_get_center(p1, p2, &x, &y);

    APITools_SetFloatValue(context, 3, x);
		APITools_SetFloatValue(context, 4, y);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_events_get_distance(VMContext& context) {
		GdkEvent* p1 = (GdkEvent*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkEvent* p2 = (GdkEvent*)p2_obj[0];

		gdouble distance;
		const auto status = gdk_events_get_distance(p1, p2, &distance);

		APITools_SetFloatValue(context, 3, distance);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_copy(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_copy(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_free(VMContext& context) {
		GdkEvent* p1 = (GdkEvent*)APITools_GetIntValue(context, 1);

		gdk_event_free(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_axis(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);
		const GdkAxisUse p2 = (GdkAxisUse)APITools_GetIntValue(context, 2);

		gdouble value_;
		const auto status = gdk_event_get_axis(p1, p2, &value_);

    APITools_SetFloatValue(context, 3, value_);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_button(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);
		
		guint button;
		const auto status = gdk_event_get_button(p1, &button);

    APITools_SetIntValue(context, 2, button);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_click_count(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);
		
		guint click_count;
		const auto status = gdk_event_get_click_count(p1, &click_count);

    APITools_SetIntValue(context, 2, click_count);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_coords(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);
		
		gdouble x_win, y_win;
		const auto status = gdk_event_get_coords(p1, &x_win, &y_win);

		APITools_SetFloatValue(context, 2, x_win);
		APITools_SetFloatValue(context, 3, y_win);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_device(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_device(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_device_tool(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_device_tool(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_event_sequence(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_event_sequence(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_event_type(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_event_type(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_keycode(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);
		
		guint16 keycode;
		const auto status = gdk_event_get_keycode(p1, &keycode);

    APITools_SetIntValue(context, 2, keycode);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_keyval(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		guint keyval;
		const auto status = gdk_event_get_keyval(p1, &keyval);

    APITools_SetIntValue(context, 2, keyval);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_pointer_emulated(VMContext& context) {
		GdkEvent* p1 = (GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_pointer_emulated(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_root_coords(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);
		
		gdouble x_root, y_root;
		const auto status = gdk_event_get_root_coords(p1, &x_root, &y_root);

		APITools_SetFloatValue(context, 2, x_root);
		APITools_SetFloatValue(context, 3, y_root);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_scancode(VMContext& context) {
		GdkEvent* p1 = (GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_scancode(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_screen(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_screen(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_scroll_deltas(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);
		
		gdouble delta_x, delta_y;
		const auto status = gdk_event_get_scroll_deltas(p1, &delta_x, &delta_y);

    APITools_SetFloatValue(context, 2, delta_x);
		APITools_SetFloatValue(context, 3, delta_y);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_scroll_direction(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);
		
		GdkScrollDirection direction;
		const auto status = gdk_event_get_scroll_direction(p1, &direction);

    APITools_SetIntValue(context, 2, direction);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_seat(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_seat(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_source_device(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_source_device(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_state(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);
		
		GdkModifierType state;
		const auto status = gdk_event_get_state(p1, &state);

    APITools_SetIntValue(context, 2, state);
    APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_time(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_time(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get_window(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_get_window(p1);

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_is_scroll_stop_event(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_is_scroll_stop_event(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_put(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		gdk_event_put(p1);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_set_device(VMContext& context) {
		GdkEvent* p1 = (GdkEvent*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		gdk_event_set_device(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_set_device_tool(VMContext& context) {
		GdkEvent* p1 = (GdkEvent*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDeviceTool* p2 = (GdkDeviceTool*)p2_obj[0];

		gdk_event_set_device_tool(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_set_screen(VMContext& context) {
		GdkEvent* p1 = (GdkEvent*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkScreen* p2 = (GdkScreen*)p2_obj[0];

		gdk_event_set_screen(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_set_source_device(VMContext& context) {
		GdkEvent* p1 = (GdkEvent*)APITools_GetIntValue(context, 1);
		const size_t* p2_obj = APITools_GetObjectValue(context, 2);
		GdkDevice* p2 = (GdkDevice*)p2_obj[0];

		gdk_event_set_source_device(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_triggers_context_menu(VMContext& context) {
		const GdkEvent* p1 = (const GdkEvent*)APITools_GetIntValue(context, 1);

		const auto status = gdk_event_triggers_context_menu(p1);

		APITools_SetIntValue(context, 0, status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_get(VMContext& context) {
		auto status = gdk_event_get();

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_peek(VMContext& context) {
		auto status = gdk_event_peek();

		APITools_SetIntValue(context, 0, (size_t)status);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_event_request_motions(VMContext& context) {
		const size_t* p1_obj = APITools_GetObjectValue(context, 1);
		const GdkEventMotion* p1 = (const GdkEventMotion*)p1_obj[0];

		gdk_event_request_motion);
	}
}