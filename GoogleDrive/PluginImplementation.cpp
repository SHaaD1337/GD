#include "PluginImplementation.h"
#include "HBLib/filesystem/filesystem.h"
#include "HBLib/time/conversion.h"
#include "HBLib/strings/CommonParsing.h"
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include "Common/config.h"

using namespace std;

#include "cacert.h"

//------------------------------------------------------------------------------------------------

PluginImplementation::PluginImplementation(ILog *pLog, ILink* pLink)
:/* VSSWrapper(pLoï)
, */PluginBase(pLog)
{	
	m_pLog = pLog;
	m_pLink = pLink;
	wstring code,refresh_token;
	m_pLink->GetValue(L"AuthorizeCode", code);
	m_pLink->SetValue(L"AuthorizeCode", L"");
	m_pLink->GetValue(L"RefreshToken", refresh_token);

	m_gd.m_TempFileName= HBLib::filesystem::GetUniqueTempFilePath(HBLib::filesystem::GetTempFolderPath());

	unsigned char* buf=certificate;
	int byteswriten=1024;
	
	FILE *f=0;
	f = fopen(m_gd.ws2s(m_gd.m_TempFileName).c_str(),"wb");
	fwrite(certificate,sizeof(unsigned char),certificate_len,f);
	
	fclose(f);

	curl_global_init(CURL_GLOBAL_ALL);
	m_gd.InitCurl();
	if (m_gd.RequestAuthorization(code,refresh_token)==false)
		m_pLog->Write(LogEvent::FAILURE, L"Can't authorize, please get anouther code");

	m_pLink->SetValue(L"RefreshToken", m_gd.s2ws(m_gd.auth.refresh_token));

	m_gd.files.clear();
}

PluginImplementation::~PluginImplementation()
{
	/**/
	remove(m_gd.ws2s(m_gd.m_TempFileName).c_str());
	m_gd.m_TempFileName.clear();
	m_gd.FreeCurl();
}

ErrorID PluginImplementation::BeginTransaction()
{
	return Error::SUCCESS;
}

ErrorID PluginImplementation::CommitTransaction()
{
	return Error::SUCCESS;
}


ErrorID PluginImplementation::GetElementAttributes(const wstring &strPath, CFileAttributes &fa)
{	
    wstring FolderID,FileID;
	if (m_gd.GetPath(strPath,FolderID,FileID)==true)
		m_gd.FindDataToFileAttributes(m_gd.files[m_gd.FindFileByID(FileID)],fa);
	else 
	{
		m_gd.GetPathListing(strPath);
		if (m_gd.GetPath(strPath,FolderID,FileID)==true)
			m_gd.FindDataToFileAttributes(m_gd.files[m_gd.FindFileByID(FileID)],fa);
		else
			return Error::FAILED;
	}

  return Error::SUCCESS;
}

ErrorID PluginImplementation::SetElementAttributes(const wstring &strPath, const CFileAttributes &fa)
{
  return Error::SUCCESS; //todo: implement
}

ErrorID PluginImplementation::GetListing(const wstring &strPath, list<CFileInfo>& lst) 
{
	m_gd.StartofListing=true;
	m_gd.GetPathListing(strPath);
	if (m_gd.RequestDirListing(strPath)==false)
	{
		m_pLog->Write(LogEvent::FAILURE, L"Can't get listing");
		return Error::FAILED;
	}
	else
		if (m_gd.GetDirListing(strPath,lst))
			return Error::SUCCESS;
}

void PluginImplementation::OnStop() 
{
}

bool PluginImplementation::IsFolderExists(const wstring &strPath)
{
	wstring a,b;
	if (m_gd.GetPath(strPath,a,b))
		return true;
	return false;
}

ErrorID PluginImplementation::CreateFolder(const wstring &strPath) // works
{
	wstring folderID,fileID;
	list<CFileInfo> test;

	m_gd.FolderCreate(strPath,1);

	if (IsFileExists(strPath))
		return Error::SUCCESS;
	else 
		return Error::FAILED;
}

ErrorID PluginImplementation::DeleteFolder(const wstring &strPath)
{	
	if (m_gd.RequestElementDeletion(strPath))
	   {
		   return Error::SUCCESS;
	   }
	else
		return Error::FAILED;
}

ErrorID PluginImplementation::DeleteFile(const wstring &strPath)
{
	if (m_gd.RequestElementDeletion(strPath))
	   {
		   return Error::SUCCESS;
	   }
	else
		return Error::FAILED;
}

bool PluginImplementation::IsFileExists(const wstring &strPath)
{
	wstring a,b;
	if (m_gd.GetPath(strPath,a,b))
		return true;
	return false;
}

ErrorID PluginImplementation::OpenFile(const wstring &strPath, const FileAccessMode::ID mode, unsigned int &fh)
{
	m_gd.RequestAuthorization(L"",m_gd.s2ws(m_gd.auth.refresh_token));
	m_gd.GetPathListing(strPath);
	m_gd.FolderCreate(strPath,0);
	
	if (mode == FileAccessMode::Read) {
		m_gd.GetPathListing(strPath);
		if (m_gd.RequestFileDownload(strPath)) {
			fh = OPENED_FILE_HANDLE;
			m_openedFileHandle = fh;
			return Error::SUCCESS;
		}
		
	} else 
		if (mode == FileAccessMode::Write) {
		if (m_gd.RequestFileUpload(strPath)) {
			m_openedFileHandle = fh;
			fh = OPENED_FILE_HANDLE;
			return Error::SUCCESS;
		}
	}

	LogWrite(LogEvent::FAILURE, L"Can't open file");
	return Error::FAILED;
}

ErrorID PluginImplementation::ReadFile(unsigned char* buf, unsigned int &sz, const unsigned int &fh)
{
	if (fh != OPENED_FILE_HANDLE) {
		LogWrite(LogEvent::FAILURE, L"Invalid file handle: %d.", fh);
		return Error::FAILED;
	}

	if (m_gd.IsEOFReached()) {
		return Error::HANDLE_EOF; 
	}
	
	unsigned int bytesRead = 0;

	if (m_gd.ReadFileData(buf, sz, bytesRead)) { /*todo: process reading end*/
		sz = bytesRead;
		if (sz==0)
			sz=1;
		return Error::SUCCESS;
	} else { /* !!! remake whand EOF error become implemented !!! */
		sz = bytesRead;
		return Error::FAILED;
	}

	sz = 0;

	return Error::FAILED;
}

ErrorID PluginImplementation::WriteFile(const unsigned char* buf, unsigned int &sz, const unsigned int &fh)
{
	unsigned int bytesWritten = 0;
	m_gd.m_endingWasSent=false;
	if (m_gd.WriteFileData(buf, sz, bytesWritten)) {
		sz = bytesWritten;
		return Error::SUCCESS;
	} 
	
	return Error::FAILED;
}

ErrorID PluginImplementation::SeekFile(const unsigned int &fh, const unsigned __int64 &offset)
{
	return Error::NOT_SUPPORTED;
}

ErrorID PluginImplementation::CloseFile(const unsigned int &fh)
{
	if (fh != OPENED_FILE_HANDLE) {
		return Error::FAILED;
	} 
	m_gd.FinishFileOperation(); // bad, need to hide this method inside CWebDavConnection

	m_openedFileHandle = 0;
	
	return Error::SUCCESS;
}

ErrorID PluginImplementation::MoveFile(const std::wstring &strPathFrom, const std::wstring &strPathTo)
{
	return Error::NOT_SUPPORTED;
}

