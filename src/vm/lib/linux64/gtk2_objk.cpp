#include <iostream>
#include <stdlib.h>
#include <gtk/gtk.h>
#include "../../../shared/dll_tools.h"

using namespace std;

extern "C" {
 	typedef struct _callback_data {
		int cls_id;
		int mthd_id;
		long* op_stack;
		long* stack_pos;
		long* self;
		DLLTools_MethodCall_Ptr callback;
	} callback_data;

   static void destroy_callback_handler(GtkWidget *widget, gpointer data);

	void load_lib() {
		int argc = 0; char** argv = NULL;
		gtk_init(&argc, &argv);
	}

	void unload_lib() {
	}

	void g_window_new(long* data_array, long* op_stack, long* stack_pos, DLLTools_MethodCall_Ptr callback) {
		GtkWidget* window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
		DLLTools_SetIntValue(data_array, 0, (long)window);
	}

	void g_widget_show(long* data_array, long* op_stack, long* stack_pos, DLLTools_MethodCall_Ptr callback) {
		GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
		gtk_widget_show(widget);
	}

	void signal_connect(long* data_array, long* op_stack, long* stack_pos, DLLTools_MethodCall_Ptr callback) {

		GtkWidget* widget = (GtkWidget*)DLLTools_GetIntValue(data_array, 0);
		int signal = DLLTools_GetIntValue(data_array, 1);
		long* self = (long*)DLLTools_GetIntValue(data_array, 2);
		int cls_id = DLLTools_GetIntValue(data_array, 3);
		int mthd_id = DLLTools_GetIntValue(data_array, 4);
//    cout << "@@@ " << signal << ", " << cls_id << ", " << mthd_id << " @@@" << endl;
		callback_data* cbd = new callback_data;
		cbd->cls_id = cls_id;
		cbd->mthd_id = mthd_id;
		cbd->op_stack = op_stack;
		cbd->stack_pos = stack_pos;
		cbd->self = self;
		cbd->callback = callback;

		cout << "@@@ " << cbd << " @@@" << endl;

		switch(signal) {
		case -100:
			g_signal_connect(widget, "destroy", G_CALLBACK(destroy_callback_handler), (gpointer)cbd);
			break;
		}
   }

	void g_main(long* data_array, long* op_stack, long* stack_pos, DLLTools_MethodCall_Ptr callback) {
		gtk_main();
	}

   void destroy_callback_handler(GtkWidget* widget, gpointer data) {
		callback_data* cbd = (callback_data*)data;
		DLLTools_MethodCall_Ptr callback = cbd->callback;
		DLLTools_PushInt(cbd->op_stack, cbd->stack_pos, (long)cbd->self);
		(*callback)(cbd->op_stack, cbd->stack_pos, NULL, cbd->cls_id, cbd->mthd_id);
	}
}
