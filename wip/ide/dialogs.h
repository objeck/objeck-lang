#ifndef __DIALOGS_H__
#define __DIALOGS_H__

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/sizer.h>
#include <wx/radiobut.h>
#include <wx/radiobox.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/statline.h>
#include <wx/spinctrl.h>

#include "utils.h"

///////////////////////////////////////////////////////////////////////////////
/// Class GeneralOptions
///////////////////////////////////////////////////////////////////////////////
class GeneralOptions : public wxDialog {
  GeneralOptionsManager* manager;
  wxStaticText* m_PathLabel;
  wxTextCtrl* m_pathText;
  wxButton* m_pathSelectButton;
  wxStaticText* m_spacingLabel;
  wxRadioButton* m_tabIndentButton;
  wxRadioButton* m_spaceIndentButton;
  wxSpinCtrl* m_numSpacesSpin;
  wxRadioBox* m_lineFeedRadio;
  wxStdDialogButtonSizer* m_OkCancelSizer;
  wxButton* m_OkCancelSizerSave;
  wxButton* m_OkCancelSizerCancel;

 public:
  GeneralOptions( wxWindow* parent, GeneralOptionsManager* mgr, const wxString &objeck_path, const wxString &indentation, 
                  const wxString &line_endings, wxWindowID id = wxID_ANY, const wxString& title = wxT("Options"), 
                  const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE );
  ~GeneralOptions();

  void ShowAndUpdate();
  void OnFilePath(wxCommandEvent& event);
  void OnSpaces(wxCommandEvent& event);
  void OnTabs(wxCommandEvent& event);
  
  wxString GetObjeckPath() {
    return wxEmptyString;
  }

  wxString GetIdentSpacing() {
    return wxEmptyString;
  }
  
  wxString GetLineEnding() {
    return wxEmptyString;
  }

  DECLARE_EVENT_TABLE()
};

///////////////////////////////////////////////////////////////////////////////
/// Class NewProject
///////////////////////////////////////////////////////////////////////////////
class NewProject : public wxDialog 
{
  wxStaticText* m_NameLabel;
  wxTextCtrl* m_nameText;
  wxStaticText* m_PathLabel;
  wxTextCtrl* m_pathText;
  wxButton* m_button1;
  wxStdDialogButtonSizer* m_sdbSizer4;
  wxButton* m_sdbSizer4OK;
  wxButton* m_sdbSizer4Cancel;

  void OnFilePath(wxCommandEvent& event);
	
 public:	
  NewProject( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = wxT("New Project"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE );
  ~NewProject();
  
  const wxString GetProjectName() {
    return m_nameText->GetValue();
  }

  const wxString GetPath() {
    return m_pathText->GetValue();
  }

  DECLARE_EVENT_TABLE()
};

#endif
