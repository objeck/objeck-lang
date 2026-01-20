/***************************************************************************
 * Links pre-compiled code into existing program
 *
 * Copyright (c) 2025, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#include "linker.h"
#include "types.h"
#include "../shared/instrs.h"
#include "../shared/version.h"

using namespace instructions;

/****************************
 * Creates associations with instructions
 * that reference library classes
 ****************************/
void Linker::ResloveExternalClass(LibraryClass* klass)
{
  std::map<const std::wstring, LibraryMethod*> methods = klass->GetMethods();
  for(auto& mthd_pair : methods) {
    std::vector<LibraryInstr*> instrs = mthd_pair.second->GetInstructions();
    for(size_t j = 0; j < instrs.size(); ++j) {
      LibraryInstr* instr = instrs[j];
      // check library call
      switch(instr->GetType()) {
        // test
      case LIB_NEW_OBJ_INST:
      case LIB_OBJ_INST_CAST:
      case LIB_MTHD_CALL: {
        LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
        if(lib_klass) {
          if(!lib_klass->GetCalled()) {
            lib_klass->SetCalled(true);
            ResloveExternalClass(lib_klass);
          }
        } 
        else {
          std::wcerr << L"Error: Unable to resolve external library class: '"
                << instr->GetOperand5() << L"'; check library path" << std::endl;
          exit(1);
        }
      }
        break;

      default:
        break;
      }
    }
  }

}

void Linker::ResloveExternalClasses()
{
  // all libraries
  for(auto& lib_pair : libraries) {
    // all classes
    std::vector<LibraryClass*> classes = lib_pair.second->GetClasses();
    for(size_t i = 0; i < classes.size(); ++i) {
      // all methods
      if(classes[i]->GetCalled()) {
        ResloveExternalClass(classes[i]);
      }
    }
  }
}

void Linker::ResolveExternalMethodCalls()
{
  // all libraries
  for(auto& lib_pair : libraries) {
    // all classes
    std::vector<LibraryClass*> classes = lib_pair.second->GetClasses();
    for(size_t i = 0; i < classes.size(); ++i) {
      // all methods
      std::map<const std::wstring, LibraryMethod*> methods = classes[i]->GetMethods();
      for(auto& mthd_pair : methods) {
        std::vector<LibraryInstr*> instrs = mthd_pair.second->GetInstructions();
        for(size_t j = 0; j < instrs.size(); ++j) {
          LibraryInstr* instr = instrs[j];

          switch(instr->GetType()) {
            // NEW_OBJ_INST
          case LIB_NEW_OBJ_INST: {
            LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
            if(lib_klass) {
              instr->SetType(instructions::NEW_OBJ_INST);
              instr->SetOperand(lib_klass->GetId());
            }
            else {
              std::wcerr << L"Error: Unable to resolve external library class: '"
                << instr->GetOperand5() << L"'; check library path" << std::endl;
              exit(1);
            }
          }
            break;

          case LIB_OBJ_TYPE_OF: {
            LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
            if(lib_klass) {
              instr->SetType(instructions::OBJ_TYPE_OF);
              instr->SetOperand(lib_klass->GetId());
            }
            else {
              std::wcerr << L"Error: Unable to resolve external library class: '"
                << instr->GetOperand5() << L"'; check library path" << std::endl;
              exit(1);
            }
          }
            break;

          case LIB_OBJ_INST_CAST: {
            LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
            if(lib_klass) {
              instr->SetType(instructions::OBJ_INST_CAST);
              instr->SetOperand(lib_klass->GetId());
            }
            else {
              std::wcerr << L"Error: Unable to resolve external library class: '"
                << instr->GetOperand5() << L"'; check library path" << std::endl;
              exit(1);
            }
          }
            break;

            // MTHD_CALL
          case instructions::LIB_MTHD_CALL: {
            LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
            if(lib_klass) {
              LibraryMethod* lib_method = lib_klass->GetMethod(instr->GetOperand6());
              if(lib_method) {
                instr->SetType(instructions::MTHD_CALL);
                instr->SetOperand(lib_klass->GetId());
                instr->SetOperand2(lib_method->GetId());
              }
              else {
                std::wcerr << L"Error: Unable to resolve external library method: '" << instr->GetOperand6() << L"'; check library path" << std::endl;
                exit(1);
              }
            }
            else {
              std::wcerr << L"Error: Unable to resolve external library class: '" << instr->GetOperand5() << L"'; check library path" << std::endl;
              exit(1);
            }
          }
            break;

          case instructions::LIB_FUNC_DEF: {
            LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
            if(lib_klass) {
              LibraryMethod* lib_method = lib_klass->GetMethod(instr->GetOperand6());
              if(lib_method) {
                const int16_t lib_method_id = lib_method->GetId();
                const int16_t lib_cls_id = lib_method->GetLibraryClass()->GetId();
                const int lib_method_cls_id = (lib_cls_id << 16) | lib_method_id;
                instr->SetType(instructions::LOAD_INT_LIT);
                instr->SetOperand7(lib_method_cls_id);
              }
              else {
                std::wcerr << L"Error: Unable to resolve external library method: '" << instr->GetOperand6() << L"'; check library path" << std::endl;
                exit(1);
              }
            }
            else {
              std::wcerr << L"Error: Unable to resolve external library class: '" << instr->GetOperand5() << L"'; check library path" << std::endl;
              exit(1);
            }
          }
            break;

          default:
            break;
          }
        }
      }
    }
  }
}

std::unordered_map<std::wstring, LibraryAlias*> Linker::GetAllAliasesMap()
{
  if(all_aliases_map.empty()) {
    std::vector<LibraryAlias*> aliases = GetAllAliases();
    for(size_t i = 0; i < aliases.size(); ++i) {
      LibraryAlias* klass = aliases[i];
      all_aliases_map[klass->GetName()] = klass;
    }
  }

  return all_aliases_map;
}

// get all libraries
std::vector<Library*> Linker::GetAllUsedLibraries()
{
  std::vector<Library*> used_libraries;

  for(auto& pair : libraries) {
    Library* library = pair.second;
    std::vector<LibraryClass*> classes = library->GetClasses();

    bool add_library = false;
    for(size_t i = 0; !add_library && i < classes.size(); ++i) {
      if(classes[i]->GetCalled()) {
        add_library = true;
      }
    }

    if(add_library) {
      used_libraries.push_back(library);
    }
  }

  return used_libraries;
}

std::vector<LibraryAlias*> Linker::GetAllAliases()
{
  if(all_aliases.empty()) {
    for(auto& pair : libraries) {
      std::vector<LibraryAlias*> aliases = pair.second->GetAliases();
      for(size_t i = 0; i < aliases.size(); ++i) {
        all_aliases.push_back(aliases[i]);
      }
    }
  }

  return all_aliases;
}

std::unordered_map<std::wstring, LibraryClass*> Linker::GetAllClassesMap()
{
  if(all_classes_map.empty()) {
    std::vector<LibraryClass*> klasses = GetAllClasses();
    for(size_t i = 0; i < klasses.size(); ++i) {
      LibraryClass* klass = klasses[i];
      all_classes_map[klass->GetName()] = klass;
    }
  }

  return all_classes_map;
}

