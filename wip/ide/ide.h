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
#include "wx/listctrl.h"
#include <wx/datstrm.h>

#include "editor.h"
#include "dialogs.h"
#include "opers.h"

class MyFrame;

//----------------------------------------------------------------------------
//! ProjectTreeCtrl
class ProjectTreeCtrl : public wxTreeCtrl {
  MyFrame* m_frame;
  TreeData* item_data;

  void OnItemMenu(wxTreeEvent& event);
  void OnItemActivated(wxTreeEvent& event);

public:
  ProjectTreeCtrl(MyFrame* parent, const wxWindowID id, const wxPoint& pos, const wxSize& size, long style);
  ~ProjectTreeCtrl() {}

  TreeData* GetData() {
    return item_data;
  }
  
  DECLARE_EVENT_TABLE();
};

//----------------------------------------------------------------------------
//! wxBuildErrorList
class wxBuildErrorList : public wxListCtrl {
  Notebook* m_notebook;

  void OnActivated(wxListEvent& event);

public:
  wxBuildErrorList(wxWindow *parent, Notebook* notebook, wxWindowID id, const wxPoint &pos, const wxSize &size, long style);
  ~wxBuildErrorList();

  void BuildSuccess(const wxString &output);
  int ShowErrors(const wxString &output);

  DECLARE_EVENT_TABLE()
};

//----------------------------------------------------------------------------
//! ExecuteTextCtrl
class ExecuteTextCtrl : public wxTextCtrl {

public:
  ExecuteTextCtrl(wxWindow* parent, wxWindowID id, const wxString &value, const wxPoint &pos, const wxSize &size, long style);
  ~ExecuteTextCtrl();

  DECLARE_EVENT_TABLE()
};

//----------------------------------------------------------------------------
//! MyFrame
class MyFrame : public wxFrame {
  enum {
    ID_SampleItem
  };

  wxAuiManager m_auiManager;
  wxBuildErrorList* m_buildOutput;
  ExecuteTextCtrl* m_executeOutput;
  Notebook* m_notebook;
  wxAuiNotebook* m_infoNotebook;
  size_t m_newPageCount;
  GeneralOptionsManager* m_optionsManager;
  ProjectManager* m_projectManager;
  wxMenu* m_projectView;
  ProjectTreeCtrl* m_tree;

  void DoUpdate();
  
  // tree
  ProjectTreeCtrl* CreateTreeCtrl();
  // menu and toolbar
  wxMenuBar* CreateMenuBar();
  wxAuiToolBar* DoCreateToolBar();
  // tabbed editor
  Notebook* CreateNotebook();
  wxAuiNotebook* CreateInfoCtrl();

  /****************************
  * Converts a UTF-8 bytes to
  * native a unicode string
  ****************************/
  static bool BytesToUnicode(const string &in, wstring &out) {
#ifdef _WIN32
    // allocate space
    int wsize = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), -1, NULL, 0);
    if(!wsize) {
      return false;
    }
    wchar_t* buffer = new wchar_t[wsize];

    // convert
    int check = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), -1, buffer, wsize);
    if(!check) {
      delete[] buffer;
      buffer = NULL;
      return false;
    }

    // create string
    out.append(buffer, wsize - 1);

    // clean up
    delete[] buffer;
    buffer = NULL;
#else
    // allocate space
    size_t size = mbstowcs(NULL, in.c_str(), in.size());
    if(size == (size_t)-1) {
      return false;
    }
    wchar_t* buffer = new wchar_t[size + 1];

    // convert
    size_t check = mbstowcs(buffer, in.c_str(), in.size());
    if(check == (size_t)-1) {
      delete[] buffer;
      buffer = NULL;
      return false;
    }
    buffer[size] = L'\0';

    // create string
    out.append(buffer, size);

    // clean up
    delete[] buffer;
    buffer = NULL;
#endif

    return true;
  }

  static wstring BytesToUnicode(const string &in) {
    wstring out;
    if(BytesToUnicode(in, out)) {
      return out;
    }

    return L"";
  }

  wxString ReadInputStream(wxInputStream* in) {
    if(!in) {
      return wxEmptyString;
    }

    wxString out;

#ifdef __WXMSW__
    int i = 0;
    wxChar buffer[256];
    while(!in->Eof()) {
      if(i < 256) {
        wxChar c;
        in->Read(&c, sizeof(c));
        buffer[i++] = c;
      }
      else {
        buffer[i - 1] = '\0';
        out += buffer;
        i = 0;
      }
    }
    buffer[i - 1] = '\0';
    out += buffer;
#else
    int i = 0;
    char buffer[256];
    while(!in->Eof()) {
      if(i < 256) {
        buffer[i++] = in->GetC();
      }
      else {
        buffer[i - 1] = '\0';
        out += wxString::FromUTF8(buffer);
        i = 0;
      }
    }
    buffer[i - 1] = '\0';
    out += wxString::FromUTF8(buffer);
#endif

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
  
  // common
  void OnClose(wxCloseEvent &event);
  // project
  void OnProjectNew(wxCommandEvent &event);
  void OnProjectOpen(wxCommandEvent &event);
  void OnProjectClose(wxCommandEvent &event);
  void OnProjectRun(wxCommandEvent &event);
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

//----------------------------------------------------------------------------
//! BuildProcess
class BuildProcess : public wxProcess {
public:
  BuildProcess() : wxProcess(wxPROCESS_REDIRECT) {}

  ~BuildProcess() {}
  
  void OnTerminate(int pid, int status) {}
};

//----------------------------------------------------------------------------
//! MyApp
class MyApp : public wxApp {

public:
  bool OnInit();
};
#endif
