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
  map<const wstring, map<const wstring, wstring>*> section_map;
  wstring filename, input;
  wchar_t cur_char, next_char;
  size_t cur_pos;
  bool locked;
  wstring objeck_path;
  wstring indent_spacing;
  wstring line_ending;
  
  wstring LoadFile(const wstring &fn);
  bool WriteFile(const wstring &fn, const wstring &out);
  void NextChar();
  void Clear();
  wstring Serialize();
  void Deserialize();
  
public:
  IniManager(const wstring &fn);
  ~IniManager();

  // basic operations
  bool IsLocked() { return locked; }
  wstring GetValue(const wstring &sec, const wstring &key);
  void SetValue(const wstring &sec, const wstring &key, const wstring &value);
  void Load();
  void Save();
  

  // options
  void ShowOptionsDialog(wxWindow* parent);
  
  wstring GetObjeckPath() {
    return GetValue(L"Options", L"objeck_path");
  }

  wstring GetIdentSpacing() {
    return GetValue(L"Options", L"indent_spacing");
  }

  wstring GetLineEnding() {
    return GetValue(L"Options", L"line_ending");
  }
  
  // project
  void ShowNewProjectDialog(wxWindow* parent);
  
  // opened files
  void AddOpenedFile(const wxString &fn);
};

//----------------------------------------------------------------------------
//! IniManager
class ProjectManager {
 public:
  ProjectManager(const wstring &name, const wstring &fn);
  ~ProjectManager();
};

#endif
