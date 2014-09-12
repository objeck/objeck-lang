#ifndef __UTILS_H__
#define __UTILS_H__

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#include <string>
#include <vector>
#include <map>
#include <fstream>

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/radiobut.h>
#include <wx/spinctrl.h>
#include <wx/combobox.h>
#include <wx/statbox.h>
#include <wx/dialog.h>
#include <wx/artprov.h>
#include <wx/wxhtml.h>
#include <wx/utils.h>
#include <wx/process.h>

#include "../../src/shared/sys.h"
#include "defsext.h"

using namespace std;

//----------------------------------------------------------------------------
//! IniManager
class IniManager {
  map<const wxString, map<const wxString, wxString>*> section_map;
  wxString filename, input;
  wchar_t cur_char, next_char;
  size_t cur_pos;
  bool locked;
  wxString objeck_path;
  wxString indent_spacing;
  wxString line_ending;
  
  wxString LoadFile(const wxString &fn);
  void NextChar();
  void Clear();
  wxString Serialize();
  void Deserialize();
  
public:
  IniManager(const wxString &fn);
  ~IniManager();

  // basic operations
  static bool WriteFile(const wxString &fn, const wxString &out);
  bool IsLocked() { return locked; }
  wxString GetValue(const wxString &sec, const wxString &key);
  void SetValue(const wxString &sec, const wxString &key, const wxString &value);
  void Load();
  void Save();
  
  // options
  void ShowOptionsDialog(wxWindow* parent);
  
  wxString GetObjeckPath() {
    return GetValue(L"Options", L"objeck_path");
  }

  wxString GetIdentSpacing() {
    return GetValue(L"Options", L"indent_spacing");
  }

  wxString GetLineEnding() {
    return GetValue(L"Options", L"line_ending");
  }
};

//----------------------------------------------------------------------------
//! IniManager
class ProjectManager {
 public:
  ProjectManager(const wxString &name, const wxString &full_name);
  ~ProjectManager();
};

#endif