std::vector<LibraryClass*> Linker::GetAllClasses()
{
  if(all_classes.empty()) {
    std::map<const std::wstring, Library*>::iterator iter;
    for(iter = libraries.begin(); iter != libraries.end(); ++iter) {
      std::vector<LibraryClass*> classes = iter->second->GetClasses();
      for(size_t i = 0; i < classes.size(); ++i) {
        all_classes.push_back(classes[i]);
      }
    }
  }

  return all_classes;
}

std::unordered_map<std::wstring, LibraryEnum*> Linker::GetAllEnumsMap()
{
  if(all_enums_map.empty()) {
    std::vector<LibraryEnum*> enums = GetAllEnums();
    for(size_t i = 0; i < enums.size(); ++i) {
      LibraryEnum* klass = enums[i];
      all_enums_map[klass->GetName()] = klass;
    }
  }

  return all_enums_map;
}

std::vector<LibraryEnum*> Linker::GetAllEnums()
{
  if(all_enums.empty()) {
    std::map<const std::wstring, Library*>::iterator iter;
    for(iter = libraries.begin(); iter != libraries.end(); ++iter) {
      std::vector<LibraryEnum*> enums = iter->second->GetEnums();
      for(size_t i = 0; i < enums.size(); ++i) {
        all_enums.push_back(enums[i]);
      }
    }
  }

  return all_enums;
}

LibraryAlias* Linker::SearchAliasLibraries(const std::wstring& name, std::vector<std::wstring> uses)
{
  std::unordered_map<std::wstring, LibraryAlias*> alias_map = GetAllAliasesMap();
  LibraryAlias* alias = alias_map[name];
  if(alias) {
    return alias;
  }

  for(size_t i = 0; i < uses.size(); ++i) {
    alias = alias_map[uses[i] + L"." + name];
    if(alias) {
      return alias;
    }
  }

  return nullptr;
}

LibraryClass* Linker::SearchClassLibraries(const std::wstring& name, std::vector<std::wstring> uses)
{
  std::unordered_map<std::wstring, LibraryClass*> klass_map = GetAllClassesMap();
  LibraryClass* klass = klass_map[name];
  if(klass) {
    return klass;
  }

  for(size_t i = 0; i < uses.size(); ++i) {
    klass = klass_map[uses[i] + L"." + name];
    if(klass) {
      return klass;
    }
  }

  return nullptr;
}

bool Linker::HasBundleName(const std::wstring& name)
{
  std::map<const std::wstring, Library*>::iterator iter;
  for(iter = libraries.begin(); iter != libraries.end(); ++iter) {
    if(iter->second->HasBundleName(name)) {
      return true;
    }
  }

  return false;
}

LibraryEnum* Linker::SearchEnumLibraries(const std::wstring& name, std::vector<std::wstring> uses)
{
  std::unordered_map<std::wstring, LibraryEnum*> enum_map = GetAllEnumsMap();
  LibraryEnum* eenum = enum_map[name];
  if(eenum) {
    return eenum;
  }

  for(size_t i = 0; i < uses.size(); ++i) {
    eenum = enum_map[uses[i] + L"." + name];
    if(eenum) {
      return eenum;
    }
  }

  return nullptr;
}

void Linker::Load(bool is_lib)
{
#ifdef _DEBUG
  GetLogger() << L"--------- Linking Libraries ---------" << std::endl;
#endif

  // set library path
  const std::wstring lib_path = GetLibraryPath();
  const std::wstring config_file_path = lib_path + L"configobjk.ini";;
  std::map<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>> lib_aliases = ParseIni(config_file_path);
  
  // parses library path
  if(master_path.size() > 0) {
    size_t offset = 0;
    size_t index = master_path.find(',');
    while(index != std::wstring::npos) {
      // load library
      std::wstring file_ref = master_path.substr(offset, index - offset);
      if(!file_ref.empty()) {
        // check for alias 
        if(!is_lib && file_ref[0] == L'@') {
          if(!lib_aliases.empty()) {
            auto lib_section_aliases = lib_aliases.find(file_ref);
            if(lib_section_aliases != lib_aliases.end()) {
              auto lib_name_value = lib_section_aliases->second;
              for(const auto& derived_lib : lib_name_value) {
                std::wstring file_path = lib_path + derived_lib.first;
                if(!frontend::EndsWith(file_path, L".obl")) {
                  file_path += L".obl";
                }

                Library* library = new Library(file_path);
                library->Load();
                libraries.insert(std::pair<std::wstring, Library*>(file_path, library));
                paths.push_back(std::move(file_path));
              }
            }
            else {
              std::wcerr << L"Unknown library alias: '" << file_ref << L"'.\n\tCheck the alias name and ensure the 'OBJECK_LIB_PATH' environment variable refers to the library directory." << std::endl;
              exit(1);
            }
          }
          else {
            std::wcerr << L"Unknown library alias: '" << file_ref << L"'.\n\tCheck the alias name and ensure the 'OBJECK_LIB_PATH' environment variable refers to the library directory." << std::endl;
            exit(1);
          }
        }
        else {
          std::wstring file_path = lib_path + file_ref;
          if(!frontend::EndsWith(file_path, L".obl")) {
            file_path += L".obl";
          }
          Library* library = new Library(file_path);
          library->Load();
          // insert library
          libraries.insert(std::pair<std::wstring, Library*>(file_path, library));
          std::vector<std::wstring>::iterator found = find(paths.begin(), paths.end(), file_path);
          if(found == paths.end()) {
            paths.push_back(std::move(file_path));
          }
        }

        // update
        offset = index + 1;
        index = master_path.find(',', offset);
      }
      else {
        ++index;
      }
    }
    // insert library
    const std::wstring file_ref = master_path.substr(offset, master_path.size());
    if(!file_ref.empty()) {
      std::wstring file_path = lib_path + file_ref;
      
      const bool is_alias = file_ref[0] == L'@';
      if(is_alias && !lib_aliases.empty()) {
        auto lib_section_aliases = lib_aliases.find(file_ref);
        if(lib_section_aliases != lib_aliases.end()) {
          auto lib_name_value = lib_section_aliases->second;
          for(const auto& derived_lib : lib_name_value) {
            std::wstring file_path = lib_path + derived_lib.first;
            if(!frontend::EndsWith(file_path, L".obl")) {
              file_path += L".obl";
            }

            Library* library = new Library(file_path);
            library->Load();
            libraries.insert(std::pair<std::wstring, Library*>(file_path, library));
            paths.push_back(std::move(file_path));
          }
        }
        else {
          std::wcerr << L"Unknown library alias: '" << file_ref << L"'.\n\tCheck the alias name and ensure the 'OBJECK_LIB_PATH' environment variable refers to the library directory." << std::endl;
          exit(1);
        }
      }
      else {
        if(!frontend::EndsWith(file_path, L".obl")) {
          file_path += L".obl";
        }

        Library* library = new Library(file_path);
        library->Load();
        libraries.insert(std::pair<std::wstring, Library*>(file_path, library));
        paths.push_back(file_path);
      }
#ifdef _DEBUG
      GetLogger() << L"--------- End Linking ---------" << std::endl;
#endif
    }
  }
}

