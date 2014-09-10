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
  // TODO: add lock to operations
  wstring GetValue(const wstring &sec, const wstring &key);
  void SetValue(const wstring &sec, const wstring &key, wstring &value);
  void Load();
  void Save();
  
  // UI operations
  void ShowOptionsDialog(wxWindow* parent);
  void ShowNewProjectDialog(wxWindow* parent);
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
