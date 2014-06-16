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

MyFrame::MyFrame(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : 
wxFrame(parent, id, title, pos, size, style) {
  aui_manager.SetManagedWindow(this);

  aui_manager.AddPane(CreateTreeCtrl(), wxAuiPaneInfo().Left());
  aui_manager.AddPane(CreateTextCtrl(wxT("This pane will prompt the user before hiding.")), wxAuiPaneInfo().Centre());
  aui_manager.AddPane(CreateTextCtrl(wxT("blah")), wxAuiPaneInfo().Bottom());

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