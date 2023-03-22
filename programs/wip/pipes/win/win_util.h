#ifndef __UTIL_H__
#define __UTIL_H__

#include <windows.h>

/****************************
 * Converts UTF-8 bytes to a 
 * native Unicode std::string 
 ****************************/
static bool BytesToUnicode(const std::string &in, std::wstring &out) {    
#ifdef _WIN32
  // allocate space
  const int wsize = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), -1, nullptr, 0);
  if(wsize == 0) {
    return false;
  }
  wchar_t* buffer = new wchar_t[wsize];

  // convert
  const int check = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), -1, buffer, wsize);
  if(check == 0) {
    delete[] buffer;
    buffer = nullptr;
    return false;
  }
  
  // create string
  out.append(buffer, wsize - 1);

  // clean up
  delete[] buffer;
  buffer = nullptr;  
#else
  // allocate space
  size_t size = mbstowcs(nullptr, in.c_str(), in.size());
  if(size == (size_t)-1) {
    return false;
  }
  wchar_t* buffer = new wchar_t[size + 1];

  // convert
  size_t check = mbstowcs(buffer, in.c_str(), in.size());
  if(check == (size_t)-1) {
    delete[] buffer;
    buffer = nullptr;
    return false;
  }
  buffer[size] = L'\0';

  // create string
  out.append(buffer, size);

  // clean up
  delete[] buffer;
  buffer = nullptr;
#endif

  return true;
}

static std::wstring BytesToUnicode(const std::string &in) {
  std::wstring out;
  if(BytesToUnicode(in, out)) {
    return out;
  }

  return L"";
}

/**
 * Converts UTF-8 bytes to
 * native a unicode character
 */
static bool BytesToCharacter(const std::string &in, wchar_t &out) {
  std::wstring buffer;
  if(!BytesToUnicode(in, buffer)) {
    return false;
  }
  
  if(buffer.size() != 1) {
#if defined(_ARM64) && defined(_OSX)
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> cvt;
    buffer = cvt.from_bytes(in);
    if(buffer.size() != 1) {
      return false;
    }
    
    out = buffer[0];
    return true;
#else
    return false;
#endif
  }
  
  out = buffer[0];  
  return true;
}

/**
 * Converts a native std::string
 * to UTF-8 bytes
 */
static bool UnicodeToBytes(const std::wstring &in, std::string &out) {
#ifdef _WIN32
  // allocate space
  const int size = WideCharToMultiByte(CP_UTF8, 0, in.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if(size == 0) {
    return false;
  }
  char* buffer = new char[size];
  
  // convert string
  const int check = WideCharToMultiByte(CP_UTF8, 0, in.c_str(), -1, buffer, size, nullptr, nullptr);
  if(check == 0) {
    delete[] buffer;
    buffer = nullptr;
    return false;
  }
  
  // append output
  out.append(buffer, size - 1);

  // clean up
  delete[] buffer;
  buffer = nullptr;
#else
  // convert string
  size_t size = wcstombs(nullptr, in.c_str(), in.size());
  if(size == (size_t)-1) {
    return false;
  }
  char* buffer = new char[size + 1];
  
  wcstombs(buffer, in.c_str(), size);
  if(size == (size_t)-1) {
    delete[] buffer;
    buffer = nullptr;
    return false;
  }
  buffer[size] = '\0';
  
  // create string      
  out.append(buffer, size);

  // clean up
  delete[] buffer;
  buffer = nullptr;
#endif
  
  return true;
}

static std::string UnicodeToBytes(const std::wstring &in) {
  std::string out;
  if(UnicodeToBytes(in, out)) {
    return out;
  }

  return "";
}

/**
 * Converts a native character
 * to UTF-8 bytes
 */
static bool CharacterToBytes(wchar_t in, std::string &out) {
  if(in == L'\0') {
    return true;
  }
  
  wchar_t buffer[2];
  buffer[0] = in;
  buffer[1] = L'\0';

  if(!UnicodeToBytes(buffer, out)) {
    return false;
  }
  
  return true;
}

#endif