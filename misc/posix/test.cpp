#include <iostream>
#include <sys/types.h>
#include <dirent.h>

using namespace std;

int main() {
	DIR* dirp = opendir(".");
	struct dirent* dp;

	while(dirp) {
	    dp = readdir(dirp);
	    if(dp) {
		cout << dp->d_name << endl;
	    } 
  	    else {
		closedir(dirp);
		return 1;
	    }
	}
}

