//////////////////////////////////////////////////////////////////////////////
// Original authors:  Wyo, John Labenski, Otto Wyss
// Copyright: (c) wxGuide, (c) John Labenski, Otto Wyss
// Modified by: Randy Hollines
// Licence: wxWindows licence
//////////////////////////////////////////////////////////////////////////////

#ifndef __IDE_H__
#define __IDE_H__

// #include <vld.h>

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

#include "editor.h"
#include "dialogs.h"
#include "opers.h"

class MyFrame;

//----------------------------------------------------------------------------
//! MyApp
class MyApp : public wxApp {

public:
  bool OnInit();
};

//----------------------------------------------------------------------------
//! TreeData
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

  const wxString GetName() {
    return name;
  }

  const wxString GetFullPath() {
    return full_path;
  }
};

//----------------------------------------------------------------------------
//! MyTreeCtrl
class MyTreeCtrl : public wxTreeCtrl {
  MyFrame* m_frame;
  wxString m_removePropertyName;

  void OnItemMenu(wxTreeEvent& event);
  void OnItemActivated(wxTreeEvent& event);

public:
  MyTreeCtrl(MyFrame* parent, const wxWindowID id, const wxPoint& pos, const wxSize& size, long style);
  ~MyTreeCtrl() {
  }

  const wxString GetRemovePropertyName() {
    return m_removePropertyName;
  }

  void CleanRemovePropertyName() {
    m_removePropertyName = wxEmptyString;
  }
  
  DECLARE_EVENT_TABLE();
};

//----------------------------------------------------------------------------
//! MyFrame
class MyFrame : public wxFrame {
  enum {
    ID_SampleItem
  };

  wxAuiManager aui_manager;
  Notebook* m_notebook;
  size_t m_newPageCount;
  GeneralOptionsManager* m_optionsManager;
  ProjectManager* m_projectManager;
  wxMenu* m_projectView;
  MyTreeCtrl* m_tree;

  void DoUpdate();

  // tree
  MyTreeCtrl* CreateTreeCtrl();
  // menu and toolbar
  wxMenuBar* CreateMenuBar();
  wxAuiToolBar* DoCreateToolBar();
  // tabbed editor
  Notebook* CreateNotebook();
  wxAuiNotebook* CreateInfoCtrl();

  wxString ReadInputStream(wxInputStream* in) {
    if(!in) {
      return wxEmptyString;
    }

    wxString out;
    while(in->CanRead() && !in->Eof()) {
      wxChar c = in->GetC();
      if(iswprint(c) || isspace(c)) {
        out.Append(c);
      }
    }

    return out;
  }

  bool IsProjectLoaded() {
    return m_tree != NULL;
  }

public:
  MyFrame(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos = wxDefaultPosition,
          const wxSize& size = wxDefaultSize, long style = wxDEFAULT_FRAME_STYLE | wxSUNKEN_BORDER);
  ~MyFrame();

  void OpenFile(const wxString &path) {
    if(m_notebook && path.size() > 0) {
      m_notebook->OpenFile(path);
    }
  }

  // project operations
  ProjectManager* GetProjectManager() {
    return m_projectManager;
  }

  void EnableProjectMenu() {
    m_projectView->Enable(myID_BUILD_PROJECT, true);
    m_projectView->Enable(myID_ADD_FILE_PROJECT, true);
    m_projectView->Enable(myID_REMOVE_FILE_PROJECT, true);
    m_projectView->Enable(myID_PROJECT_OPTIONS, true);
  }

  void DisableProjectMenu() {
    m_projectView->Enable(myID_BUILD_PROJECT, false);
    m_projectView->Enable(myID_ADD_FILE_PROJECT, false);
    m_projectView->Enable(myID_REMOVE_FILE_PROJECT, false);
    m_projectView->Enable(myID_PROJECT_OPTIONS, false);
  }

  void EnableProjectNode(wxTreeItemId id) {
    if(m_tree) {
      m_tree->Expand(id);
    }
  }

  void AddProjectSource(const wxString &source);
  void RemoveProjectSource(const wxString &source);

  // common
  void OnClose(wxCloseEvent &event);
  // project
  void OnProjectNew(wxCommandEvent &event);
  void OnProjectOpen(wxCommandEvent &event);
  void OnProjectClose(wxCommandEvent &event);
  void OnProjectBuild(wxCommandEvent &event);
  void OnAddProjectFile(wxCommandEvent &event);
  void OnRemoveProjectFile(wxCommandEvent &event);
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
