#include <string>
#include <map>
#include <fstream>
#include <iostream>
#include <stdlib.h>

#include "../../src/shared/sys.h"

using namespace std;

/******************************
 * Manages name/value pairs in 
 * an .ini file
 ******************************/
class IniManager {
  map<const wstring, map<const wstring, const wstring>*> section_map;
  wstring filename, input;
  wchar_t cur_char, next_char;
  size_t cur_pos;
  
  /******************************
   * Load file into memory
   ******************************/
  wstring LoadFile(wstring filename) {
    char* buffer;

    string fn(filename.begin(), filename.end());
    ifstream in(fn.c_str(), ios_base::in | ios_base::binary | ios_base::ate);
    if(in.good()) {
      // get file size
      in.seekg(0, ios::end);
      size_t buffer_size = (size_t)in.tellg();
      in.seekg(0, ios::beg);
      buffer = (char*)calloc(buffer_size + 1, sizeof(char));
      in.read(buffer, buffer_size);
      // close file
      in.close();
    }
    else {
      wcerr << L"Unable to read file: " << filename << endl;
      exit(1);
    }  
    wstring out = BytesToUnicode(buffer);
    
    free(buffer);
    return out;
  }
 
  /******************************
   * Write file
   ******************************/
  bool WriteFile(wstring filename, wstring output) {
    string fn(filename.begin(), filename.end());
    ofstream out(fn.c_str(), ios_base::out | ios_base::binary);
    if(out.good()) {
      const string bytes = UnicodeToBytes(output);
      out.write(bytes.c_str(), bytes.size());      
      // close file
      out.close();
      return true;
    }
    else {
      wcerr << L"Unable to write file: " << filename << endl;
      exit(1);
    }  
    
    return false;
  }
 
  /******************************
   * Next parse token
   ******************************/
  void NextChar() {
    if(cur_pos < input.size()) {
      cur_char = input[cur_pos++];
      if(cur_pos < input.size()) {
        next_char = input[cur_pos];
      }
      else {
        next_char = L'\0';
      }
    }
    else {
      cur_char = next_char = L'\0';
    }
  }
  
  /******************************
   * Parses setions and name/value
   * pairs
   ******************************/
  void ParseFile() {
    map<const wstring, const wstring>* value_map = new map<const wstring, const wstring>;
    
    NextChar();    
    while(cur_char != L'\0') {
      // ignore white space
      while(cur_char == L' ' || cur_char == L'\t' || cur_char == L'\n' || cur_char == L'\r') {
        NextChar();
      }
      
      // parse section
      size_t start;
      if(cur_char == L'[') {
        start = cur_pos;
        while(cur_pos < input.size() && iswprint(cur_char) && cur_char != L']') {
          NextChar();
        }
        const wstring section = input.substr(start, cur_pos - start - 1);
        if(cur_char == L']') {
          NextChar();
        }        
        value_map = new map<const wstring, const wstring>;
        section_map.insert(pair<const wstring, map<const wstring, const wstring>*>(section, value_map));

        // wcout << "Section: |" << section << L"|" << endl;
      }
      // comment
      else if(cur_char == L'#') {
        while(cur_pos < input.size() && cur_char != L'\n' && cur_char != L'\r') {
          NextChar();
        }
      }
      // key/value
      else if(iswalpha(cur_char)) {
        start = cur_pos - 1;
        while(cur_pos < input.size() && iswprint(cur_char) && cur_char != L'=') {
          NextChar();
        }
        const wstring key = input.substr(start, cur_pos - start - 1);
        NextChar();
        
        wstring value;
        start = cur_pos - 1;
        while(cur_pos < input.size() && iswprint(cur_char) && cur_char != L'\n' && cur_char != L'\r') {
          if(cur_char == L'\\') {
            switch(next_char) {
            case L'n':
              value += L'\n';
              NextChar();
              break;
            case L'r':
              value += L'\r';
              NextChar();
              break;
            default:
              value += L'\\';
              break;
            }
          }
          else {
            value += cur_char;
          }
          
          NextChar();
        }
        
        // add key/value pair
        if(value_map) {
          value_map->insert(pair<const wstring, const wstring>(key, value));
        }

        // wcout << "Pair: key=|" << key << L"|, value=|" << value << L"|" << endl;
      } 
    }
  }
  
public:
  IniManager(const wstring &f) {
    filename = f;
    cur_char = next_char = L'\0';
    cur_pos = 0;
    
    input = LoadFile(filename);
    if(input.size() > 0) {
      ParseFile();
    }
  }
  
  ~IniManager() {
  }

  wstring GetValue(const wstring &sec, const wstring &key) {
    map<const wstring, map<const wstring, const wstring>*>::iterator section = section_map.find(sec);
    if(section != section_map.end()) {
      map<const wstring, const wstring>::iterator value = section->second->find(key);
      if(value != section->second->end()) {
        return value->second;
      }
    }
    
    return L"";
  }
};

int main(int argc, char* argv[]) {
  const string fn(argv[1]);
  const wstring filename(fn.begin(), fn.end());

  IniManager manager(filename);
  wcout << manager.GetValue(L"abbc", L"foo") << endl;
}
