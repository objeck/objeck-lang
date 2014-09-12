#include "dialogs.h"

///////////////////////////////////////////////////////////////////////////////
/// Class GeneralOptions
///////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(GeneralOptions, wxDialog)
EVT_BUTTON(myID_DLG_OPTIONS_PATH, GeneralOptions::OnFilePath)
EVT_RADIOBUTTON(myID_DLG_OPTIONS_TABS, GeneralOptions::OnTabs)
EVT_RADIOBUTTON(myID_DLG_OPTIONS_SPACES, GeneralOptions::OnSpaces)
END_EVENT_TABLE()

GeneralOptions::GeneralOptions( wxWindow* parent, IniManager* ini, const wxString &objeck_path, const wxString &ident, 
                                const wxString &line_endings, wxWindowID id, const wxString& title, const wxPoint& pos, 
                                const wxSize& size, long style ) 
  : wxDialog( parent, id, title, pos, size, style )
{
  iniManager = ini;
	this->SetSizeHints( wxSize( 300,-1 ), wxDefaultSize );
	
	wxBoxSizer* dialogSizer;
	dialogSizer = new wxBoxSizer( wxVERTICAL );
	
  // objeck path
	wxBoxSizer* pathSizer;
	pathSizer = new wxBoxSizer( wxHORIZONTAL );
	
	m_PathLabel = new wxStaticText( this, wxID_ANY, wxT("Objeck Path"), wxDefaultPosition, wxDefaultSize, 0 );
	m_PathLabel->Wrap( -1 );
	pathSizer->Add( m_PathLabel, 0, wxALL, 5 );
	
	m_pathText = new wxTextCtrl( this, wxID_ANY, objeck_path, wxDefaultPosition, wxDefaultSize, 0 );
	pathSizer->Add( m_pathText, 1, wxALL, 5 );
  
  m_pathSelectButton = new wxButton( this, myID_DLG_OPTIONS_PATH, wxT("..."), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
	pathSizer->Add( m_pathSelectButton, 0, wxALL, 5 );
	
	dialogSizer->Add( pathSizer, 1, wxEXPAND, 5 );
	
  // indentation spacing
	wxBoxSizer* spacingBoxer;
	spacingBoxer = new wxBoxSizer( wxHORIZONTAL );
	
	m_spacingLabel = new wxStaticText( this, wxID_ANY, wxT("Spacing"), wxDefaultPosition, wxDefaultSize, 0 );
	m_spacingLabel->Wrap( -1 );
	spacingBoxer->Add( m_spacingLabel, 0, wxALL, 5 );
	
	m_tabIndentButton = new wxRadioButton( this, myID_DLG_OPTIONS_SPACES, wxT("Tabs"), wxDefaultPosition, wxDefaultSize, 0 );
	spacingBoxer->Add( m_tabIndentButton, 0, wxALL, 5 );
	
	m_spaceIndentButton = new wxRadioButton( this, myID_DLG_OPTIONS_TABS, wxT("Spaces"), wxDefaultPosition, wxDefaultSize, 0 );
	spacingBoxer->Add( m_spaceIndentButton, 0, wxALL, 5 );
	
	m_numSpacesSpin = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 8, 3); // new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	spacingBoxer->Add( m_numSpacesSpin, 0, wxALIGN_BOTTOM|wxTOP|wxBOTTOM|wxRIGHT, 5 );

  // tabs
  const size_t space_index = ident.find(L';');
  if(space_index == wstring::npos) {
    m_tabIndentButton->SetValue(true);
    m_numSpacesSpin->Disable();
  }
  // spaces
  else {
    m_spaceIndentButton->SetValue(true);
    const wxString num_spaces(ident.substr(space_index + 1));
    m_numSpacesSpin->Enable();
    m_numSpacesSpin->SetValue(num_spaces);
  }
	
	dialogSizer->Add( spacingBoxer, 1, wxEXPAND, 5 );
	
  // line endings
	wxBoxSizer* lineEndingSizer;
	lineEndingSizer = new wxBoxSizer( wxHORIZONTAL );
	
	wxString m_lineFeedRadioChoices[] = { wxT("Windows"), wxT("Unix"), wxT("Mac") };
	int m_lineFeedRadioNChoices = sizeof( m_lineFeedRadioChoices ) / sizeof( wxString );
	m_lineFeedRadio = new wxRadioBox( this, wxID_ANY, wxT("Line Feeds"), wxDefaultPosition, wxDefaultSize, 
                                    m_lineFeedRadioNChoices, m_lineFeedRadioChoices, 1, 0 );

  if(line_endings == L"Windows") {
    m_lineFeedRadio->SetSelection(0);  
  }
  else if(line_endings == L"Unix") {
    m_lineFeedRadio->SetSelection(1);  
  }
  else if(line_endings == L"Mac") {
    m_lineFeedRadio->SetSelection(2);  
  }
  else {
    m_lineFeedRadio->SetSelection(0);
  }
	
	lineEndingSizer->Add( m_lineFeedRadio, 1, wxALL, 5 );
	
	dialogSizer->Add( lineEndingSizer, 0, 0, 5 );
  
	dialogSizer->Add( new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL ), 0, wxEXPAND | wxALL, 5 );
	
	m_OkCancelSizer = new wxStdDialogButtonSizer();
	m_OkCancelSizerSave = new wxButton( this, wxID_OK );
	m_OkCancelSizer->AddButton( m_OkCancelSizerSave );
	m_OkCancelSizerCancel = new wxButton( this, wxID_CANCEL );
	m_OkCancelSizer->AddButton( m_OkCancelSizerCancel );
	m_OkCancelSizer->Realize();
	dialogSizer->Add( m_OkCancelSizer, 1, wxEXPAND, 5 );
	
	this->SetSizer( dialogSizer );
	this->Layout();
	dialogSizer->Fit( this );
}

