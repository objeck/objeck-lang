#include <string>
#include <fstream>
#include <iostream>
#include <stdlib.h>

using namespace std;

wchar_t* LoadFileBuffer(wstring filename, size_t& buffer_size) {
  char* buffer;
  string open_filename(filename.begin(), filename.end());
    
  ifstream in(open_filename.c_str(), ios_base::in | ios_base::binary | ios_base::ate);
  if(in.good()) {
    // get file size
    in.seekg(0, ios::end);
    buffer_size = (size_t)in.tellg();
    in.seekg(0, ios::beg);
    buffer = (char*)calloc(buffer_size + 1, sizeof(char));
    in.read(buffer, buffer_size);
    // close file
    in.close();
  }
  else {
    wcerr << L"Unable to open source file: " << filename << endl;
    exit(1);
  }

  // convert unicode
#ifdef _WIN32
  int wsize = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, NULL, 0);
  if(!wsize) {
    wcerr << L"Unable to open source file: " << filename << endl;
    exit(1);
  }
  wchar_t* wbuffer = new wchar_t[wsize];
  int check = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wbuffer, wsize);
  if(!check) {
    wcerr << L"Unable to open source file: " << filename << endl;
    exit(1);
  }
#else
  size_t wsize = mbstowcs(NULL, buffer, buffer_size);
  if(wsize == (size_t)-1) {
    delete buffer;
    wcerr << L"Unable to open source file: " << filename << endl;
    exit(1);
  }
  wchar_t* wbuffer = new wchar_t[wsize + 1];
  size_t check = mbstowcs(wbuffer, buffer, buffer_size);
  if(check == (size_t)-1) {
    delete buffer;
    delete[] wbuffer;
    wcerr << L"Unable to open source file: " << filename << endl;
    exit(1);
  }
  wbuffer[wsize] = L'\0';
#endif
    
  free(buffer);
  return wbuffer;
}

class IniManager {
  wstring filename, input;
  wchar_t cur_char, next_char;
  size_t cur_pos;
  
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
  
  bool ParseFile() {
    NextChar();
    
    while(cur_char != L'\0') {
      // ignore white space
      while(cur_char == L' ' || cur_char == L'\t' || cur_char == L'\n' || cur_char == L'\r') {
        NextChar();
      }
      
      size_t start;
      if(cur_char == L'[') {
        start = cur_pos;
        while(cur_pos < input.size() && iswprint(cur_char) && cur_char != L']') {
          NextChar();
        }
        const wstring title = input.substr(start, cur_pos - start - 1);

        if(cur_char == L']') {
          NextChar();
        }

wcout << "Title: |" << title << L"|" << endl;
      }
      else if(cur_char == L'#') {
        while(cur_pos < input.size() && cur_char != L'\n' && cur_char != L'\r') {
          NextChar();
        }
      }
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
            case L'#':
              value += L'#';
              NextChar();
              break;
            case L'=':
              value += L'=';
              NextChar();
              break;
            case L'r':
              value += L'\r';
              NextChar();
              break;
            case L'n':
              value += L'\n';
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
        
wcout << "Pair: key=|" << key << L"|, value=|" << value << L"|" << endl;
      } 
    }
    
    return 0;
  }
  
public:
  IniManager(wstring f) {
    filename = f;
    cur_char = next_char = L'\0';
    cur_pos = 0;
    
    size_t buffer_size;
    input = LoadFileBuffer(filename, buffer_size);
    if(buffer_size) {
      ParseFile();
    }
  }
  
  ~IniManager() {
  }
};

int main(int argc, char* argv[]) {
  const string fn(argv[1]);
  const wstring filename(fn.begin(), fn.end());

  IniManager manager(filename);
}
