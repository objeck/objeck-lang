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
  void load_lib() {
    cout << "$$$ " << SIGNAL(clicked()) << ", " << SLOT(quit()) << " $$$" << endl;
    cout << "$$$ " << SIGNAL(quit(int)) << ", " << SLOT(clicked()) << " $$$" << endl;
  }
  
  void unload_lib() {}

  void qt_object_connect(long* data_array, long* op_stack, long *stack_pos, 
			 DLLTools_MethodCall_Ptr callback) {
#ifdef _DEBUG
    cout << "@@@@ qt_object_connect @@@@" << endl;
#endif
    
    long* sender = (long*)DLLTools_GetIntValue(data_array, 0);
    char* sender_str = (char*)DLLTools_GetStringValue(data_array, 1);    
    long* recv = (long*)DLLTools_GetIntValue(data_array, 2);
    char* recv_str = (char*)DLLTools_GetStringValue(data_array, 3);
    
    if(sender && sender_str && recv && recv_str) {
      QObject* sender_obj = (QObject*)sender[0];
      QObject* recv_obj = (QObject*)recv[0];
      
#ifdef _DEBUG
      cout << "@@@@ qt_object_connect: " << sender_obj << ":" << sender_str 
	   << ", " << recv << ": " << recv_str << "  @@@@" << endl;
#endif      
      QObject::connect(sender_obj, sender_str, recv_obj, recv_str);
    }
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


  void qt_pushbutton_new(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
    QWidget* widget = (QWidget*)DLLTools_GetIntValue(data_array, 1);
    QPushButton* button = new QPushButton(widget);
#ifdef _DEBUG
    cout << "@@@@ qt_pushbutton_new: " << button << " @@@@" << endl;
#endif
    DLLTools_SetIntValue(data_array, 0, (long)button);
  }

  
  void qt_qboxlayout_new(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
    QBoxLayout::Direction direction = (QBoxLayout::Direction)DLLTools_GetIntValue(data_array, 1);
    QBoxLayout* layout = new QBoxLayout(direction);
#ifdef _DEBUG
    cout << "@@@@ qt_qboxlayout_new: " << layout << " @@@@" << endl;
#endif
    DLLTools_SetIntValue(data_array, 0, (long)layout);
  }

  void qt_qboxlayout_new_1(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
    QBoxLayout::Direction direction = (QBoxLayout::Direction)DLLTools_GetIntValue(data_array, 1);
    QWidget* widget = (QWidget*)DLLTools_GetIntValue(data_array, 2);
    QBoxLayout* layout = new QBoxLayout(direction, widget);
#ifdef _DEBUG
    cout << "@@@@ qt_qboxlayout_new_1: " << layout << " @@@@" << endl;
#endif
    DLLTools_SetIntValue(data_array, 0, (long)layout);
  }
  
  void qt_qboxlayout_addwidget(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
#ifdef _DEBUG
    cout << "@@@@ qt_qboxlayout_addwidget @@@@" << endl;
#endif
    QBoxLayout* layout = (QBoxLayout*)DLLTools_GetIntValue(data_array, 0);
    if(layout) {
      QWidget* widget = (QWidget*)DLLTools_GetStringValue(data_array, 1);
      layout->addWidget(widget);
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
      widget->resize(width, height);
    }
  }

  void qt_widget_move(long* data_array, long* op_stack, long *stack_pos, DLLTools_MethodCall_Ptr callback) {
    QWidget* widget = (QWidget*)DLLTools_GetIntValue(data_array, 0);
#ifdef _DEBUG
    cout << "@@@@ qt_widget_move: " << widget << " @@@@" << endl;
#endif
    if(widget) {
      int x = DLLTools_GetIntValue(data_array, 1);
      int y = DLLTools_GetIntValue(data_array, 2);
      widget->move(x, y);
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
