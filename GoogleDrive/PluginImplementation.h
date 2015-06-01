#ifndef PLUGINIMPLEMENTATION_LOCALFS_VSS_20101126
#define PLUGINIMPLEMENTATION_LOCALFS_VSS_20101126

#include <map>
#include "PluginFSInterfaces.h"
#include "PluginServiceInterfaces.h"
#include "PluginBase.h"
//#include "SimpleVSS/VSSWrapper.h"
#include "GDwraper.h"

#include <boost/format.hpp>

#include <stdio.h>
#include <string>
#include <iostream>
#include <sstream>
#include "JsonLib/JSON.h"
#include <Windows.h>
#include <fstream>
#include <algorithm>
#include "NeonStuff/ne_dates.h"
#include "utf8.h"

#undef DeleteFile // DeleteFile from WinBase.h
#undef MoveFile

#define OPENED_FILE_HANDLE 1

using namespace std;
//---------------------------------------------------------------------------------------------------


class PluginImplementation : public IBackupDataWrite, private PluginBase
{

private:
	bool IsFolderExists(const wstring &strPath); 
	bool IsFileExists(const wstring &strPath); 
	int FindFileByID(const wstring ID); 		
	int m_openedFileHandle;

public:
	PluginImplementation(ILog *pLog, ILink* pLink);
	virtual ~PluginImplementation();

	ILink* m_pLink;
	ILog* m_pLog;

	ErrorID BeginTransaction(); 
	ErrorID CommitTransaction();
	ErrorID GetElementAttributes(const wstring&, CFileAttributes&);
	ErrorID SetElementAttributes(const wstring&, const CFileAttributes&);
	ErrorID GetListing(const wstring&, list<CFileInfo>&);
	void OnStop();
	ErrorID CreateFolder(const wstring&);
	ErrorID DeleteFolder(const wstring&);
	ErrorID DeleteFile(const wstring &);

	ErrorID OpenFile(const wstring &strPath, const FileAccessMode::ID mode, unsigned int &fh);
	ErrorID ReadFile(unsigned char* buf, unsigned int &sz, const unsigned int &fh);
	ErrorID WriteFile(const unsigned char* buf, unsigned int &sz, const unsigned int &fh);
	ErrorID SeekFile(const unsigned int &fh, const unsigned __int64 &offset);
	ErrorID CloseFile(const unsigned int &fh);
	ErrorID MoveFile(const std::wstring &strPathFrom, const std::wstring &strPathTo);

private:
	HANDLE OpenFileForReading(const wstring &strPath);
	HANDLE OpenFileForWriting(const wstring &strPath);
	void CloseFile(HANDLE hFile);
	CGDConnection m_gd;
};


#endif // PLUGINIMPLEMENTATION_LOCALFS_VSS_20101126
