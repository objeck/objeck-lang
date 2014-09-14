//////////////////////////////////////////////////////////////////////////////
// Original authors:  Wyo, John Labenski, Otto Wyss
// Copyright: (c) wxGuide, (c) John Labenski, Otto Wyss
// Modified by: Randy Hollines
// Licence: wxWindows licence
//////////////////////////////////////////////////////////////////////////////

#ifndef __IDE_H__
#define __IDE_H__

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#include "wx/app.h"
#include "wx/aui/aui.h"
#include "wx/treectrl.h"
#include "wx/artprov.h"
#include "wx/wxhtml.h"
#include "wx/utils.h"
#include "wx/process.h"

#include "edit.h"
#include "utils.h"
#include "dialogs.h"

class MyApp : public wxApp {

public:
  bool OnInit();
};

//----------------------------------------------------------------------------
//! MyFrame
class TreeData : public wxTreeItemData {
  wxString name;
  wxString full_path;

 public:
  TreeData(const wxString &n, const wxString &p) {
    name = n;
    full_path = p;
  }

  ~TreeData() {
  }

  wxString GetName() {
    return name;
  }

  wxString GetFullPath() {
    return full_path;
  }
};

class MyFrame : public wxFrame {
  enum {
    ID_SampleItem
  };

  wxAuiManager aui_manager;
  Notebook* m_notebook;
  size_t m_newPageCount;
  GeneralOptionsManager* m_optionsManager;
  ProjectManager* m_projectManager;

  wxTreeCtrl* m_Tree;
  wxArrayTreeItemIds m_sourceTreeItemsIds;
  wxTreeItemId m_sourceTreeItemId;
  
  void DoUpdate();
  
  // tree
  wxTreeCtrl* CreateTreeCtrl();
  void AddProjectSource(const wxString &source);
  void RemoveProjectSource(const wxString &source);
  // menu and toolbar
  wxMenuBar* CreateMenuBar();
  wxAuiToolBar* DoCreateToolBar();
  // tabbed editor
  Notebook* CreateNotebook();
  wxAuiNotebook* CreateInfoCtrl();
  
  wxString ReadInputStream(wxInputStream* in) {
    if (!in) {
      return wxEmptyString;
    }
    
    wxString out;
    while (in->CanRead() && !in->Eof()) {
      wxChar c = in->GetC();
      if(iswprint(c) || isspace(c)) {
        out.Append(c);
      }
    }
    
    return out;
  }

public:
  MyFrame(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize, long style = wxDEFAULT_FRAME_STYLE | wxSUNKEN_BORDER);
  ~MyFrame();

  // common
  void OnClose(wxCloseEvent &event);
  // project
  void OnProjectNew(wxCommandEvent &event);
  void OnProjectOpen(wxCommandEvent &event);
  void OnProjectClose(wxCommandEvent &event);
  // file
  void OnFileNew(wxCommandEvent &event);
  void OnFileNewFrame(wxCommandEvent &event);
  void OnFileOpen(wxCommandEvent &event);
  void OnFileOpenFrame(wxCommandEvent &event);
  void OnFileSave(wxCommandEvent &event);
  void OnFileSaveAs(wxCommandEvent &event);
  void OnFileClose(wxCommandEvent &event);
  void OnEdit(wxCommandEvent &event);
  void OnFindReplace(wxCommandEvent &event);
  void OnOptions(wxCommandEvent &event);

  DECLARE_EVENT_TABLE()
};

// TODO: move this into a class
class MyProcess : public wxProcess {
public:
  MyProcess() : wxProcess(wxPROCESS_REDIRECT) {}

  ~MyProcess() {}
  
  void OnTerminate(int pid, int status) {}
};

#endif
