#include "GDwraper.h"

int main(int argc, char* argv[])
{
	CGDConnection gd; 
	list<CFileInfo> listing;


	if (gd.InitCurl())
	   if (gd.Authorize())
	      {
			  gd.RequestDirListing(L"",listing);
	      }

	return 0;
}
