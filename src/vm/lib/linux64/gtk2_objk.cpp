#include <iostream>
#include <gtk/gtk.h>
#include "../../../shared/dll_tools.h"

using namespace std;

extern "C" {
   static void callback_handler(GtkWidget *widget, GdkEvent  *event, gpointer   data);

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
//		char* name = DLLTools_GetStringValue(data_array, 1);
		int signal = DLLTools_GetIntValue(data_array, 1);
		int cls_id = DLLTools_GetIntValue(data_array, 2);
		int mthd_id = DLLTools_GetIntValue(data_array, 3);
    cout << "@@@ " << signal << ", " << cls_id << ", " << mthd_id << " @@@" << endl;
		g_signal_connect(widget, "destroy", G_CALLBACK(callback_handler), NULL);
   }

	void g_main(long* data_array, long* op_stack, long* stack_pos, DLLTools_MethodCall_Ptr callback) {
		gtk_main();
	}

   void callback_handler(GtkWidget* widget, GdkEvent* event, gpointer data)
{
  g_print ("Hello World\n");
}

}
