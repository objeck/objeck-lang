#include "ide.h"

bool MyApp::OnInit() {
  if (!wxApp::OnInit()) {
    return false;
  }

  wxFrame* frame = new MyFrame(NULL, wxID_ANY, wxT("wxAUI Sample Application"), wxDefaultPosition, wxSize(800, 600));
  frame->Show();

  return true;
}

DECLARE_APP(MyApp)
IMPLEMENT_APP(MyApp)

MyFrame::MyFrame(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxFrame(parent, id, title, pos, size, style) {
  // TODO: ulgy put into init method calls
  aui_manager.SetManagedWindow(this);

  aui_manager.AddPane(CreateTreeCtrl(), wxAuiPaneInfo().Left());
  aui_manager.AddPane(CreateTextCtrl(wxT("This pane will prompt the user before hiding.")), wxAuiPaneInfo().Centre());
  aui_manager.AddPane(CreateTextCtrl(wxT("blah")), wxAuiPaneInfo().Bottom());

  wxMenuBar* menu_bar = new wxMenuBar;
  menu_bar->Append(new wxMenu, wxT("&File"));
  menu_bar->Append(new wxMenu, wxT("&View"));
  menu_bar->Append(new wxMenu, wxT("&Perspectives"));
  menu_bar->Append(new wxMenu, wxT("&Options"));
  menu_bar->Append(new wxMenu, wxT("&Notebook"));
  menu_bar->Append(new wxMenu, wxT("&Help"));

  SetMenuBar(menu_bar);
  CreateStatusBar();
  GetStatusBar()->SetStatusText(_("Ready"));

  SetMinSize(wxSize(400, 300));
  
  wxAuiToolBarItemArray prepend_items;
  wxAuiToolBarItemArray append_items;
  wxAuiToolBar* tb3 = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
    wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_OVERFLOW);
  tb3->SetToolBitmapSize(wxSize(16, 16));
  wxBitmap tb3_bmp1 = wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, wxSize(16, 16));
  tb3->AddTool(ID_SampleItem + 16, wxT("Check 1"), tb3_bmp1, wxT("Check 1"), wxITEM_CHECK);
  tb3->AddTool(ID_SampleItem + 17, wxT("Check 2"), tb3_bmp1, wxT("Check 2"), wxITEM_CHECK);
  tb3->AddTool(ID_SampleItem + 18, wxT("Check 3"), tb3_bmp1, wxT("Check 3"), wxITEM_CHECK);
  tb3->AddTool(ID_SampleItem + 19, wxT("Check 4"), tb3_bmp1, wxT("Check 4"), wxITEM_CHECK);
  tb3->AddSeparator();
  tb3->AddTool(ID_SampleItem + 20, wxT("Radio 1"), tb3_bmp1, wxT("Radio 1"), wxITEM_RADIO);
  tb3->AddTool(ID_SampleItem + 21, wxT("Radio 2"), tb3_bmp1, wxT("Radio 2"), wxITEM_RADIO);
  tb3->AddTool(ID_SampleItem + 22, wxT("Radio 3"), tb3_bmp1, wxT("Radio 3"), wxITEM_RADIO);
  tb3->AddSeparator();
  tb3->AddTool(ID_SampleItem + 23, wxT("Radio 1 (Group 2)"), tb3_bmp1, wxT("Radio 1 (Group 2)"), wxITEM_RADIO);
  tb3->AddTool(ID_SampleItem + 24, wxT("Radio 2 (Group 2)"), tb3_bmp1, wxT("Radio 2 (Group 2)"), wxITEM_RADIO);
  tb3->AddTool(ID_SampleItem + 25, wxT("Radio 3 (Group 2)"), tb3_bmp1, wxT("Radio 3 (Group 2)"), wxITEM_RADIO);
  tb3->SetCustomOverflowItems(prepend_items, append_items);
  tb3->Realize();

  aui_manager.AddPane(tb3, wxAuiPaneInfo().
  Name(wxT("tb3")).Caption(wxT("Toolbar 3")).
  ToolbarPane().Top().Row(1).Position(1));

  aui_manager.Update();
}

MyFrame::~MyFrame() {
  aui_manager.UnInit();
}

void MyFrame::DoUpdate() {
  aui_manager.Update();
}

// Demo tree
wxTreeCtrl* MyFrame::CreateTreeCtrl() {
  wxTreeCtrl* tree = new wxTreeCtrl(this, wxID_ANY,
    wxPoint(0, 0), wxSize(160, 250),
    wxTR_DEFAULT_STYLE | wxNO_BORDER);

  wxImageList* imglist = new wxImageList(16, 16, true, 2);
  imglist->Add(wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, wxSize(16, 16)));
  imglist->Add(wxArtProvider::GetBitmap(wxART_NORMAL_FILE, wxART_OTHER, wxSize(16, 16)));
  tree->AssignImageList(imglist);

  wxTreeItemId root = tree->AddRoot(wxT("wxAUI Project"), 0);
  wxArrayTreeItemIds items;



  items.Add(tree->AppendItem(root, wxT("Item 1"), 0));
  items.Add(tree->AppendItem(root, wxT("Item 2"), 0));
  items.Add(tree->AppendItem(root, wxT("Item 3"), 0));
  items.Add(tree->AppendItem(root, wxT("Item 4"), 0));
  items.Add(tree->AppendItem(root, wxT("Item 5"), 0));


  int i, count;
  for (i = 0, count = items.Count(); i < count; ++i)
  {
    wxTreeItemId id = items.Item(i);
    tree->AppendItem(id, wxT("Subitem 1"), 1);
    tree->AppendItem(id, wxT("Subitem 2"), 1);
    tree->AppendItem(id, wxT("Subitem 3"), 1);
    tree->AppendItem(id, wxT("Subitem 4"), 1);
    tree->AppendItem(id, wxT("Subitem 5"), 1);
  }


  tree->Expand(root);

  return tree;
}

// Demo text
wxTextCtrl* MyFrame::CreateTextCtrl(const wxString& ctrl_text)
{
  static int n = 0;

  wxString text;
  if (!ctrl_text.empty())
    text = ctrl_text;
  else
    text.Printf(wxT("This is text box %d"), ++n);

  return new wxTextCtrl(this, wxID_ANY, text,
    wxPoint(0, 0), wxSize(150, 90),
    wxNO_BORDER | wxTE_MULTILINE);
}