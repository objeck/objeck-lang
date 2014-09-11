#include "dialogs.h"

///////////////////////////////////////////////////////////////////////////////
/// Class GeneralOptions
///////////////////////////////////////////////////////////////////////////////

GeneralOptions::GeneralOptions( wxWindow* parent, IniManager* ini, const wxString &objeck_path, const wxString &indentation, 
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
	
	dialogSizer->Add( pathSizer, 1, wxEXPAND, 5 );
	
  // indentation spacing
	wxBoxSizer* spacingBoxer;
	spacingBoxer = new wxBoxSizer( wxHORIZONTAL );
	
	m_spacingLabel = new wxStaticText( this, wxID_ANY, wxT("Spacing"), wxDefaultPosition, wxDefaultSize, 0 );
	m_spacingLabel->Wrap( -1 );
	spacingBoxer->Add( m_spacingLabel, 0, wxALL, 5 );
	
	m_tabSpacingButton = new wxRadioButton( this, wxID_ANY, wxT("Tabs"), wxDefaultPosition, wxDefaultSize, 0 );
	spacingBoxer->Add( m_tabSpacingButton, 0, wxALL, 5 );
	
	m_spacesTabButton = new wxRadioButton( this, wxID_ANY, wxT("Spaces"), wxDefaultPosition, wxDefaultSize, 0 );
	spacingBoxer->Add( m_spacesTabButton, 0, wxALL, 5 );
	
	m_numSpacesText = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	spacingBoxer->Add( m_numSpacesText, 0, wxALIGN_BOTTOM|wxTOP|wxBOTTOM|wxRIGHT, 5 );
	
	dialogSizer->Add( spacingBoxer, 1, wxEXPAND, 5 );
	
  // line endings
	wxBoxSizer* lineEndingSizer;
	lineEndingSizer = new wxBoxSizer( wxHORIZONTAL );
	
	wxString m_lineFeedRadioChoices[] = { wxT("Windows"), wxT("Unix"), wxT("Mac") };
	int m_lineFeedRadioNChoices = sizeof( m_lineFeedRadioChoices ) / sizeof( wxString );
	m_lineFeedRadio = new wxRadioBox( this, wxID_ANY, wxT("Line Feeds"), wxDefaultPosition, wxDefaultSize, m_lineFeedRadioNChoices, m_lineFeedRadioChoices, 1, 0 );
	m_lineFeedRadio->SetSelection( 2 );
	lineEndingSizer->Add( m_lineFeedRadio, 1, wxALL, 5 );
	
	dialogSizer->Add( lineEndingSizer, 0, 0, 5 );
  
	dialogSizer->Add( new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL ), 0, wxEXPAND | wxALL, 5 );
	
	m_OkCancelSizer = new wxStdDialogButtonSizer();
	m_OkCancelSizerSave = new wxButton( this, wxID_SAVE );
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

// TODO: get values from controls
void GeneralOptions::ShowAndUpdate() 
{
  if(ShowModal() == wxID_OK) {    
    // save changes
    const wstring std_objeck_path; //  = wxEmptyString.ToStdWstring();
    iniManager->SetValue(L"Options", L"objeck_path", std_objeck_path);
    
    const wstring std_ident_spacing; // =  wxEmptyString.ToStdWstring();
    iniManager->SetValue(L"Options", L"ident_spacing", std_ident_spacing);
    
    const wstring std_line_ending; // =  wxEmptyString.ToStdWstring();
    iniManager->SetValue(L"Options", L"line_ending", std_line_ending);
    
  }
}

///////////////////////////////////////////////////////////////////////////////
/// Class NewProject
///////////////////////////////////////////////////////////////////////////////

NewProject::NewProject( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxDialog( parent, id, title, pos, size, style )
{
	this->SetSizeHints( wxSize( 300,-1 ), wxDefaultSize );
	
	wxBoxSizer* bSizer11;
	bSizer11 = new wxBoxSizer( wxVERTICAL );
	
	wxBoxSizer* pathSizer;
	pathSizer = new wxBoxSizer( wxHORIZONTAL );
	
	m_PathLabel = new wxStaticText( this, wxID_ANY, wxT("Name"), wxDefaultPosition, wxDefaultSize, 0 );
	m_PathLabel->Wrap( -1 );
	pathSizer->Add( m_PathLabel, 0, wxALL, 5 );
	
	m_pathText = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	pathSizer->Add( m_pathText, 1, wxALL, 5 );
	
	bSizer11->Add( pathSizer, 1, wxEXPAND, 5 );
	
	wxBoxSizer* pathSizer1;
	pathSizer1 = new wxBoxSizer( wxHORIZONTAL );
	
	m_PathLabel1 = new wxStaticText( this, wxID_ANY, wxT("Location"), wxDefaultPosition, wxDefaultSize, 0 );
	m_PathLabel1->Wrap( -1 );
	pathSizer1->Add( m_PathLabel1, 0, wxALL, 5 );
	
	m_pathText1 = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	pathSizer1->Add( m_pathText1, 1, wxTOP|wxBOTTOM, 5 );
	
	m_button1 = new wxButton( this, wxID_ANY, wxT("..."), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
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
