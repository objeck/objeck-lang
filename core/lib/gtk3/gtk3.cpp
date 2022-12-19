/***************************************************************************
 * GTK3 support for Objeck
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

#include <gtk/gtk.h>
#include "../../vm/lib_api.h"
#include "../../shared/sys.h"

extern "C" {
	//
	// GtkApplication
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void application_new(VMContext& context) {
		const std::string application_id = UnicodeToBytes(APITools_GetStringValue(context, 1));
		const GApplicationFlags flags = (GApplicationFlags)APITools_GetIntValue(context, 2);
		APITools_SetIntValue(context, 0, (size_t)gtk_application_new(application_id.c_str(), flags));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void application_remove_window(VMContext& context) {
		GtkApplication* application = (GtkApplication*)APITools_GetIntValue(context, 0);
		size_t* window_obj = (size_t*)APITools_GetObjectValue(context, 1);
		gtk_application_remove_window(application, (GtkWindow*)window_obj[0]);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void application_get_app_menu(VMContext& context) {
		GtkApplication* application = (GtkApplication*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)gtk_application_get_app_menu(application));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void application_get_menubar(VMContext& context) {
		GtkApplication* application = (GtkApplication*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)gtk_application_get_menubar(application));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void application_inhibit(VMContext& context) {
		GtkApplication* application = (GtkApplication*)APITools_GetIntValue(context, 1);
		size_t* window_obj = (size_t*)APITools_GetObjectValue(context, 2);
		const GtkApplicationInhibitFlags flags = (GtkApplicationInhibitFlags)APITools_GetIntValue(context, 3);
		const std::string reason = UnicodeToBytes(APITools_GetStringValue(context, 4));

		const int value = gtk_application_inhibit(application, (GtkWindow*)window_obj[0], flags, reason.c_str());
		APITools_SetIntValue(context, 0, value);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void application_is_inhibited(VMContext& context) {
		GtkApplication* application = (GtkApplication*)APITools_GetIntValue(context, 1);
		const GtkApplicationInhibitFlags flags = (GtkApplicationInhibitFlags)APITools_GetIntValue(context, 2);
		APITools_SetIntValue(context, 0, gtk_application_is_inhibited(application, flags));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void application_get_active_window(VMContext& context) {
		GtkApplication* application = (GtkApplication*)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)gtk_application_get_active_window(application));
	}

	//
	// GtkWindow
	//
#ifdef _WIN32
	__declspec(dllexport)
#endif
	void window_new(VMContext& context) {
		const GtkWindowType type = (GtkWindowType)APITools_GetIntValue(context, 1);
		APITools_SetIntValue(context, 0, (size_t)gtk_window_new(type));
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void window_get_title(VMContext& context) {
		GtkWindow* window = (GtkWindow*)APITools_GetIntValue(context, 1);
		const std::wstring value = BytesToUnicode(gtk_window_get_title(window));
		APITools_SetStringValue(context, 0, value);
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void window_set_role(VMContext& context) {
		GtkWindow* window = (GtkWindow*)APITools_GetIntValue(context, 0);
		const std::string role = UnicodeToBytes(APITools_GetStringValue(context, 1));
		gtk_window_set_role(window, role.c_str());
	}

#ifdef _WIN32
	__declspec(dllexport)
#endif
	void window_get_role(VMContext& context) {
		GtkWindow* window = (GtkWindow*)APITools_GetIntValue(context, 1);
		const std::wstring value = BytesToUnicode(gtk_window_get_role(window));
		APITools_SetStringValue(context, 0, value);
	}
}