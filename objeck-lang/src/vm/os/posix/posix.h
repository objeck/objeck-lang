#ifndef __POSIX_H__
#define __POSIX_H__

#include "../../common.h"
#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>

class File {
 public:
  static long FileSize(const char* name) {
    int fd = open(name, O_RDONLY);
    if(fd < 0) {
      return -1;
    }
	  
    struct stat info;
    if(fstat(fd, &info) < 0) {
      return -1;
    }
	  
    close(fd);
    return info.st_size;
  }
  
  static bool FileExists(const char* name) {
    int fd = open(name, O_RDONLY);
    if(fd < 0) {
      return false;
    }
    
    close(fd);
    return true;
  }

  static FILE* FileOpen(const char* name, const char* mode) {
    FILE* file = fopen(name, "r");
    if(file < 0) {
      return NULL;
    }
    
    return file;
  }

  static bool MakeDir(const char* name) {
    if(mkdir(name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
      return false;
    }

    return true;
  }

  static bool IsDir(const char* name) {
    DIR* dir = opendir(name);
    if(!dir) {
      return false;
    }
    closedir(dir);

    return true;
  }

  static vector<string> ListDir(const char* path) {
    vector<string> files;
    
    struct dirent **names;
    int n = scandir(path, &names, 0, alphasort);
    if(n > 0) {
      while(n--) {
	if((strcmp(names[n]->d_name, "..") != 0) && (strcmp(names[n]->d_name, ".") != 0)) {
	  files.push_back(names[n]->d_name);
	}
	free(names[n]);
      }
      free(names);
    }

    return files;
  }
};

#endif