GeneralOptions::~GeneralOptions()
{
}

// events
void GeneralOptions::OnFilePath(wxCommandEvent& event)
{
  wxDirDialog dirDialog(this, wxT("Objeck Root Path"), wxEmptyString, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
  if(dirDialog.ShowModal() == wxID_OK) {
    m_pathText->SetValue(dirDialog.GetPath());
  }
}

void GeneralOptions::OnSpaces(wxCommandEvent& event)
{
  m_numSpacesSpin->Disable();
}

void GeneralOptions::OnTabs(wxCommandEvent& event)
{
  m_numSpacesSpin->Enable();
}

// TODO: get values from controls
void GeneralOptions::ShowAndUpdate() 
{
  if(ShowModal() == wxID_OK) {    
    // objeck path
    const wstring std_objeck_path = m_pathText->GetValue().ToStdWstring();
    iniManager->SetValue(L"Options", L"objeck_path", std_objeck_path);
    
    // line endings
    if(m_lineFeedRadio->GetSelection() == 0) {
      iniManager->SetValue(L"Options", L"line_ending", L"Windows");  
    }
    else if(m_lineFeedRadio->GetSelection() == 1) {
      iniManager->SetValue(L"Options", L"line_ending", L"Unix");  
    } 
    else {
      iniManager->SetValue(L"Options", L"line_ending", L"Mac");  
    }
    
    // indentation
    if(m_tabIndentButton->GetValue()) {
      iniManager->SetValue(L"Options", L"indent_spacing", L"tabs");
    }
    else if(m_spaceIndentButton->GetValue()) {
      wstring indent = wxString::Format(wxT("%s;%i"),wxT("spaces"), m_numSpacesSpin->GetValue()).ToStdWstring();
      iniManager->SetValue(L"Options", L"indent_spacing", indent);
    }
    
    // save
    iniManager->Save();
  }
}

///////////////////////////////////////////////////////////////////////////////
/// Class NewProject
///////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(NewProject, wxDialog)
EVT_BUTTON(myID_DLG_PROJECT_PATH, NewProject::OnFilePath)
END_EVENT_TABLE()

NewProject::NewProject( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxDialog( parent, id, title, pos, size, style )
{
	this->SetSizeHints( wxSize( 320,-1 ), wxDefaultSize );
	
	wxBoxSizer* bSizer11;
	bSizer11 = new wxBoxSizer( wxVERTICAL );
	
	wxBoxSizer* pathSizer;
	pathSizer = new wxBoxSizer( wxHORIZONTAL );
	
	m_NameLabel = new wxStaticText( this, wxID_ANY, wxT("Name"), wxDefaultPosition, wxDefaultSize, 0 );
	m_NameLabel->Wrap( -1 );
	pathSizer->Add( m_NameLabel, 0, wxALL, 5 );
	
	m_nameText = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	pathSizer->Add( m_nameText, 1, wxALL, 5 );
	
	bSizer11->Add( pathSizer, 1, wxEXPAND, 5 );
	
	wxBoxSizer* pathSizer1;
	pathSizer1 = new wxBoxSizer( wxHORIZONTAL );
	
	m_PathLabel = new wxStaticText( this, wxID_ANY, wxT("Location"), wxDefaultPosition, wxDefaultSize, 0 );
	m_PathLabel->Wrap( -1 );
	pathSizer1->Add( m_PathLabel, 0, wxALL, 5 );
	
	m_pathText = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	pathSizer1->Add( m_pathText, 1, wxTOP|wxBOTTOM, 5 );
	
	m_button1 = new wxButton( this, myID_DLG_PROJECT_PATH, wxT("..."), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
	pathSizer1->Add( m_button1, 0, wxALL, 5 );
	
	bSizer11->Add( pathSizer1, 1, wxEXPAND, 5 );
	
  bSizer11->Add( new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL ), 0, wxEXPAND | wxALL, 5 );
  
	m_sdbSizer4 = new wxStdDialogButtonSizer();
	m_sdbSizer4OK = new wxButton( this, wxID_OK );
	m_sdbSizer4->AddButton( m_sdbSizer4OK );
	m_sdbSizer4Cancel = new wxButton( this, wxID_CANCEL );
	m_sdbSizer4->AddButton( m_sdbSizer4Cancel );
	m_sdbSizer4->Realize();
	bSizer11->Add( m_sdbSizer4, 1, wxEXPAND, 5 );
	
	this->SetSizer( bSizer11 );
	this->Layout();
	bSizer11->Fit( this );
}

NewProject::~NewProject()
{
}

// events
void NewProject::OnFilePath(wxCommandEvent& event)
{
  wxDirDialog dirDialog(this, wxT("Project Path"), wxEmptyString, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
  if(dirDialog.ShowModal() == wxID_OK) {
    m_pathText->SetValue(dirDialog.GetPath());
  }
}
