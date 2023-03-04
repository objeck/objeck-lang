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

		const gchar* p2 = UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

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
		const gchar* p2 = UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

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
		// TODO: test call
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
		const gdouble p2 = APITools_GetFloatValue(context, 2);

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

		const gboolean p3 = APITools_GetIntValue(context, 3) ? false : true;

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

		const gboolean p3 = APITools_GetIntValue(context, 3) ? false : true;

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
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

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
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

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
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

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
		const gchar* p2 = UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

		gdk_window_set_icon_name(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_keep_above(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_keep_above(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_keep_below(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_keep_below(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_modal_hint(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_modal_hint(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_opacity(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gdouble p2 = APITools_GetFloatValue(context, 2);

		gdk_window_set_opacity(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_override_redirect(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_override_redirect(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_pass_through(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_pass_through(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_role(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gchar* p2 = UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

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
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_skip_pager_hint(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_skip_taskbar_hint(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

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
		const gchar* p2 = UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

		gdk_window_set_startup_id(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_support_multidevice(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

		gdk_window_set_support_multidevice(p1, p2);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void gtk3_gdk_window_set_title(VMContext& context) {
		GdkWindow* p1 = (GdkWindow*)APITools_GetIntValue(context, 1);
		const gchar* p2 = UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

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
		const gboolean p2 = APITools_GetIntValue(context, 2) ? false : true;

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

		APITools_SetIntValue(context, 5, new_width);
		APITools_SetIntValue(context, 6, new_height);
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
		const gchar* p2 = UnicodeToBytes(APITools_GetStringValue(context, 2)).c_str();

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
		obj->x = (gint)value;
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
		obj->y = (gint)value;
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
		obj->width = (gint)value;
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
		obj->height = (gint)value;
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

}