std::map<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>> Linker::ParseIni(const std::wstring& filename) {
  std::map<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>> values;

  std::wifstream file_reader(UnicodeToBytes(filename));
  if(file_reader.good()) {
    std::wstring section_title;
    std::vector <std::pair<std::wstring, std::wstring>> section_names_values;

    std::wstring line;
    while(std::getline(file_reader, line)) {
      line = TrimNameValue(line);
      
      if(!line.empty() && line[0] != '#') {
        if(line.front() == L'[' && line.back() == L']') {
          if(!section_names_values.empty()) {
            values[section_title] = section_names_values;
            section_names_values.clear();
          }

          section_title = line.substr(1, line.size() - 2);
          section_title = TrimNameValue(section_title);
        }
        else {
          const size_t index = line.find(L'=');
          if(index != std::wstring::npos) {
            std::wstring name = line.substr(0, index);
            name = TrimNameValue(name);

            std::wstring value = line.substr(index + 1, line.size() - index - 1);
            value = TrimNameValue(value);

            std::pair<std::wstring, std::wstring> name_value(name, value);
            section_names_values.push_back(std::move(name_value));
          }
          else {
            std::wstring name = TrimNameValue(line);
            name = TrimNameValue(name);

            std::pair<std::wstring, std::wstring> name_value(name, std::wstring());
            section_names_values.push_back(std::move(name_value));
          }
        }
      }
    }

    if(!section_names_values.empty()) {
      values[section_title] = std::move(section_names_values);
    }
  }
  file_reader.close();

  return values;
}

const std::wstring Linker::TrimNameValue(const std::wstring& name_value) {
  std::wstring timed_name_value;

  if(!name_value.empty()) {
    bool done = false;

    size_t index = 0;
    do {
      const wchar_t name_value_char = name_value.at(index++);
      if(!std::isspace(name_value_char)) {
        done = true;
      }
    } while(!done);
    size_t name_value_start = index - 1;

    done = false;
    index = name_value.size();
    do {
      const wchar_t name_value_char = name_value.at(--index);
      if(!std::isspace(name_value_char)) {
        ++index;
        done = true;
      }
    } while(!done);

    return name_value.substr(name_value_start, index - name_value_start);
  }

  return L"";
}


/****************************
 * LibraryClass class
 ****************************/
LibraryClass::LibraryClass(const std::wstring& n, const std::wstring& p, const std::vector<std::wstring> i, bool is, bool ip, const std::vector<std::wstring> g, 
                           bool v, const int cs, const int in, backend::IntermediateDeclarations* ce, backend::IntermediateDeclarations* ie, 
                           std::map<std::wstring, backend::IntermediateDeclarations*> le, Library* l, const std::wstring& fn, bool d)
{
  id = -1;
  name = n;
  parent_name = p;
  interface_names = i;
  generic_name_types = g;
  is_interface = is;
  is_public = ip;
  is_virtual = v;
  cls_space = cs;
  inst_space = in;
  cls_entries = ce;
  inst_entries = ie;
  lib_closure_entries = le;
  library = l;
  is_generic = false;
  generic_interface = nullptr;

  for(size_t i = 0; i < generic_name_types.size(); ++i) {
    const std::wstring generic_name_type = generic_name_types[i];
    size_t end = generic_name_type.find(L'|');
    if(end != std::wstring::npos) {
      const std::wstring generic_name = generic_name_type.substr(0, end);
      std::wstring concrete_name;
      end++;
      if(end < generic_name_type.size()) {
        concrete_name = generic_name_type.substr(end, generic_name_type.size() - end);
      }

      generic_classes.push_back(new LibraryClass(generic_name, concrete_name));
    }
  }

  // force runtime linking of these classes
  if(name == L"System.Introspection.Class" ||
     name == L"System.Introspection.Method" ||
     name == L"System.Introspection.DataType") {
    was_called = true;
  }
  else {
    was_called = false;
  }
  is_debug = d;
  file_name = fn;
}

/****************************
 * Returns all children related
 * to this class.
 ****************************/
std::vector<LibraryClass*> LibraryClass::GetLibraryChildren()
{
  if(!lib_children.size()) {
    std::map<const std::wstring, const std::wstring> hierarchies = library->GetHierarchies();
    for(auto& pair : hierarchies) {
      if(pair.second == name) {
        lib_children.push_back(library->GetClass(pair.first));
      }
    }
  }

  return lib_children;
}

std::map<backend::IntermediateDeclarations*, std::pair<std::wstring, int>> LibraryClass::CopyClosureEntries()
{
  std::map<backend::IntermediateDeclarations*, std::pair<std::wstring, int> > closure_entries;

  for(auto& lambda_pair : lib_closure_entries) {
    const std::wstring lib_mthd_name = lambda_pair.first;
    LibraryMethod* lib_method = GetMethod(lib_mthd_name);
    if(!lib_method) {
      std::wcerr << L"Internal compiler error: Invalid method name." << std::endl;
      exit(1);
    }
    backend::IntermediateDeclarations* dclr = lambda_pair.second;
    closure_entries[dclr] = std::pair<std::wstring, int>(lib_mthd_name, lib_method->GetId());
  }

  return closure_entries;
}

std::vector<LibraryMethod*> LibraryClass::GetUnqualifiedMethods(const std::wstring& n)
{
  std::vector<LibraryMethod*> results;
  std::pair<std::multimap<const std::wstring, LibraryMethod*>::iterator,
  std::multimap<const std::wstring, LibraryMethod*>::iterator> result;
  result = unqualified_methods.equal_range(n);
  std::multimap<const std::wstring, LibraryMethod*>::iterator iter = result.first;
  for(iter = result.first; iter != result.second; ++iter) {
    results.push_back(iter->second);
  }

  return results;
}

void LibraryClass::AddMethod(LibraryMethod* method)
{
  const std::wstring& encoded_name = method->GetName();
  methods.insert(std::pair<const std::wstring, LibraryMethod*>(encoded_name, method));

  // add to unqualified names to list
  const size_t start = encoded_name.find(':');
  if(start != std::wstring::npos) {
    const size_t end = encoded_name.find(':', start + 1);
    if(end != std::wstring::npos) {
      const std::wstring& unqualified_name = encoded_name.substr(start + 1, end - start - 1);
      unqualified_methods.insert(std::pair<std::wstring, LibraryMethod*>(unqualified_name, method));
    }
    else {
      delete method;
      method = nullptr;
    }
  }
  else {
    delete method;
    method = nullptr;
  }
}

/****************************
 * Loads library file
 ****************************/
void Library::Load()
{
#ifdef _DEBUG
  GetLogger() << L"=== Loading file: '" << lib_path << L"' ===" << std::endl;
#endif
  LoadFile(lib_path);
}

