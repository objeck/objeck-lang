#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <map>

// trim from start (in place)
static const std::wstring TrimNameValue(const std::wstring& name_value) {
  std::wstring timed_name_value;

  if(!name_value.empty()) {
    bool done = false;

    size_t index = 0;
    do {
      const wchar_t name_value_char = name_value.at(index++);
      if(!std::isspace(name_value_char)) {
        done = true;
      }
    }
    while(!done);
    size_t name_value_start = index - 1;

    done = false;
    index = name_value.size();
    do {
      const wchar_t name_value_char = name_value.at(--index);
      if(!std::isspace(name_value_char)) {
        ++index;
        done = true;
      }
    } 
    while(!done);
    
    return name_value.substr(name_value_start, index - name_value_start);
  }

  return L"";
}

static std::map<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>> ParseIni(const std::wstring& filename) {
  std::map<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>> values;

  std::wifstream file_reader(filename);
  if(file_reader.good()) {
    std::wstring section_title;
    std::vector <std::pair<std::wstring, std::wstring>> section_names_values;

    std::wstring line;
    while(std::getline(file_reader, line)) {
      if(!line.empty()) {
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
            section_names_values.push_back(name_value);
          }
          else {
            std::wstring name = TrimNameValue(line);
            name = TrimNameValue(name);

            std::pair<std::wstring, std::wstring> name_value(name, std::wstring());
            section_names_values.push_back(name_value);
          }
        }
      }
    }

    if(!section_names_values.empty()) {
      values[section_title] = section_names_values;
    }
  }
  file_reader.close();
  
  return values;
}

int main() {
  ParseIni(L"../test.ini");
  return 0;
}