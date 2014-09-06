#include <windows.h>
#include <stdio.h>

void GetOSDisplayString(char* version, const int MAX) {
	OSVERSIONINFO OSversion;	
	OSversion.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&OSversion);
	
	switch(OSversion.dwPlatformId)
	{
	   case VER_PLATFORM_WIN32s: 
			   sprintf(version, "Windows %d.%d",OSversion.dwMajorVersion,OSversion.dwMinorVersion);
		   break;
	   case VER_PLATFORM_WIN32_WINDOWS:
		  if(OSversion.dwMinorVersion==0)
			  strncpy(version, "Windows 95", MAX - 1);
		  else
			  if(OSversion.dwMinorVersion==10)  
			  strncpy(version, "Windows 98", MAX - 1);
			  else
			  if(OSversion.dwMinorVersion==90)  
			  strncpy(version, "Windows Me", MAX - 1);
			  break;
	   case VER_PLATFORM_WIN32_NT:
		 if(OSversion.dwMajorVersion==5 && OSversion.dwMinorVersion==0)
				 sprintf(version, "Windows 2000 With %s", OSversion.szCSDVersion);
			 else	
			 if(OSversion.dwMajorVersion==5 &&   OSversion.dwMinorVersion==1)
				 sprintf(version, "Windows XP %s",OSversion.szCSDVersion);
			 else	
		 if(OSversion.dwMajorVersion<=4)
			sprintf(version, "Windows NT %d.%d with %s",
							   OSversion.dwMajorVersion,
							   OSversion.dwMinorVersion,
							   OSversion.szCSDVersion);			
			 else	
				 //for unknown windows/newest windows version

			sprintf(version, "Windows %d.%d ",
							   OSversion.dwMajorVersion,
							   OSversion.dwMinorVersion);
			 break;
	}
}

int main() {
	char version[80];
	GetOSDisplayString(version, 80);
	puts(version);
}