char* Library::LoadFileBuffer(std::wstring filename, size_t& buffer_size)
{
  char* buffer = nullptr;
  // open file
  const std::string open_filename = UnicodeToBytes(filename);
  std::ifstream in(open_filename.c_str(), std::ifstream::binary);
  if(in.good()) {
    // get file size
    in.seekg(0, std::ios::end);
    buffer_size = (size_t)in.tellg();
    in.seekg(0, std::ios::beg);
    buffer = static_cast<char*>(calloc(buffer_size + 1, sizeof(char)));
    in.read(buffer, buffer_size);
    // close file
    in.close();

    unsigned long dest_len;
    char* out = OutputStream::UncompressZlib(buffer, static_cast<unsigned long>(buffer_size), dest_len);
    if(!out) {
      std::wcerr << L"Unable to uncompress file: " << filename << std::endl;
      exit(1);
    }
#ifdef _DEBUG
    GetLogger() << L"--- file in: compressed=" << buffer_size << L", uncompressed=" << dest_len << L" ---" << std::endl;
#endif

    free(buffer);
    buffer = nullptr;
    return out;
  }
  else {
    if(frontend::EndsWith(filename, L".obl")) {
      std::wcerr << L"Unable to read library: '" << filename << L"'" << std::endl;
    }
    else if(frontend::EndsWith(filename, L".obl")) {
      std::wcerr << L"Unable to read source: '" << filename << L"'" << std::endl;
    }
    else {
      std::wcerr << L"Unable to open file: '" << filename << L"'" << std::endl;
    }
  }

  return nullptr;
}

/****************************
 * Reads a file
 ****************************/
void Library::LoadFile(const std::wstring &file_name)
{
  // read file into memory
  ReadFile(file_name);
  if(buffer) {
    const int ver_num = ReadInt();
    if(ver_num != VER_NUM) {
      std::wcerr << L"The " << lib_path << L" library appears to be compiled with a different version of the tool chain.\n\tPlease recompile the libraries or link the correct version." << std::endl;
      exit(1);
    }

    const int magic_num = ReadInt();
    if(magic_num == MAGIC_NUM_EXE) {
      std::wcerr << L"Unable to use executable '" << file_name << L"' as linked library." << std::endl;
      exit(1);
    }
    else if(magic_num != MAGIC_NUM_LIB) {
      std::wcerr << L"Unable to link invalid library file '" << file_name << L"'.\n\tCheck the alias name and ensure the 'OBJECK_LIB_PATH' environment variable refers to the library directory." << std::endl;
      exit(1);
    }

    // read float strings
    const int num_float_strings = ReadInt();
    for(int i = 0; i < num_float_strings; ++i) {
      frontend::FloatStringHolder* holder = new frontend::FloatStringHolder;
      holder->length = ReadInt();
      holder->value = new FLOAT_VALUE[holder->length];
      for(int j = 0; j < holder->length; ++j) {
        holder->value[j] = ReadDouble();
      }
#ifdef _DEBUG
      GetLogger() << L"float string id=" << i << L"; value=";
      for(int j = 0; j < holder->length; ++j) {
        GetLogger() << holder->value[j] << L",";
      }
      GetLogger() << std::endl;
#endif
      FloatStringInstruction* str_instr = new FloatStringInstruction;
      str_instr->value = holder;
      float_strings.push_back(str_instr);
    }
    // read bool strings
    const int num_bool_strings = ReadInt();
    for(int i = 0; i < num_bool_strings; ++i) {
      frontend::BoolStringHolder* holder = new frontend::BoolStringHolder;
      holder->length = ReadInt();
      holder->value = new bool[holder->length];
      for(int j = 0; j < holder->length; ++j) {
        holder->value[j] = ReadByte();
      }
#ifdef _DEBUG
      GetLogger() << L"bool string id=" << i << L"; value=";
      for(int j = 0; j < holder->length; ++j) {
        GetLogger() << holder->value[j] << L",";
      }
      GetLogger() << std::endl;
#endif
      BoolStringInstruction* str_instr = new BoolStringInstruction;
      str_instr->value = holder;
      bool_strings.push_back(str_instr);
    }
    // read byte strings
    const int num_byte_strings = ReadInt();
    for(int i = 0; i < num_byte_strings; ++i) {
      frontend::ByteStringHolder* holder = new frontend::ByteStringHolder;
      holder->length = ReadInt();
      holder->value = new char[holder->length];
      for(int j = 0; j < holder->length; ++j) {
        holder->value[j] = ReadByte();
      }
#ifdef _DEBUG
      GetLogger() << L"byte string id=" << i << L"; value=";
      for(int j = 0; j < holder->length; ++j) {
        GetLogger() << holder->value[j] << L",";
      }
      GetLogger() << std::endl;
#endif
      ByteStringInstruction* str_instr = new ByteStringInstruction;
      str_instr->value = holder;
      byte_strings.push_back(str_instr);
    }
    // read int strings
    const int num_int_strings = ReadInt();
    for(int i = 0; i < num_int_strings; ++i) {
      frontend::IntStringHolder* holder = new frontend::IntStringHolder;
      holder->length = ReadInt();
      holder->value = new INT64_VALUE[holder->length];
      for(int j = 0; j < holder->length; ++j) {
        holder->value[j] = ReadInt64();
      }
#ifdef _DEBUG
      GetLogger() << L"int string id=" << i << L"; value=";
      for(int j = 0; j < holder->length; ++j) {
        GetLogger() << holder->value[j] << L",";
      }
      GetLogger() << std::endl;
#endif
      IntStringInstruction* str_instr = new IntStringInstruction;
      str_instr->value = holder;
      int_strings.push_back(str_instr);
    }
    // read char strings
    const int num_char_strings = ReadInt();
    for(int i = 0; i < num_char_strings; ++i) {
      const std::wstring& char_str_value = ReadString();
#ifdef _DEBUG
      const std::wstring& msg = L"char string id=" + Linker::ToString(i) + L"; value='" + char_str_value + L"'";
      Linker::Debug(msg, -1, 0);
#endif
      CharStringInstruction* str_instr = new CharStringInstruction;
      str_instr->value = char_str_value;
      char_strings.push_back(str_instr);
    }

    // read bundle names
    const int num_bundle_name = ReadInt();
    for(int i = 0; i < num_bundle_name; ++i) {
      const std::wstring str_value = ReadString();
      bundle_names.push_back(str_value);
#ifdef _DEBUG
      const std::wstring& msg = L"bundle name='" + str_value + L"'";
      Linker::Debug(msg, -1, 0);
#endif
    }

    // load aliases, enums and classes
    LoadLambdas();
    LoadEnums();
    LoadClasses();
  }
}

/****************************
 * Reads aliases
 ****************************/
