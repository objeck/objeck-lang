/***************************************************************************
* Objeck IDE
*
* Copyright (c) 2008-2013, Randy Hollines
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* - Redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer.
* - Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in
* the documentation and/or other materials provided with the distribution.
* - Neither the name of the Objeck team nor the names of its
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

#ifndef __IDE_H__
#define __IDE_H__

#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/log.h>
#include <wx/app.h>
#include <wx/frame.h>
#include <wx/scrolwin.h>
#include <wx/menu.h>
#include <wx/textdlg.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/aui/framemanager.h>
#endif

#ifndef wxHAS_IMAGES_IN_RESOURCES
#include "../sample.xpm"
#endif

//
// application
//
class MyApp : public wxApp {
public:
  MyApp() { 
  }

  virtual bool OnInit();

  wxDECLARE_NO_COPY_CLASS(MyApp);
};

//
// frame
//
enum {
  SPLIT_QUIT = 1,
  SPLIT_VERTICAL,
  SPLIT_BORDER
};

class MyFrame : public wxFrame
{
  wxWindow* top;
  wxWindow* bottom;
  wxSplitterWindow *main_splitter;
  wxAuiManager* aui_manager;

public:
  MyFrame();
  virtual ~MyFrame();

  void OnQuit(wxCommandEvent& WXUNUSED(event));
  void OnSplitVertical(wxCommandEvent& WXUNUSED(event));

  DECLARE_EVENT_TABLE()
  wxDECLARE_NO_COPY_CLASS(MyFrame);
};

BEGIN_EVENT_TABLE(MyFrame, wxFrame)
  EVT_MENU(SPLIT_VERTICAL, MyFrame::OnSplitVertical)
  EVT_MENU(SPLIT_QUIT, MyFrame::OnQuit)
END_EVENT_TABLE()

//
// main spilitter
//
class MySplitterWindow : public wxSplitterWindow
{
  wxFrame* parent;

public:
  MySplitterWindow(wxFrame* parent);

  // event handlers
  void OnDClick(wxSplitterEvent& event);
  void OnUnsplitEvent(wxSplitterEvent& event);

  DECLARE_EVENT_TABLE()
  wxDECLARE_NO_COPY_CLASS(MySplitterWindow);
};

BEGIN_EVENT_TABLE(MySplitterWindow, wxSplitterWindow)
  EVT_SPLITTER_DCLICK(wxID_ANY, MySplitterWindow::OnDClick)
  EVT_SPLITTER_UNSPLIT(wxID_ANY, MySplitterWindow::OnUnsplitEvent)
END_EVENT_TABLE()

#endif