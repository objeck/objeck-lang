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
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/radiobut.h>
#include <wx/spinctrl.h>
#include <wx/combobox.h>
#include <wx/statbox.h>
#include <wx/dialog.h>
#include <string>
#include <map>
#include <fstream>

#include "edit.h"
#include "../../src/shared/sys.h"

using namespace std;

class MyApp : public wxApp {

public:
  bool OnInit();
};

//----------------------------------------------------------------------------
//! InIManager
class InIManager {
  map<const wstring, map<const wstring, const wstring>*> section_map;
  wstring filename, input;
  wchar_t cur_char, next_char;
  size_t cur_pos;
  
  wstring LoadFile(wstring filename);
  bool WriteFile(const wstring &filename, const wstring &output);
  void NextChar();
  void Clear();
  wstring Serialize();
  void Deserialize();
  
 public:
  InIManager(const wstring &f);
  ~InIManager();
  
  wstring GetValue(const wstring &sec, const wstring &key);
  void SetValue(const wstring &sec, const wstring &key, const wstring &value);
  void Read();
  void Write();
};

//----------------------------------------------------------------------------
//! EditProperties
class GlobalOptions : public wxDialog {
wxStaticText* m_fontSelect;
  wxTextCtrl* m_textCtrl4;
  wxButton* m_pathButton;
  wxRadioButton* m_winEnding;
  wxRadioButton* m_unixEnding;
  wxRadioButton* m_macEndig;
  wxRadioButton* m_tabIdent;
  wxRadioButton* m_spaceIdent;
  wxSpinCtrl* m_identSize;
  wxComboBox* m_comboBox1;
  wxSpinCtrl* font_size;
  wxStdDialogButtonSizer* m_sdbSizer1;
  wxButton* m_sdbSizer1OK;
  wxButton* m_sdbSizer1Cancel;
  InIManager* m_IniManager;
  
 public:
  //! constructor
  GlobalOptions(wxWindow* parent, InIManager* ini, long style = 0);
  void DoShow();
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
  GlobalOptions* m_globalOptions;
  InIManager* m_iniManager;
  
  void DoUpdate();
  
  wxMenuBar* CreateMenuBar();
  wxAuiToolBar* DoCreateToolBar();
  wxTreeCtrl* CreateTreeCtrl();
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