void Library::LoadLambdas()
{
  // read alias names
  const int num_alias_name = ReadInt();
  for(int i = 0; i < num_alias_name; ++i) {
    const std::wstring str_value = ReadString();
#ifdef _DEBUG
    const std::wstring& msg = L"alias name='" + str_value + L"'";
    Linker::Debug(msg, -1, 0);
#endif

    size_t name_end = str_value.find(L'|');
    if(name_end != std::wstring::npos) {
      const std::wstring name = str_value.substr(0, name_end);
      const std::wstring named_types = str_value.substr(name_end + 1);

      size_t named_type_start = 0;
      size_t named_type_end = named_types.find(L';');

      std::map<std::wstring, frontend::Type*> alias_map;
      while(named_type_end != std::wstring::npos) {
        const std::wstring named_type = named_types.substr(named_type_start, named_type_end);
        size_t named_index = named_type.find(L'|');
        if(named_index != std::wstring::npos) {
          const std::wstring type_name = named_type.substr(0, named_index);
          const std::wstring type_id = named_type.substr(named_index + 1);
          alias_map[type_name] = frontend::TypeParser::ParseType(type_id);;
        }
        // update
        named_type_start = named_type_end + 1;
        named_type_end = named_types.find(L';', named_type_start);
      }

      LibraryAlias* lib_alias = new LibraryAlias(name, alias_map);
      AddAlias(lib_alias);
    }
  }
}

/****************************
 * Reads enums
 ****************************/
void Library::LoadEnums()
{
  const int number = ReadInt();
  for(int i = 0; i < number; ++i) {
    // read enum
    const std::wstring &enum_name = ReadString();
#ifdef _DEBUG
    const std::wstring &msg = L"[enum: name='" + enum_name + L"']";
    Linker::Debug(msg, 0, 1);
#endif
    const INT64_VALUE enum_offset = ReadInt64();
    LibraryEnum* eenum = new LibraryEnum(enum_name, enum_offset);

    // read enum items
    const INT_VALUE num_items = ReadInt();
    for(int i = 0; i < num_items; ++i) {
      const std::wstring &item_name = ReadString();
      const INT64_VALUE item_id = ReadInt64();
      eenum->AddItem(new LibraryEnumItem(item_name, item_id, eenum));
    }
    // add enum
    AddEnum(eenum);
  }
}

/****************************
 * Reads classes
 ****************************/
void Library::LoadClasses()
{
  // we ignore all class ids
  const int number = ReadInt();
  for(int i = 0; i < number; ++i) {
    // id
    ReadInt();
    const std::wstring &name = ReadString();

    // pid
    ReadInt();
    const std::wstring &parent_name = ReadString();

    // read interface ids
    const int interface_size = ReadInt();
    for(int i = 0; i < interface_size; ++i) {
      ReadInt();
    }

    // read interface names
    std::vector<std::wstring> interface_names;
    const int interface_names_size = ReadInt();
    for(int i = 0; i < interface_names_size; ++i) {
      interface_names.push_back(ReadString());
    }

    const bool is_interface = ReadByte() != 0;
    const bool is_public = ReadByte() != 0;

    // read generic names
    std::vector<std::wstring> generic_names;
    const int generic_names_size = ReadInt();
    for(int i = 0; i < generic_names_size; ++i) {
      generic_names.push_back(ReadString());
    }

    bool is_virtual = ReadByte() != 0;
    bool is_debug = ReadByte() != 0;
    std::wstring file_name;
    if(is_debug) {
      file_name = ReadString();
    }
    const int cls_space = ReadInt();
    const int inst_space = ReadInt();

    // read class and instance entries
    backend::IntermediateDeclarations* cls_entries = LoadEntries(is_debug);
    backend::IntermediateDeclarations* inst_entries = LoadEntries(is_debug);

    // read closure entries
    std::map<std::wstring, backend::IntermediateDeclarations*> closure_entries;
    const int num_lambda_dclrs = ReadInt();
    for(int i = 0; i < num_lambda_dclrs; ++i) {
      const std::wstring lambda_dclrs_name = ReadString();
      backend::IntermediateDeclarations* lambda_entries = LoadEntries(is_debug);
      closure_entries[lambda_dclrs_name] = lambda_entries;
    }

    hierarchies.insert(std::pair<const std::wstring, const std::wstring>(name, parent_name));

#ifdef _DEBUG
    const std::wstring &msg = L"[class: name='" + name + L"'; parent='" + parent_name + 
      L"'; interface=" + Linker::ToString(is_interface) + L"'; is_public=" + Linker::ToString(is_public) +
      L"; virtual=" + Linker::ToString(is_virtual) + L"; class_mem_size=" + Linker::ToString(cls_space) +
      L"; instance_mem_size=" + Linker::ToString(inst_space) + L"; is_debug=" + Linker::ToString(is_debug) + L"]";
    Linker::Debug(msg, 0, 1);
#endif

    LibraryClass* cls = new LibraryClass(name, parent_name, interface_names, is_interface, is_public, generic_names, is_virtual,
                                         cls_space, inst_space, cls_entries, inst_entries, closure_entries, this, file_name, is_debug);
    // load method
    LoadMethods(cls, is_debug);
    // add class
    AddClass(cls);
  }
}

/****************************
 * Reads methods
 ****************************/
void Library::LoadMethods(LibraryClass* cls, bool is_debug)
{
  int number = ReadInt();
  for(int i = 0; i < number; ++i) {
    int id = ReadInt();
    frontend::MethodType type = (frontend::MethodType)ReadInt();
    bool is_virtual = ReadByte() != 0;
    bool has_and_or = ReadByte() != 0;
    bool is_lambda = ReadByte() != 0;
    bool is_native = ReadByte() != 0;
    bool is_static = ReadByte() != 0;
    const std::wstring &name = ReadString();
    const std::wstring &rtrn_name = ReadString();
    int params = ReadInt();
    int mem_size = ReadInt();

    // read type parameters
    backend::IntermediateDeclarations* entries = new backend::IntermediateDeclarations;
    int num_params = ReadInt();
    for(int i = 0; i < num_params; ++i) {
      instructions::ParamType type = (instructions::ParamType)ReadInt();
      std::wstring var_name;
      if(is_debug) {
        var_name = ReadString();
      }
      entries->AddParameter(new backend::IntermediateDeclaration(var_name, type));
    }

#ifdef _DEBUG
    const std::wstring &msg = L"(method: name=" + name + L"; id=" + Linker::ToString(id) + L"; num_params: " +
      Linker::ToString(params) + L"; mem_size=" + Linker::ToString(mem_size) + L"; is_native=" +
      Linker::ToString(is_native) +  L"; is_debug=" + Linker::ToString(is_debug) + L"]";
    Linker::Debug(msg, 0, 2);
#endif

    LibraryMethod* mthd = new LibraryMethod(id, name, rtrn_name, type, is_virtual, has_and_or,
                                            is_native, is_static, is_lambda, params, mem_size, cls, entries);
    // load statements
    LoadStatements(mthd, is_debug);

    // add method
    cls->AddMethod(mthd);
  }
}

/****************************
 * Reads statements
 ****************************/
