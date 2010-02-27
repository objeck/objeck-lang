#ifndef __WINDOWS_H__
#define __WINDOWS_H__

#include "../../common.h"
#include <windows.h>

class File {
 public:
  static long FileSize(const char* name) {
    HANDLE file = CreateFile(name, GENERIC_READ, 
			     FILE_SHARE_READ, NULL, OPEN_EXISTING, 
			     FILE_ATTRIBUTE_NORMAL, NULL);
    
    if(file == INVALID_HANDLE_VALUE) {
      return -1;
    }
    
    long size = GetFileSize(file, NULL);
    CloseHandle(file);
    if(size < 0) {
      return -1;
    }
    
    return size;  
  }

  static bool FileExists(const char* name) {
    HANDLE file = CreateFile(name, GENERIC_READ, 
			     FILE_SHARE_READ, NULL, OPEN_EXISTING, 
			     FILE_ATTRIBUTE_NORMAL, NULL);    
    
    if(file == INVALID_HANDLE_VALUE) {
      return false;
    }
    CloseHandle(file);

    return true;
  }

  static FILE* FileOpen(const char* name, const char* mode) {
    FILE* file;
    if(fopen_s(&file, name, mode) != 0) {
      return NULL;
    }

    return file;
  }

  static bool MakeDir(const char* name) {
    if(CreateDirectory(name, NULL) == 0) {
      return false;
    }

    return true;
  }

  static bool IsDir(const char* name) {
    WIN32_FIND_DATA data;
    HANDLE find = FindFirstFile(name, &data);
    if(find == INVALID_HANDLE_VALUE) {
      return false;
    }
    FindClose(find);
    
    return true;
  }

  static vector<string> ListDir(char* p) {
    vector<string> files;
    
    string path = p;
    if(path.size() > 0 && path[path.size() - 1] == '\\') {
      path += "*";
    }
    else {
      path += "\\*";
    }

    WIN32_FIND_DATA file_data;
    HANDLE find = FindFirstFile(path.c_str(), &file_data);
    if(find == INVALID_HANDLE_VALUE) {
      return files;
    } 
    else {
      files.push_back(file_data.cFileName);

      bool b = FindNextFile(find, &file_data);
      while(b) {
        files.push_back(file_data.cFileName);
        b = FindNextFile(find, &file_data);
      }
      FindClose(find);
   }
    return files;
  }
};

#endif
