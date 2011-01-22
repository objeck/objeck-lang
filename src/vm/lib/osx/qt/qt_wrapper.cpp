/***************************************************************************
 * QT wrapper
 *
 * Copyright (c) 2011, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and uses in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the StackVM Team nor the names of its
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

#include "qt_wrapper.h"

extern "C" {
  void load_lib() {}
  void unload_lib() {}

  void qt_object_connect(long* data_array, long* op_stack, long *stack_pos, 
			 DLLTools_MethodCall_Ptr callback) {
#ifdef _DEBUG
    cout << "@@@@ qt_object_connect @@@@" << endl;
#endif
    
  }

  void qt_widget_new(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
    QWidget* widget = new QWidget;
#ifdef _DEBUG
    cout << "@@@@ qt_widget_new: " << widget << " @@@@" << endl;
#endif
    DLLTools_SetIntValue(data_array, 0, (long)widget);
  }

  void qt_widget_show(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
#ifdef _DEBUG
    cout << "@@@@ qt_widget_show @@@@" << endl;
#endif
    QWidget* widget = (QWidget*)DLLTools_GetIntValue(data_array, 0);
    if(widget) {
      widget->show();
    }
  }

  void qt_widget_set_title(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
#ifdef _DEBUG
    cout << "@@@@ qt_widget_set_title @@@@" << endl;
#endif
    QWidget* widget = (QWidget*)DLLTools_GetIntValue(data_array, 0);
    if(widget) {
      char* title = DLLTools_GetStringValue(data_array, 1);
      widget->setWindowTitle(title);
    }
  }
  
  void qt_widget_resize(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
    QWidget* widget = (QWidget*)DLLTools_GetIntValue(data_array, 0);
#ifdef _DEBUG
    cout << "@@@@ qt_widget_resize: " << widget << " @@@@" << endl;
#endif
    if(widget) {
      int width = DLLTools_GetIntValue(data_array, 1);
      int height = DLLTools_GetIntValue(data_array, 2);
      cout << "### " << widget << ": " << width << ", " << height << " ###" << endl;
      widget->resize(width, height);
    }
  }

  void qt_app_new(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
#ifdef _DEBUG
    cout << "@@@@ qt_app_new @@@@" << endl;
#endif
    int argc = 0; char* argv[0];
    QApplication* application = new QApplication(argc, argv);
    DLLTools_SetIntValue(data_array, 0, (long)application);
  }
  
  void qt_app_exec(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
#ifdef _DEBUG
    cout << "@@@@ qt_app_exec @@@@" << endl;
#endif
    QApplication* application = (QApplication*)DLLTools_GetIntValue(data_array, 0);
    if(application) {
      application->exec();
    }
  }

  void qt_app_delete(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
#ifdef _DEBUG
    cout << "@@@@ qt_app_delete @@@@" << endl;
#endif
    QApplication* application = (QApplication*)DLLTools_GetIntValue(data_array, 0);
    if(application) {
      delete application;
      application = NULL;
    }
  }
}