void Library::LoadStatements(LibraryMethod* method, bool is_debug)
{
  std::vector<LibraryInstr*> instrs;
  
  int line_num = -1;

  const unsigned long num_instrs = ReadUnsigned();
  for(unsigned long i = 0; i < num_instrs; ++i) {
    if(is_debug) {
      line_num = ReadInt();
    }    

    const int type = ReadByte();
    switch(type) {
    case LOAD_INT_LIT:
      instrs.push_back(new LibraryInstr(line_num, LOAD_INT_LIT, ReadInt64()));
      break;
      
    case LOAD_CHAR_LIT:
      instrs.push_back(new LibraryInstr(line_num, LOAD_CHAR_LIT, (int)ReadChar()));
      break;
      
    case SHL_INT:
      instrs.push_back(new LibraryInstr(line_num, SHL_INT));
      break;

    case SHR_INT:
      instrs.push_back(new LibraryInstr(line_num, SHR_INT));
      break;

    case LOAD_INT_VAR: {
      const INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_INT_VAR, id, mem_context));
    }
      break;

    case LOAD_FUNC_VAR: {
      long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_FUNC_VAR, (int)id, mem_context));
    }
      break;

    case LOAD_FLOAT_VAR: {
      const INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_FLOAT_VAR, id, mem_context));
    }
      break;

    case STOR_INT_VAR: {
      const INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_INT_VAR, id, mem_context));
    }
      break;

    case STOR_FUNC_VAR: {
      long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_FUNC_VAR, (int)id, mem_context));
    }
      break;

    case STOR_FLOAT_VAR: {
      const INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_FLOAT_VAR, id, mem_context));
    }
      break;

    case COPY_INT_VAR: {
      const INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, COPY_INT_VAR, id, mem_context));
    }
      break;

    case COPY_FLOAT_VAR: {
      const INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, COPY_FLOAT_VAR, id, mem_context));
    }
      break;

    case NEW_INT_ARY: {
      const int dim = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_INT_ARY, dim));
    }
      break;

    case NEW_FLOAT_ARY: {
      const int dim = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_FLOAT_ARY, dim));
    }
      break;

    case NEW_BYTE_ARY: {
      const int dim = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_BYTE_ARY, dim));

    }
      break;

    case NEW_CHAR_ARY: {
      const int dim = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_CHAR_ARY, dim));
      
    }
      break;
      
    case NEW_OBJ_INST: {
      const int obj_id = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_OBJ_INST, obj_id));
    }
      break;

    case NEW_FUNC_INST: {
      const int mem_size = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_FUNC_INST, mem_size));
    }
      break;

    case JMP: {
      const INT_VALUE label = ReadInt();
      const INT_VALUE cond = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, JMP, label, cond));
    }
      break;

    case LBL: {
      const int id = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LBL, id));
    }
      break;

    case MTHD_CALL: {
      const INT_VALUE cls_id = ReadInt();
      const INT_VALUE mthd_id = ReadInt();
      const INT_VALUE is_native = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, MTHD_CALL, cls_id, mthd_id, is_native));
    }
      break;

    case DYN_MTHD_CALL: {
      const INT_VALUE num_params = ReadInt();
      const INT_VALUE rtrn_type = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, DYN_MTHD_CALL, num_params, rtrn_type));
    }
      break;

    case LIB_OBJ_INST_CAST: {
      const std::wstring &cls_name = ReadString();
#ifdef _DEBUG
      const std::wstring &msg = L"LIB_OBJ_INST_CAST: class=" + cls_name;
      Linker::Debug(msg, 0, 3);
#endif
      instrs.push_back(new LibraryInstr(line_num, LIB_OBJ_INST_CAST, cls_name));
    }
      break;

    case LIB_NEW_OBJ_INST: {
      const std::wstring &cls_name = ReadString();
#ifdef _DEBUG
      const std::wstring &msg = L"LIB_NEW_OBJ_INST: class=" + cls_name;
      Linker::Debug(msg, 0, 3);
#endif
      instrs.push_back(new LibraryInstr(line_num, LIB_NEW_OBJ_INST, cls_name));
    }
      break;
      
    case LIB_OBJ_TYPE_OF: {
      const std::wstring &cls_name = ReadString();
#ifdef _DEBUG
      const std::wstring &msg = L"LIB_OBJ_TYPE_OF: class=" + cls_name;
      Linker::Debug(msg, 0, 3);
#endif
      instrs.push_back(new LibraryInstr(line_num, LIB_OBJ_TYPE_OF, cls_name));
    }
      break;
      
    case LIB_MTHD_CALL: {
      const INT_VALUE is_native = ReadInt();
      const std::wstring &cls_name = ReadString();
      const std::wstring &mthd_name = ReadString();
#ifdef _DEBUG
      const std::wstring &msg = L"LIB_MTHD_CALL: class=" + cls_name + L", method=" + mthd_name;
      Linker::Debug(msg, 0, 3);
#endif
      instrs.push_back(new LibraryInstr(line_num, LIB_MTHD_CALL, is_native, cls_name, mthd_name));
    }
      break;

    case LIB_FUNC_DEF: {
      const std::wstring &cls_name = ReadString();
      const std::wstring &mthd_name = ReadString();
#ifdef _DEBUG
      const std::wstring &msg = L"LIB_FUNC_DEF: class=" + cls_name + L", method=" + mthd_name;
      Linker::Debug(msg, 0, 3);
#endif
      instrs.push_back(new LibraryInstr(line_num, LIB_FUNC_DEF, -1, cls_name, mthd_name));
    }
      break;

    case OBJ_INST_CAST: {
      const int to_id = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, OBJ_INST_CAST, to_id));
    }
      break;

    case OBJ_TYPE_OF: {
      const int check_id = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, OBJ_TYPE_OF, check_id));
    }
      break;

    case LOAD_BYTE_ARY_ELM: {
      const INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_BYTE_ARY_ELM, dim, mem_context));
    }
      break;

    case LOAD_CHAR_ARY_ELM: {
      const INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_CHAR_ARY_ELM, dim, mem_context));
    }
      break;

    case LOAD_INT_ARY_ELM: {
      const INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_INT_ARY_ELM, dim, mem_context));
    }
      break;

    case LOAD_FLOAT_ARY_ELM: {
      const INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_FLOAT_ARY_ELM, dim, mem_context));
    }
      break;

    case STOR_BYTE_ARY_ELM: {
      const INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_BYTE_ARY_ELM, dim, mem_context));
    }
      break;

    case STOR_CHAR_ARY_ELM: {
      const INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_CHAR_ARY_ELM, dim, mem_context));
    }
      break;
      
    case STOR_INT_ARY_ELM: {
      const INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_INT_ARY_ELM, dim, mem_context));
    }
      break;

    case STOR_FLOAT_ARY_ELM: {
      const INT_VALUE dim = ReadInt();
      const INT_VALUE mem_context = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_FLOAT_ARY_ELM, dim, mem_context));
    }
      break;

    case RTRN:
      instrs.push_back(new LibraryInstr(line_num, RTRN));
      break;

    case TRAP:
      instrs.push_back(new LibraryInstr(line_num, TRAP, ReadInt()));
      break;

    case TRAP_RTRN: {
      const INT64_VALUE id = instrs.back()->GetOperand7();
      if(id == instructions::CPY_CHAR_STR_ARY) {
        LibraryInstr* cpy_instr = instrs[instrs.size() - 2];
        CharStringInstruction* str_instr = char_strings[cpy_instr->GetOperand7()];
        str_instr->instrs.push_back(cpy_instr);
      }
      else if(id == instructions::CPY_INT_STR_ARY) {
        LibraryInstr* cpy_instr = instrs[instrs.size() - 2];
        IntStringInstruction* str_instr = int_strings[cpy_instr->GetOperand7()];
        str_instr->instrs.push_back(cpy_instr);
      }
      else if(id == instructions::CPY_BOOL_STR_ARY) {
        LibraryInstr* cpy_instr = instrs[instrs.size() - 2];
        BoolStringInstruction* str_instr = bool_strings[cpy_instr->GetOperand7()];
        str_instr->instrs.push_back(cpy_instr);
      }
      else if(id == instructions::CPY_BYTE_STR_ARY) {
        LibraryInstr* cpy_instr = instrs[instrs.size() - 2];
        ByteStringInstruction* str_instr = byte_strings[cpy_instr->GetOperand7()];
        str_instr->instrs.push_back(cpy_instr);
      }
      else if(id == instructions::CPY_FLOAT_STR_ARY) {
        LibraryInstr* cpy_instr = instrs[instrs.size() - 2];
        FloatStringInstruction* str_instr = float_strings[cpy_instr->GetOperand7()];
        str_instr->instrs.push_back(cpy_instr);
      }
      instrs.push_back(new LibraryInstr(line_num, TRAP_RTRN, ReadInt()));
      break;
    }

    case SWAP_INT:
      instrs.push_back(new LibraryInstr(line_num, SWAP_INT));
      break;

    case POP_INT:
      instrs.push_back(new LibraryInstr(line_num, POP_INT));
      break;

    case POP_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, POP_FLOAT));
      break;

    case AND_INT:
      instrs.push_back(new LibraryInstr(line_num, AND_INT));
      break;

    case OR_INT:
      instrs.push_back(new LibraryInstr(line_num, OR_INT));
      break;

    case ADD_INT:
      instrs.push_back(new LibraryInstr(line_num, ADD_INT));
      break;

    case FLOR_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, FLOR_FLOAT));
      break;

    case CPY_BYTE_ARY:
      instrs.push_back(new LibraryInstr(line_num, CPY_BYTE_ARY));
      break;
      
    case LOAD_ARY_SIZE:
      instrs.push_back(new LibraryInstr(line_num, LOAD_ARY_SIZE));
      break;
      
    case CPY_CHAR_ARY:
      instrs.push_back(new LibraryInstr(line_num, CPY_CHAR_ARY));
      break;
      
    case CPY_INT_ARY:
      instrs.push_back(new LibraryInstr(line_num, CPY_INT_ARY));
      break;

    case CPY_FLOAT_ARY:
      instrs.push_back(new LibraryInstr(line_num, CPY_FLOAT_ARY));
      break;

    case ZERO_BYTE_ARY:
      instrs.push_back(new LibraryInstr(line_num, ZERO_BYTE_ARY));
      break;

    case ZERO_CHAR_ARY:
      instrs.push_back(new LibraryInstr(line_num, ZERO_CHAR_ARY));
      break;

    case ZERO_INT_ARY:
      instrs.push_back(new LibraryInstr(line_num, ZERO_INT_ARY));
      break;

    case ZERO_FLOAT_ARY:
      instrs.push_back(new LibraryInstr(line_num, ZERO_FLOAT_ARY));
      break;

    case CEIL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, CEIL_FLOAT));
      break;

    case TRUNC_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, TRUNC_FLOAT));
      break;

    case SIN_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, SIN_FLOAT));
      break;

    case COS_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, COS_FLOAT));
      break;

    case TAN_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, TAN_FLOAT));
      break;

    case ASIN_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ASIN_FLOAT));
      break;

    case ACOS_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ACOS_FLOAT));
      break;

    case ATAN_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ATAN_FLOAT));
      break;

    case LOG2_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, LOG2_FLOAT));
      break;

    case CBRT_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, CBRT_FLOAT));
      break;
      
    case ATAN2_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ATAN2_FLOAT));
      break;

    case ACOSH_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ACOSH_FLOAT));
      break;

    case ASINH_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ASINH_FLOAT));
      break;

    case ATANH_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ATANH_FLOAT));
      break;

    case COSH_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, COSH_FLOAT));
      break;

    case SINH_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, SINH_FLOAT));
      break;

    case TANH_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, TANH_FLOAT));
      break;

    case MOD_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, MOD_FLOAT));
      break;
      
    case ROUND_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ROUND_FLOAT));
      break;

    case EXP_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, EXP_FLOAT));
      break;

    case LOG_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, LOG_FLOAT));
      break;

    case LOG10_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, LOG10_FLOAT));
      break;

    case POW_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, POW_FLOAT));
      break;

    case SQRT_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, SQRT_FLOAT));
      break;

    case GAMMA_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, GAMMA_FLOAT));
      break;

    case NAN_INT:
      instrs.push_back(new LibraryInstr(line_num, NAN_INT));
      break;

    case INF_INT:
      instrs.push_back(new LibraryInstr(line_num, INF_INT));
      break;

    case NEG_INF_INT:
      instrs.push_back(new LibraryInstr(line_num, NEG_INF_INT));
      break;

    case NAN_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, NAN_FLOAT));
      break;

    case INF_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, INF_FLOAT));
      break;

    case NEG_INF_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, NEG_INF_FLOAT));
      break;

    case RAND_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, RAND_FLOAT));
      break;

    case F2I:
      instrs.push_back(new LibraryInstr(line_num, F2I));
      break;

    case I2F:
      instrs.push_back(new LibraryInstr(line_num, I2F));
      break;

    case S2I:
      instrs.push_back(new LibraryInstr(line_num, S2I));
      break;

    case S2F:
      instrs.push_back(new LibraryInstr(line_num, S2F));
      break;
      
    case I2S:
      instrs.push_back(new LibraryInstr(line_num, I2S));
      break;

    case F2S:
      instrs.push_back(new LibraryInstr(line_num, F2S)); 
      break;
      
    case LOAD_CLS_MEM:
      instrs.push_back(new LibraryInstr(line_num, LOAD_CLS_MEM));
      break;

    case LOAD_INST_MEM:
      instrs.push_back(new LibraryInstr(line_num, LOAD_INST_MEM));
      break;

    case SUB_INT:
      instrs.push_back(new LibraryInstr(line_num, SUB_INT));
      break;

    case MUL_INT:
      instrs.push_back(new LibraryInstr(line_num, MUL_INT));
      break;

    case DIV_INT:
      instrs.push_back(new LibraryInstr(line_num, DIV_INT));
      break;

    case MOD_INT:
      instrs.push_back(new LibraryInstr(line_num, MOD_INT));
      break;

    case BIT_AND_INT:
      instrs.push_back(new LibraryInstr(line_num, BIT_AND_INT));
      break;

    case BIT_OR_INT:
      instrs.push_back(new LibraryInstr(line_num, BIT_OR_INT));
      break;

    case BIT_NOT_INT:
      instrs.push_back(new LibraryInstr(line_num, BIT_NOT_INT));
      break;

    case BIT_XOR_INT:
      instrs.push_back(new LibraryInstr(line_num, BIT_XOR_INT));
      break;

    case EQL_INT:
      instrs.push_back(new LibraryInstr(line_num, EQL_INT));
      break;

    case NEQL_INT:
      instrs.push_back(new LibraryInstr(line_num, NEQL_INT));
      break;

    case LES_INT:
      instrs.push_back(new LibraryInstr(line_num, LES_INT));
      break;

    case GTR_INT:
      instrs.push_back(new LibraryInstr(line_num, GTR_INT));
      break;

    case LES_EQL_INT:
      instrs.push_back(new LibraryInstr(line_num, LES_EQL_INT));
      break;

    case GTR_EQL_INT:
      instrs.push_back(new LibraryInstr(line_num, GTR_EQL_INT));
      break;

    case ADD_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ADD_FLOAT));
      break;

    case SUB_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, SUB_FLOAT));
      break;

    case MUL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, MUL_FLOAT));
      break;

    case DIV_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, DIV_FLOAT));
      break;

    case EQL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, EQL_FLOAT));
      break;

    case NEQL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, NEQL_FLOAT));
      break;

    case LES_EQL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, LES_EQL_FLOAT));
      break;

    case GTR_EQL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, GTR_EQL_FLOAT));
      break;

    case LES_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, LES_FLOAT));
      break;

    case GTR_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, GTR_FLOAT));
      break;

    case LOAD_FLOAT_LIT:
      instrs.push_back(new LibraryInstr(line_num, LOAD_FLOAT_LIT, ReadDouble()));
      break;

    case ASYNC_MTHD_CALL: {
      const INT_VALUE cls_id = ReadInt();
      const INT_VALUE mthd_id = ReadInt();
      const INT_VALUE is_native = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, ASYNC_MTHD_CALL, cls_id, mthd_id, is_native));
    }
      break;

    case EXT_LIB_LOAD:
      instrs.push_back(new LibraryInstr(line_num, EXT_LIB_LOAD));
      break;

    case EXT_LIB_UNLOAD:
      instrs.push_back(new LibraryInstr(line_num, EXT_LIB_UNLOAD));
      break;

    case EXT_LIB_FUNC_CALL:
      instrs.push_back(new LibraryInstr(line_num, EXT_LIB_FUNC_CALL));
      break;

    case THREAD_JOIN:
      instrs.push_back(new LibraryInstr(line_num, THREAD_JOIN));
      break;

    case THREAD_SLEEP:
      instrs.push_back(new LibraryInstr(line_num, THREAD_SLEEP));
      break;

    case THREAD_MUTEX:
      instrs.push_back(new LibraryInstr(line_num, THREAD_MUTEX));
      break;

    case CRITICAL_START:
      instrs.push_back(new LibraryInstr(line_num, CRITICAL_START));
      break;

    case CRITICAL_END:
      instrs.push_back(new LibraryInstr(line_num, CRITICAL_END));
      break;

    default: {
#ifdef _DEBUG
      InstructionType instr = (InstructionType)type;
      std::wcerr << L">>> unknown instruction: " << instr << L" <<<" << std::endl;
#endif
      exit(1);
    }
      break;
    }    
  }
  method->AddInstructions(instrs);
}

