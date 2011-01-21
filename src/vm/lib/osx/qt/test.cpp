#include <QtGui>
#include <iostream>
#include <string>
#include "../../../../shared/dll_tools.h"

using namespace std;

extern "C" {
  void load_lib() {}
  void unload_lib() {}

  void foo(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
	 int argc = 0;
	 char* argv[0];

    QApplication app(argc, argv);
    QWidget window;
    window.resize(320, 240);
    window.show();
    window.setWindowTitle(QApplication::translate("toplevel", "Top-level widget"));
    app.exec();
  }
}