/******************************
 * LibraryMethod class
 ****************************/
void LibraryMethod::ParseDeclarations()
{
  const std::wstring method_name = name;
  size_t start = method_name.rfind(':');
  if(start != std::wstring::npos) {
    const std::wstring parameters = method_name.substr(start + 1);
    declarations = frontend::TypeParser::ParseParameters(parameters);
  }
}

std::wstring LibraryMethod::EncodeUserType(frontend::Type* type)
{
  std::wstring name;
  if(type) {
    // type
    switch(type->GetType()) {
    case frontend::BOOLEAN_TYPE:
      name = L"Bool";
      break;

    case frontend::BYTE_TYPE:
      name = L"Byte";
      break;

    case frontend::INT_TYPE:
      name = L"Int";
      break;

    case frontend::FLOAT_TYPE:
      name = L"Float";
      break;

    case frontend::CHAR_TYPE:
      name = L"Char";
      break;

    case frontend::NIL_TYPE:
      name = L"Nil";
      break;

    case frontend::VAR_TYPE:
      name = L"Var";
      break;

    case frontend::CLASS_TYPE:
      name = type->GetName();
      if(type->HasGenerics()) {
        const std::vector<frontend::Type*> generic_types = type->GetGenerics();
        name += L'<';
        for(size_t i = 0; i < generic_types.size(); ++i) {
          frontend::Type* generic_type = generic_types[i];
          name += generic_type->GetName();
          if(i + 1 < generic_types.size()) {
            name += L',';
          }
        }
        name += L'>';
      }
      break;

    case frontend::FUNC_TYPE: {
      name = L'(';
      std::vector<frontend::Type*> func_params = type->GetFunctionParameters();
      for(size_t i = 0; i < func_params.size(); ++i) {
        name += EncodeUserType(func_params[i]);
        if(i + 1 < func_params.size()) {
          name += L", ";
        }
      }
      name += L") ~ ";
      name += EncodeUserType(type->GetFunctionReturn());
    }
      break;
        
    case frontend::ALIAS_TYPE:
      break;
    }

    // dimension
    for(int i = 0; i < type->GetDimension(); ++i) {
      name += L"[]";
    }
  }

  return name;
}

void LibraryMethod::EncodeUserName()
{
  bool is_new_private = false;
  if(is_static) {
    user_name = L"function : ";
  }
  else {
    switch(type) {
    case frontend::NEW_PUBLIC_METHOD:
      break;

    case frontend::NEW_PRIVATE_METHOD:
      is_new_private = true;
      break;

    case frontend::PUBLIC_METHOD:
      user_name = L"method : public : ";
      break;

    case frontend::PRIVATE_METHOD:
      user_name = L"method : private : ";
      break;
    }
  }

  if(is_native) {
    user_name += L"native : ";
  }

  // name
  std::wstring method_name = name.substr(0, name.rfind(':'));
  user_name += ReplaceSubstring(method_name, L":", L"->");

  // private new
  if(is_new_private) {
    user_name += L" : private ";
  }

  // params
  user_name += L'(';

  for(size_t i = 0; i < declarations.size(); ++i) {
    user_name += EncodeUserType(declarations[i]);
    if(i + 1 < declarations.size()) {
      user_name += L", ";
    }
  }
  user_name += L") ~ ";

  user_name += EncodeUserType(rtrn_type);
}
