#pragma once 
#include "stdafx.h"
#include <string.h>
#include "HBLib/strings/CommonParsing.h"


#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/lexical_cast.hpp>
#include "curl/curl.h"
#include "JsonLib/JSON.h"
#include "NeonStuff/ne_dates.h"
#include <stdio.h>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <vector>
#include "utf8.h"
#include <map>

//#include "SSLcertificate/cacert.txt"

using namespace std;
using namespace boost::interprocess;

#if 1 // todo: reimplement WebDAV plug-in with this

#include <boost/thread/condition_variable.hpp>

#include <boost/algorithm/string.hpp>

#include "Common/config.h"

#include "PluginStructures.h" /* HB stuff, like CFileInfo, etc. */

struct item 
{
	wstring ID;
	wstring title;
	wstring createdDate;
	wstring downloadUrl;
	wstring lastModifyingUserName;
	wstring fileSize;
	wstring extension;
	wstring type;
	wstring webContentLink;
	wstring parentID;
	wstring lastAccess;
	wstring modifiedDate;
	bool isRoot;
	wstring selflink;
};

struct authorize
{
	string authorize_code;
	string access_token;
	string refresh_token;
};

enum eGDRequestMethod {
	AUTHORIZE_METHOD,  
	GETLISTING_METHOD,
	UPLOAD_METHOD,
	DOWNLOAD_METHOD,
	DELETE_METHOD,
	PROPFIND_METHOD,  // list of files in directory | not used
	CREATEFOLDER_METHOD
};

enum eDepth {
	FILE_DEPTH = 0,
	DIRECTORY_DEPTH = 1
};

class CMyEvent {
	boost::mutex m_mutex;
	boost::condition_variable m_eventRisedCondition;
	bool m_isEventRised;
	int m_waitersCount;

public:
	CMyEvent(): m_isEventRised(false), m_waitersCount(0) {
		/**/
	};
	virtual ~CMyEvent() {
#if 1 //todo: is notify all needed on destruction
		{
			boost::unique_lock<boost::mutex> lock(m_mutex);
			m_isEventRised = true;
		}
		m_eventRisedCondition.notify_all();
#endif
	};

	void Rise() {
		{
			boost::unique_lock<boost::mutex> lock(m_mutex);
			m_isEventRised = true;
			m_waitersCount--;
		}
		m_eventRisedCondition.notify_one(); /* alert only one event listener */
	}

	void Wait(bool resetAfterWait = true) {
		boost::unique_lock<boost::mutex> lock(m_mutex);
		m_waitersCount++;
		while (!m_isEventRised) {
			m_eventRisedCondition.wait(lock);
		}
		if (resetAfterWait) {
			m_isEventRised = false;
		}
	}

	void Reset() {
		boost::unique_lock<boost::mutex> lock(m_mutex); /* is mutex needed? */
		m_isEventRised = false;
	}

	int GetWaitersCount() { /* not tested well */
		return m_waitersCount;
	}
};


class CMyPipe {

	bool m_isWriterOpened;
	bool m_isReaderOpened;

	CMyEvent m_eventDataForConsumerReady;
	CMyEvent m_eventAllDataConsumed;
	CMyEvent m_eventBufferIsNotUsed;
	
	char* m_pDataBuf;
	size_t m_dataBufSize;
	size_t m_dateBufPos;
	bool m_isBufUsedByReader;

public:

	CMyPipe() :
	  m_isWriterOpened(false),
		  m_isReaderOpened(false),
		  m_isBufUsedByReader(false) {
	  };

	  virtual ~CMyPipe() {
		  if (m_isReaderOpened || m_isWriterOpened) {
			  Close();
		  };
	  };

	  enum eMyPipeErrors {
		  MYPIPE_OK,
		  MYPIPE_FAILURE,
		  MYPIPE_WRITER_CLOSED,
		  MYPIPE_READER_CLOSED,
	  };
	  
	  eMyPipeErrors Open() { /* init pipe and open both ends */
		  if (m_isWriterOpened || m_isReaderOpened) {
			  return eMyPipeErrors::MYPIPE_FAILURE;
		  }

		  /* init buffer pointers */
		  m_pDataBuf = 0;
		  m_dataBufSize = 0;
		  m_dateBufPos = 0;

		  if (OpenWriter() != eMyPipeErrors::MYPIPE_OK) {
			  return eMyPipeErrors::MYPIPE_FAILURE;
		  }

		  if (OpenReader() != eMyPipeErrors::MYPIPE_OK) {
			  return eMyPipeErrors::MYPIPE_FAILURE;
		  }

		  return eMyPipeErrors::MYPIPE_OK;
	  }


	  eMyPipeErrors CloseWriter() { /* close reader end only, do it to set EOF for reader */
		  if (m_isWriterOpened) {
			  m_isWriterOpened = false;
			  m_eventBufferIsNotUsed.Rise(); // can't do this because external buffer is used by reader?
			  m_eventAllDataConsumed.Rise();

			  if (!m_isBufUsedByReader) { /* reader can wait for writer forever, so release it */
				  m_eventDataForConsumerReady.Rise();
			  }
			  return eMyPipeErrors::MYPIPE_OK;
		  }

		  return eMyPipeErrors::MYPIPE_WRITER_CLOSED;
	  }

	  eMyPipeErrors CloseReader() { /* close reader end only */
		  if (m_isReaderOpened) {
			  m_isReaderOpened = false;
			  m_eventDataForConsumerReady.Rise();

			  m_isBufUsedByReader = false;
			  m_eventBufferIsNotUsed.Rise();
			  return eMyPipeErrors::MYPIPE_OK;
		  }

		  return eMyPipeErrors::MYPIPE_READER_CLOSED;
	  }

	  eMyPipeErrors Close() { /* close both pipe ends */
		  CloseWriter();
		  CloseReader();

		  return eMyPipeErrors::MYPIPE_OK;
	  }


	  bool IsWriterOpened() {
		  return m_isWriterOpened;
	  }

	  bool IsReaderOpened() {
		  return m_isReaderOpened;
	  }

	  eMyPipeErrors WriteData(const char* pBuf, const size_t bufSize, size_t& bytesWritten) {

		  if (!m_isWriterOpened) {
			  return eMyPipeErrors::MYPIPE_WRITER_CLOSED;
		  }
		  if (!m_isReaderOpened) {
			  return eMyPipeErrors::MYPIPE_READER_CLOSED;
		  }

		  if (m_isBufUsedByReader) {
			  m_eventBufferIsNotUsed.Wait();
		  }

		  if (!m_isWriterOpened) {
			  return eMyPipeErrors::MYPIPE_WRITER_CLOSED;
		  }
		  if (!m_isReaderOpened) {
			  return eMyPipeErrors::MYPIPE_READER_CLOSED;
		  }

		  m_pDataBuf = (char*)pBuf;
		  m_dataBufSize = bufSize;
		  m_dateBufPos = 0;

		  m_eventDataForConsumerReady.Rise();
		  m_eventAllDataConsumed.Wait();

		  if (!m_isWriterOpened) {
			  return eMyPipeErrors::MYPIPE_WRITER_CLOSED;
		  }
		  if (!m_isReaderOpened) {
			  return eMyPipeErrors::MYPIPE_READER_CLOSED;
		  }

		  /* todo: analyze case when pipe closed and not all bytes was written */

		  bytesWritten = m_dateBufPos;
		  return eMyPipeErrors::MYPIPE_OK;
	  }

	  eMyPipeErrors ReadData(char* pBuf, const size_t bufSize, size_t& bytesRead) {

		  if (!m_isReaderOpened) {
			  return eMyPipeErrors::MYPIPE_READER_CLOSED;
		  }

		  if (!m_isBufUsedByReader) { /* no data to consume or buffer fully consumed */
			  if (m_isWriterOpened) {
				  m_eventDataForConsumerReady.Wait();
				  if (!m_isWriterOpened) {
					  return eMyPipeErrors::MYPIPE_WRITER_CLOSED;
				  }

				  m_isBufUsedByReader = true;
			  } else {
				  return eMyPipeErrors::MYPIPE_WRITER_CLOSED;
			  }
		  }

		  if (!m_isReaderOpened) {
			  return eMyPipeErrors::MYPIPE_READER_CLOSED;
		  }

		  char* pCurrentBuf = m_pDataBuf + m_dateBufPos;
		  size_t currentBufSize = m_dataBufSize - m_dateBufPos;

		  if (currentBufSize <= bufSize) { /* data fits to requested buffer */
			  memcpy(pBuf, pCurrentBuf, currentBufSize);
			  bytesRead = currentBufSize;
			  m_dateBufPos += currentBufSize;

			  m_isBufUsedByReader = false;

			  m_eventBufferIsNotUsed.Rise();
			  m_eventAllDataConsumed.Rise();
		  } else {
			  memcpy(pBuf, pCurrentBuf, bufSize);
			  bytesRead = bufSize;
			  m_dateBufPos += bufSize;
		  }

		  return eMyPipeErrors::MYPIPE_OK;
	  }

private:
	eMyPipeErrors OpenWriter() {
		m_isWriterOpened = true;

		m_eventDataForConsumerReady.Reset();

		return eMyPipeErrors::MYPIPE_OK;
	}

	eMyPipeErrors OpenReader() {
		m_isReaderOpened = true;

		m_isBufUsedByReader = false;
		m_eventBufferIsNotUsed.Reset();

		m_eventAllDataConsumed.Reset();

		return eMyPipeErrors::MYPIPE_OK;
	}

};

class CGDConnection {

	static friend size_t CurlWriteFunction(void *contents, size_t size, size_t nmemb, void **userp) {
		CGDConnection* pWebDAV = (CGDConnection*)userp;

		if (!pWebDAV->m_curlPerformThreadWorkingFlag) {
			return 0; // interrupt curl_easy_perform() execution
		}

		size_t bytesWritten = size*nmemb;

		if (pWebDAV->m_currentRequestMethod == eGDRequestMethod::AUTHORIZE_METHOD) {
			pWebDAV->m_AuthResponse.append((char*)contents, size*nmemb);
		};

		if ((pWebDAV->m_currentRequestMethod == eGDRequestMethod::GETLISTING_METHOD)||
		   (pWebDAV->m_currentRequestMethod == eGDRequestMethod::CREATEFOLDER_METHOD)||
		   (pWebDAV->m_currentRequestMethod == eGDRequestMethod::PROPFIND_METHOD)||
		   (pWebDAV->m_currentRequestMethod == eGDRequestMethod::UPLOAD_METHOD))
		{
			pWebDAV->m_responseJSON.append((char*)contents, size*nmemb);
		};

		if (pWebDAV->m_currentRequestMethod == eGDRequestMethod::DOWNLOAD_METHOD) {

			if (pWebDAV->m_dataPipe.WriteData((const char*)contents, size*nmemb, bytesWritten ) != CMyPipe::eMyPipeErrors::MYPIPE_OK) {
				return 0;
			}
		}

		return bytesWritten;
	}
	static friend size_t CurlReadFunction(void *contents, size_t size, size_t nmemb, void **userp) {

		CGDConnection* pGD = (CGDConnection*)userp;
		string ending="\n\n--foo_bar_baz--\n";

		if (!pGD->m_curlPerformThreadWorkingFlag) {
			return 0; // interrupt curl_easy_perform() execution
		}
		
		if (pGD->m_currentRequestMethod == eGDRequestMethod::PROPFIND_METHOD) {

		};

		if (pGD->m_currentRequestMethod == eGDRequestMethod::UPLOAD_METHOD) {

			size_t bytesRead = 0;

			CMyPipe::eMyPipeErrors pipeError;
			
			pipeError = pGD->m_dataPipe.ReadData((char*)contents, size*nmemb, bytesRead);

			if ( pipeError == CMyPipe::eMyPipeErrors::MYPIPE_OK) {
				if (pGD->m_endingWasSent==false)
				{
					if (bytesRead==0)
					{
						if (ending.size()<nmemb*size)
							memcpy(contents,ending.c_str(),ending.size());
						else
							return 0;
						bytesRead=ending.size();
					}
					pGD->m_endingWasSent=true;
					return bytesRead;
				}
				return bytesRead;
			} else if ( pipeError == CMyPipe::eMyPipeErrors::MYPIPE_WRITER_CLOSED) {
				return 0;
			} else if ( pipeError == CMyPipe::eMyPipeErrors::MYPIPE_FAILURE) {
				return 0;
			} else if ( pipeError == CMyPipe::eMyPipeErrors::MYPIPE_READER_CLOSED) {
				return 0;
			}

			return bytesRead;
		}

		return 0;
	}
	static friend size_t CurlHeaderFunction(void *contents, size_t size, size_t nmemb, void **userp) {

		CGDConnection* pGD = (CGDConnection*)userp;

		list<string> receivedHeaders;

		if (!pGD->m_curlPerformThreadWorkingFlag) {
			return 0; // interrupt curl_easy_perform() execution
		}


		string strHeader = (char*)contents;

		if (strHeader.find("HTTP") == 0) { /* is new headers portion, which first item looks like "HTTP/1.1 201 Created" */
			pGD->m_receivedHeaders.clear();	
			vector<string> splittedHeader;
			splittedHeader = HBLib::strings::Split(strHeader," ");
			if (splittedHeader.size() >= 3) {
				pGD->m_lastResponseCode = atol(splittedHeader[1].c_str());
				pGD->m_lastResponseString.clear();
				for (int i=2; i<splittedHeader.size(); ++i) { /* concatenate status message from splitted one */
					if (i!=2) { 
						pGD->m_lastResponseString += " ";
					}
					pGD->m_lastResponseString += splittedHeader[i];
				}
				boost::algorithm::trim_if(pGD->m_lastResponseString, boost::algorithm::is_any_of("\n\r"));
			} else {
				pGD->m_lastResponseCode = 0;
				pGD->m_lastResponseString.clear();
			}
			pGD->m_httpStatusHeaderReceivedEvent.Rise();
		}

		pGD->m_receivedHeaders.push_back(strHeader);

		return nmemb*size;
	}

	string m_poststring;
	eGDRequestMethod m_currentRequestMethod;
	eDepth m_currentDepth;
	curl_slist* m_pHeadersList;
	string m_responseJSON;
	string m_AuthResponse;

	bool m_curlPerformThreadWorkingFlag;

	list<string> m_receivedHeaders;
	int	m_lastResponseCode;
	string m_lastResponseString;

	/* events */
	CMyEvent m_curlCanPerformEvent;
	CMyEvent m_curlPerformFinishedEvent;

	CMyEvent m_readyToHandleRequestEvent; /* this event used to protect from multiple requests calls at one time */

	CMyEvent m_httpStatusHeaderReceivedEvent;

	/* new data exchange stuff */
	CMyPipe m_dataPipe;

	void* m_pDataForPeer;
	size_t m_dataForPeerSize;
	size_t m_remainingBytesForPeer;

	/* curl stuff */
	CURL* m_hCurl;
	CURLcode m_curlCode;
	boost::thread m_curlThread;

public:
	CGDConnection() : 
	  m_pHeadersList(0),
		  m_remainingBytesForPeer(0),
		  m_pDataForPeer(0),
		  m_dataForPeerSize(0),
		  m_curlPerformThreadWorkingFlag(1) {

	  };

	  virtual ~CGDConnection() {
		  FinishCurlPerformThread();

		  FreeHeadersList();
		  FreeCurl();
	  };

	  wstring rootID;
	  wstring nextPageToken;
	  bool m_pUploadType;
	  bool StartofListing;
	  wstring m_TempFileName;

	  wstring s2ws(const std::string& s)
	  {
		  int len;
		  int slength = (int)s.length() + 1;
		  len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0); 
		  std::wstring r(len, L'\0');
		  MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, &r[0], len);
		  r.resize(r.length()-1);
		  return r;
	  }

	  string ws2s(const std::wstring& s)
	  {
		  int len;
		  int slength = (int)s.length() + 1;
		  len = WideCharToMultiByte(CP_ACP, 0, s.c_str(), slength, 0, 0, 0, 0); 
		  std::string r(len, '\0');
		  WideCharToMultiByte(CP_ACP, 0, s.c_str(), slength, &r[0], len, 0, 0); 
		  r.resize(r.length()-1);
		  return r;
	  }

	  bool GetPathListing(wstring Path) // get info about needed files
	  {
		  wstring FolderID,FileID;
		  FolderID=rootID;
		  wstring CurrentDirName;
		  
		  if (rootID==L"")
			  if (RequestDirListing(L"")==false)
				  return false;

		  for (int i=0;i<Path.length();i++)
		  {
			  if (Path[i]==L'\\')
			  {
				  if (GetPath(CurrentDirName,FolderID,FileID)==false)
				  {
					  if (RequestDirListing(CurrentDirName)==false)
						  return false;
				  }
			  }
			  CurrentDirName+=Path[i];
		  }
		  if (GetPath(CurrentDirName,FolderID,FileID)==false)
		  {
			  if (RequestDirListing(CurrentDirName)==false)
				  return false;
		  }
		  return true;
	  }

	  bool GetPath(wstring Path, wstring &FolderID, wstring &FileID) // trying to get folder's and file's ID from Path
	  {
		  FolderID=rootID;
		  wstring CurrentDirName,CurrentID;
		  bool flag=false;
		  for (int i=0;i<Path.length();i++)
		  {
			  if (Path[i]==L'\\')
			  {
				  for (int j=0;j<files.size();j++)
				  {
					  if ((files[j].title==CurrentDirName)&&(files[j].parentID==FolderID))
						  if (flag==false)
						  {
							  flag=true;
							  FileID=files[j].ID;
							  FolderID=FileID;
							  break;
						  }
						  else 
						  {
							  FileID=files[j].ID;
							  FolderID=FileID;
							  break;
						  }
				  }
				  CurrentDirName=L"";
			  }
			  else
				  CurrentDirName+=Path[i];
		  }

		  bool correct=false;
		  for (int j=0;j<files.size();j++)
			  if ((files[j].title==CurrentDirName)&&(files[j].parentID==FolderID))
				  if (flag==false)
				  {
					  flag=true;
					  FileID=files[j].ID;
					  correct=true;
					  break;
				  }
				  else 
				  {
					  FolderID=FileID;
					  FileID=files[j].ID;
					  correct=true;
					  break;
				  }
				  if (correct==true)
					  return true;
				  else
					  return false;
	  }

	  bool FolderCreate(wstring Path, int TypeOfElement) // scan Path for not created folders and create them
	  {                                                  // typeofelement used for define element type: 1-dir, 0-file
		  wstring FolderID,FileID;
		  FolderID=rootID;
		  wstring CurrentDirName;

		  for (int i=0;i<Path.length();i++)
		  {
			  if (Path[i]==L'\\')
			  {
				  if (GetPath(CurrentDirName,FolderID,FileID)==false)
				  {
					  RequestDirCreation(CurrentDirName,0);
				  }
			  }
			  CurrentDirName+=Path[i];
		  }
		  if (TypeOfElement==1)
			  if (GetPath(CurrentDirName,FolderID,FileID)==false)
			  {
				  RequestDirCreation(CurrentDirName,0);
			  }
		  return true;
	  }

	  int FindFileByID(wstring ID)
	  {
		  for (int i=0;i<files.size();i++)
			  if (files[i].ID==ID)
				  return i;
		  return -1;
	  }

	  void FreeCurl() {
		  if (m_hCurl) {
			  curl_easy_cleanup(m_hCurl);
			  m_hCurl = 0;
		  }
	  };

	  void FinishCurlPerformThread() {
		  m_curlPerformThreadWorkingFlag = false;

		  NotifyWaiters();

		  m_curlThread.join(); // bad if curl hangs
	  }

	  void NotifyWaiters() 
	  {
		  m_curlCanPerformEvent.Rise();
		  m_httpStatusHeaderReceivedEvent.Rise(); // ?
	  }


	  void FindDataToFileAttributes(item file, CFileAttributes &fa) 
	  {
		  fa.m_tmCreation=ne_iso8601_parse(ws2s(file.createdDate).c_str());
		  fa.m_tmLastAccess=ne_iso8601_parse(ws2s(file.lastAccess).c_str());
		  fa.m_tmModification=ne_iso8601_parse(ws2s(file.modifiedDate).c_str());

		  if (file.fileSize!=L"")
		  {
			  fa.m_qwSize = boost::lexical_cast<uint64_t>(file.fileSize);
			  fa.m_bSize = 1;
		  } 
		  else fa.m_bSize = 0;

		  if (file.type==L"application/vnd.google-apps.folder") // is dir?
			  fa.m_nType = FileType :: Directory;
		  else 
			  fa.m_nType = FileType :: Normal;

		  fa.m_bModifTime = 1;
		  fa.m_bCreationTime = 1;
		  fa.m_bLastAccessTime = 1;
	  }

	  bool RequestAuthorization(wstring code, wstring refresh_token) {

		  int ErrorCode;
		  auth.authorize_code=ws2s(code);
		  auth.refresh_token=ws2s(refresh_token);

		  m_readyToHandleRequestEvent.Wait();
		  ResetInternalEvents();

		  ConfigureRequest(L"", eGDRequestMethod::AUTHORIZE_METHOD);
		  CreateHeaders(eDepth::DIRECTORY_DEPTH);

		  m_AuthResponse.clear();

		  if (m_dataPipe.Open() != CMyPipe::eMyPipeErrors::MYPIPE_OK) {
			  return false;
		  }

		  m_curlCanPerformEvent.Rise(); 
		  m_curlPerformFinishedEvent.Wait(); 

		  /* todo: parse response and get refresh_token */

		  if (IsResponseOK(m_AuthResponse,ErrorCode)==false)
			  return false;

		  if (code!=L"")
			  auth.refresh_token=GetVariable(m_AuthResponse,L"refresh_token");

		  return true;
	  }

      bool IsFileExists(item file)
	  {
		  for (int i=0;i<files.size();i++)
		  {
			  if (file.ID==files[i].ID)
				  return true;
		  }
		  return false;
	  }

	  bool RequestDirListing(wstring path) { // listing of files in dir

		  int ErrorCode;
		  if (StartofListing==true)
		  {
			  nextPageToken.clear();
			  StartofListing=false;
		  }

		  m_ListingNeeded=true;

		  ConfigureRequest(path, eGDRequestMethod::PROPFIND_METHOD);
		  CreateHeaders(eDepth::DIRECTORY_DEPTH);

		  if (m_ListingNeeded==false) //if we already have listing of this dir
			  return true;

		  m_readyToHandleRequestEvent.Wait();
		  ResetInternalEvents();

		  m_responseJSON.clear();

		  if (m_dataPipe.Open() != CMyPipe::eMyPipeErrors::MYPIPE_OK) {
			  return false;
		  }
		  m_curlCanPerformEvent.Rise(); // unfreeze curl perform thread
		  m_curlPerformFinishedEvent.Wait(); // wait for thread to do it's work

		  /* fill listing with response data */

		  if (IsResponseOK(m_responseJSON,ErrorCode)==false)
			  return false;
		  m_responseJSON=utf8_to_cp1251(urldecode(m_responseJSON).c_str());

		  ParseFileList();

		  return true;
	  }

	  bool RequestListing() { // full listing

		  int ErrorCode;
		  if (StartofListing==true)
		  {
			  files.clear(); 
			  nextPageToken.clear();
			  StartofListing=false;
		  }

		  m_readyToHandleRequestEvent.Wait();
		  ResetInternalEvents();

		  ConfigureRequest(L"", eGDRequestMethod::GETLISTING_METHOD);
		  CreateHeaders(eDepth::DIRECTORY_DEPTH);

		  m_responseJSON.clear();

		  if (m_dataPipe.Open() != CMyPipe::eMyPipeErrors::MYPIPE_OK) {
			  return false;
		  }
		  m_curlCanPerformEvent.Rise(); // unfreeze curl perform thread
		  m_curlPerformFinishedEvent.Wait(); // wait for thread to do it's work

		  /* fill listing with response data */

		  if (IsResponseOK(m_responseJSON,ErrorCode)==false)
			  return false;

		  m_responseJSON=utf8_to_cp1251(urldecode(m_responseJSON).c_str());

		  ParseFileList();

		  return true;
	  }

	  map <string, bool> m_isIDListed;
	  std::vector<item> files;
	  authorize auth;
	  string fileforaccess; 
	  bool m_endingWasSent;
	  bool m_ListingNeeded;

	  string GetVariable(string jsonResponse, wstring variable)
	  {
		  string result;
		  if (jsonResponse!="")
		  {
			  JSONValue *value = JSON::Parse(s2ws(jsonResponse).c_str());
			  JSONObject root = value->AsObject();
			  if (root.find(variable) != root.end() && root[variable]->IsString())
			  {
				  result=ws2s(root[variable]->AsString());
			  }
		  }
		  return result;
	  }

	  bool IsResponseOK(string jsonResponse, int &ErrorCode) // error code isn't parsing
	  {
		  if (jsonResponse!="")
		  {
			  JSONValue *value = JSON::Parse(s2ws(jsonResponse).c_str());
			  JSONObject root = value->AsObject();
			  if (root.find(L"error") != root.end() && root[L"error"]->IsObject())
			  {
				  /*if (root.find(L"code") != root.end() && root[L"code"]->IsNumber())
					  ErrorCode=(int)root[L"code"]->AsNumber();*/
				  return false;
			  }
		  }
		  else 
		  {
			  ErrorCode=-1;
			  return false;
		  }
		  return true;
	  }

	  bool InitCurl()
	  {
		  FreeHeadersList();
		  string m_tmpStr,poststring;

		  boost::thread thread(CurlPerformThreadFunction, this);
		  m_curlThread.swap(thread);

		  /* init curl */
		  curl_global_init(CURL_GLOBAL_ALL);
		  m_hCurl = curl_easy_init();

		  if (!m_hCurl) {
			  return false;
		  }

		  m_readyToHandleRequestEvent.Rise();
		  return true;
	  }

	  void AddFile()
	  {
		  item file;
		  m_responseJSON=utf8_to_cp1251(urldecode(m_responseJSON).c_str());
		  JSONValue *value1 = JSON::Parse(s2ws(m_responseJSON).c_str());
		  JSONObject root1 = value1->AsObject();

		  if (root1.find(L"createdDate") != root1.end() && root1[L"createdDate"]->IsString())
			  file.createdDate=root1[L"createdDate"]->AsString();
		  if (file.createdDate!=L"")
		  {
			  file.createdDate.resize(file.createdDate.length()-5);
			  file.createdDate+=L"Z";
		  }
		  if (root1.find(L"lastViewedByMeDate") != root1.end() && root1[L"lastViewedByMeDate"]->IsString())
			  file.lastAccess=root1[L"lastViewedByMeDate"]->AsString();
		  if (file.lastAccess!=L"")
		  {
			  file.lastAccess.resize(file.lastAccess.length()-5);
			  file.lastAccess+=L"Z";
		  }
		  if (root1.find(L"modifiedDate") != root1.end() && root1[L"modifiedDate"]->IsString())
			  file.modifiedDate=root1[L"modifiedDate"]->AsString();
		  {
			  file.modifiedDate.resize(file.modifiedDate.length()-5);
			  file.modifiedDate+=L"Z";
		  }

		  if (root1.find(L"downloadUrl") != root1.end() && root1[L"downloadUrl"]->IsString())
			  file.downloadUrl=root1[L"downloadUrl"]->AsString();

		  if (root1.find(L"lastModifyingUserName") != root1.end() && root1[L"lastModifyingUserName"]->IsString())
			  file.lastModifyingUserName=root1[L"lastModifyingUserName"]->AsString();

		  if (root1.find(L"id") != root1.end() && root1[L"id"]->IsString())
			  file.ID=root1[L"id"]->AsString();

		  if (root1.find(L"mimeType") != root1.end() && root1[L"mimeType"]->IsString())
			  file.type=root1[L"mimeType"]->AsString();
		  if (root1.find(L"fileExtension") != root1.end() && root1[L"fileExtension"]->IsString())
			  file.extension=root1[L"fileExtension"]->AsString();

		  if (root1.find(L"title") != root1.end() && root1[L"title"]->IsString())
			  file.title=root1[L"title"]->AsString();
		  if (root1.find(L"fileSize") != root1.end() && root1[L"fileSize"]->IsString())
			  file.fileSize=root1[L"fileSize"]->AsString();

		  if (root1.find(L"webContentLink") != root1.end() && root1[L"webContentLink"]->IsString())
			  file.webContentLink=root1[L"webContentLink"]->AsString();

		  if (root1.find(L"parents") != root1.end() && root1[L"parents"]->IsArray())
		  {
			  JSONArray array1 = root1[L"parents"]->AsArray();
			  for (int i = 0; i < array1.size(); i++)
			  {
				  JSONValue *value2 = JSON::Parse(array1[i]->Stringify().c_str());
				  JSONObject root2 = value2->AsObject();

				  if (root2.find(L"id") != root2.end() && root2[L"id"]->IsString())
					  file.parentID=root2[L"id"]->AsString();
				  if (root2.find(L"isRoot") != root2.end() && root2[L"isRoot"]->IsBool())
					  file.isRoot=root2[L"isRoot"]->AsBool();
				  if (root2.find(L"selfLink") != root2.end() && root2[L"selfLink"]->IsString())
					  file.selflink=root2[L"selfLink"]->AsString();
			  }
		  }
		  if (IsFileExists(file)==false)
			  files.push_back(file);
	  }

      void ParseFileList() 
	  {
		  JSONValue *value = JSON::Parse(s2ws(m_responseJSON).c_str());
		  item file;
		  JSONObject root;
		  root = value->AsObject();
		  if (root.find(L"nextPageToken") != root.end() && root[L"nextPageToken"]->IsString())
					  nextPageToken=root[L"nextPageToken"]->AsString();
		  else 
			  nextPageToken.clear();

		  if (root.find(L"items") != root.end() && root[L"items"]->IsArray())
		  {
			  JSONArray array = root[L"items"]->AsArray();
			  for (int i = 0; i < array.size(); i++)
			  {	
				  JSONValue *value1 = JSON::Parse(array[i]->Stringify().c_str());
				  JSONObject root1 = value1->AsObject();

				  if (root1.find(L"createdDate") != root1.end() && root1[L"createdDate"]->IsString())
					  file.createdDate=root1[L"createdDate"]->AsString();
				  if (file.createdDate.length()>5)
				  {
					  file.createdDate.resize(file.createdDate.length()-5);
					  file.createdDate+=L"Z";
				  }
				  if (root1.find(L"lastViewedByMeDate") != root1.end() && root1[L"lastViewedByMeDate"]->IsString())
					  file.lastAccess=root1[L"lastViewedByMeDate"]->AsString();
				  if (file.lastAccess.length()>5)
				  {
					  file.lastAccess.resize(file.lastAccess.length()-5);
					  file.lastAccess+=L"Z";
				  }
				  if (root1.find(L"modifiedDate") != root1.end() && root1[L"modifiedDate"]->IsString())
					  file.modifiedDate=root1[L"modifiedDate"]->AsString();
				  if (file.createdDate.length()>5)
				  {
					  file.modifiedDate.resize(file.modifiedDate.length()-5);
					  file.modifiedDate+=L"Z";
				  }

				  if (root1.find(L"downloadUrl") != root1.end() && root1[L"downloadUrl"]->IsString())
					  file.downloadUrl=root1[L"downloadUrl"]->AsString();

				  if (root1.find(L"lastModifyingUserName") != root1.end() && root1[L"lastModifyingUserName"]->IsString())
					  file.lastModifyingUserName=root1[L"lastModifyingUserName"]->AsString();

				  if (root1.find(L"id") != root1.end() && root1[L"id"]->IsString())
					  file.ID=root1[L"id"]->AsString();

				  if (root1.find(L"mimeType") != root1.end() && root1[L"mimeType"]->IsString())
					  file.type=root1[L"mimeType"]->AsString();
				  if (root1.find(L"fileExtension") != root1.end() && root1[L"fileExtension"]->IsString())
					  file.extension=root1[L"fileExtension"]->AsString();

				  if (root1.find(L"title") != root1.end() && root1[L"title"]->IsString())
					  file.title=root1[L"title"]->AsString();
				  if (root1.find(L"fileSize") != root1.end() && root1[L"fileSize"]->IsString())
					  file.fileSize=root1[L"fileSize"]->AsString();

				  if (root1.find(L"webContentLink") != root1.end() && root1[L"webContentLink"]->IsString())
					  file.webContentLink=root1[L"webContentLink"]->AsString();

				  if (root1.find(L"parents") != root1.end() && root1[L"parents"]->IsArray())
				  {
					  JSONArray array1 = root1[L"parents"]->AsArray();
					  for (int i = 0; i < array1.size(); i++)
					  {
						  JSONValue *value2 = JSON::Parse(array1[i]->Stringify().c_str());
						  JSONObject root2 = value2->AsObject();

						  if (root2.find(L"id") != root2.end() && root2[L"id"]->IsString())
							  file.parentID=root2[L"id"]->AsString();
						  if (root2.find(L"isRoot") != root2.end() && root2[L"isRoot"]->IsBool())
							  file.isRoot=root2[L"isRoot"]->AsBool();
						  if (root2.find(L"selfLink") != root2.end() && root2[L"selfLink"]->IsString())
							  file.selflink=root2[L"selfLink"]->AsString();						 
						  if (file.isRoot==true)
							  rootID=file.parentID;
					  }
				  }
				  if (IsFileExists(file)==false)
					  files.push_back(file);
			  }
		  }
		  if (nextPageToken!=L"")
			  RequestListing();
		  delete value;
	  }

	  bool GetDirListing(wstring path,std::list<CFileInfo>& listing)
	  {
		  wstring folderID,fileID; 
		  GetPath(path,folderID,fileID);
		  if (fileID==L"")
			  fileID=rootID;
		  list<CFileInfo> Files;
		  CFileAttributes fa;

		  for (int i=0;i<files.size();i++)
			  if (files[i].parentID==fileID)
			  {	
				  FindDataToFileAttributes(files[i],fa);

				  if (files[i].type==L"application/vnd.google-apps.folder") // is dir?
					  listing.push_back(CFileInfo(files[i].title, fa));
				  else
					  Files.push_back(CFileInfo(files[i].title, fa));
			  }

			  if (!Files.empty()) {
				  if (listing.empty())
					  listing.swap(Files);
				  else
					  listing.splice(listing.end(), Files);
			  }	
		  return true;
	  }

	  wstring GetDirName(wstring path)
	  {
		  wstring result1=L"",result=L"";
		  for (int i=path.length();i>=0;i--)
		  {
			  if (path[i]==L'\\')
				  break;
			  result1+=path[i];
		  }
		  if (result1[0]==' ')
			  result+=result1[result1.length()-1];
		  for (int i=1;i<result1.length();i++)
			  result+=result1[result1.length()-i];
		  return result;
	  }

	  bool RequestFileDownload(wstring path) { /* no waiting */

		  m_readyToHandleRequestEvent.Wait();

		  ResetInternalEvents();

		  if (m_dataPipe.Open() != CMyPipe::eMyPipeErrors::MYPIPE_OK) {
			  return false;
		  }
		  
		  ConfigureRequest(path, eGDRequestMethod::DOWNLOAD_METHOD);
		  CreateHeaders(eDepth::FILE_DEPTH);

		  m_curlCanPerformEvent.Rise();

		  return true;
	  };

	  bool ReadFileData(unsigned char* buf, unsigned int bufSize, unsigned int& bytesRead) { /* call it after RequestFileDownload() */
		  CMyPipe::eMyPipeErrors pipeError;

		  pipeError = m_dataPipe.ReadData((char*)buf, bufSize, (size_t&)bytesRead);

		  if ( pipeError == CMyPipe::eMyPipeErrors::MYPIPE_OK) {
			  return true;
		  } else if ( pipeError == CMyPipe::eMyPipeErrors::MYPIPE_WRITER_CLOSED) {
			  return true;
		  } else if ( pipeError == CMyPipe::eMyPipeErrors::MYPIPE_FAILURE) {
			  return false;
		  } else if ( pipeError == CMyPipe::eMyPipeErrors::MYPIPE_READER_CLOSED) {
			  return false;
		  }

		  return false;
	  }

	  bool IsEOFReached() {
		  return !m_dataPipe.IsWriterOpened();
	  }

	  void FinishFileOperation() {
		  if (m_currentRequestMethod == eGDRequestMethod::DOWNLOAD_METHOD) {
			  m_dataPipe.CloseReader();
			  m_curlPerformFinishedEvent.Wait();
		  };

		  string ending="\n\n--foo_bar_baz--\n";
		  int bytesWritten;
		  size_t bytes;

		  if (m_currentRequestMethod == eGDRequestMethod::UPLOAD_METHOD) {

			  if (m_pUploadType)
			  {
				  m_pUploadType=false;
				  m_dataPipe.WriteData(m_poststring.c_str(), m_poststring.size(), bytes);
			  }

			  m_dataPipe.WriteData(ending.c_str(), ending.size(), (size_t&)bytesWritten);
			  m_dataPipe.CloseWriter();
			  m_curlPerformFinishedEvent.Wait();
			  AddFile();
			  m_responseJSON.clear();
		  };
	  }

	  bool RequestFileUpload(wstring path) { /* no waiting */
		  
		  m_readyToHandleRequestEvent.Wait();
		  ResetInternalEvents();

		  if (m_dataPipe.Open() != CMyPipe::eMyPipeErrors::MYPIPE_OK) {
			  return false;
		  }

		  m_responseJSON.clear();

		  ConfigureRequest(path, eGDRequestMethod::UPLOAD_METHOD);
		  CreateHeaders(eDepth::FILE_DEPTH);
		
		  m_curlCanPerformEvent.Rise();

		  return true;
	  }

	  bool WriteFileData(const unsigned char* buf, unsigned int bufSize, unsigned int& bytesWritten) { /* call it after RequestFileUpload() */

		  size_t bytes;
		  if (m_pUploadType)
		  {
			  m_pUploadType=false;
			  if (m_dataPipe.WriteData(m_poststring.c_str(), m_poststring.size(), bytes) != CMyPipe::eMyPipeErrors::MYPIPE_OK) {
				  return false;
			  }
		  }

		  if (m_dataPipe.WriteData((char*)buf, bufSize, (size_t&)bytesWritten) != CMyPipe::eMyPipeErrors::MYPIPE_OK) {
			  return false;
		  }

		  return true;
	  }

	  bool RequestDirCreation(wstring path,int * httpError) {

		  m_readyToHandleRequestEvent.Wait();

		  ResetInternalEvents();

		  ConfigureRequest(path, eGDRequestMethod::CREATEFOLDER_METHOD);
		  CreateHeaders(eDepth::FILE_DEPTH);

		  m_responseJSON.clear();

		  m_curlCanPerformEvent.Rise(); // unfreeze curl perform thread

		  m_curlPerformFinishedEvent.Wait(); // wait for thread to do it's work

		  AddFile();
		  files;
		  m_responseJSON.clear();

		  return true;
	  }

	  bool RequestElementDeletion(wstring path) {

		  m_readyToHandleRequestEvent.Wait();

		  ResetInternalEvents();

		  ConfigureRequest(path, eGDRequestMethod::DELETE_METHOD);
		  CreateHeaders(eDepth::FILE_DEPTH); /* depth header not used in delete request */

		  m_curlCanPerformEvent.Rise(); // unfreeze curl perform thread
		  m_curlPerformFinishedEvent.Wait(); // wait for thread to do it's work

		  return true;
	  }

private:

	void ResetInternalEvents() {
		/* reset internal events */
		//m_curlCanPerformEvent.Reset();
		m_curlPerformFinishedEvent.Reset();
		m_httpStatusHeaderReceivedEvent.Reset();
	}

	bool ConfigureRequest(const wstring& path, eGDRequestMethod method) {
		m_currentRequestMethod = method;
		bool Exist;

		wstring folderID,fileID;
		Exist=GetPath(path,folderID,fileID);
		string filename = cp1251_to_utf8(urldecode(ws2s(GetDirName(path))).c_str()); 

		curl_easy_setopt(m_hCurl, CURLOPT_UPLOAD, 0);

		switch (m_currentRequestMethod) {
		case eGDRequestMethod::AUTHORIZE_METHOD: {
			curl_easy_setopt(m_hCurl, CURLOPT_CUSTOMREQUEST, "POST"); 
			curl_easy_setopt(m_hCurl, CURLOPT_WRITEFUNCTION, CurlWriteFunction);
			curl_easy_setopt(m_hCurl, CURLOPT_WRITEDATA, this);
			curl_easy_setopt(m_hCurl, CURLOPT_READFUNCTION, CurlReadFunction);
			curl_easy_setopt(m_hCurl, CURLOPT_READDATA, this);
			curl_easy_setopt(m_hCurl, CURLOPT_URL, "https://accounts.google.com/o/oauth2/token");

			if (auth.authorize_code!="")
			{
				m_poststring="code="+auth.authorize_code+"&client_id=619740930991-n7pa0a4q3u70gg3uk1bnqtspcooo14od.apps.googleusercontent.com&"
					"client_secret=87755rtrkluUPDAAu1v0B-Uw&"
					"redirect_uri=urn:ietf:wg:oauth:2.0:oob&"
					"grant_type=authorization_code";
			}
			else
			{
				m_poststring = "client_id=619740930991-n7pa0a4q3u70gg3uk1bnqtspcooo14od.apps.googleusercontent.com&"
					"client_secret=87755rtrkluUPDAAu1v0B-Uw&"
					"refresh_token="+auth.refresh_token+"&"
					"grant_type=refresh_token";
			}

			curl_easy_setopt(m_hCurl, CURLOPT_POSTFIELDS, m_poststring.c_str());
			curl_easy_setopt(m_hCurl, CURLOPT_POSTFIELDSIZE, (curl_off_t)m_poststring.length());
												 }; break;

		case eGDRequestMethod::GETLISTING_METHOD: {
			curl_easy_setopt(m_hCurl, CURLOPT_CUSTOMREQUEST, "GET"); 
			curl_easy_setopt(m_hCurl, CURLOPT_WRITEFUNCTION, CurlWriteFunction);
			curl_easy_setopt(m_hCurl, CURLOPT_WRITEDATA, this);
			curl_easy_setopt(m_hCurl, CURLOPT_READFUNCTION, CurlReadFunction);
			curl_easy_setopt(m_hCurl, CURLOPT_READDATA, this);
			string link;
			if (nextPageToken==L"")
				link="https://www.googleapis.com/drive/v2/files?maxResults=1000&fields=items(createdDate%2CdownloadUrl%2CfileExtension%2CfileSize%2Cid%2Clabels%2Ftrashed%2ClastModifyingUserName%2ClastViewedByMeDate%2CmimeType%2CmodifiedDate%2Cparents%2Ctitle)%2CnextPageToken";
			else
				link="https://www.googleapis.com/drive/v2/files?maxResults=1000&pageToken="+ws2s(nextPageToken)+"&fields=items(createdDate%2CdownloadUrl%2CfileExtension%2CfileSize%2Cid%2Clabels%2Ftrashed%2ClastModifyingUserName%2ClastViewedByMeDate%2CmimeType%2CmodifiedDate%2Cparents%2Ctitle)%2CnextPageToken";
			curl_easy_setopt(m_hCurl, CURLOPT_URL, link.c_str());
												  }; break;

		case eGDRequestMethod::PROPFIND_METHOD: {
			curl_easy_setopt(m_hCurl, CURLOPT_CUSTOMREQUEST, "GET"); 
			curl_easy_setopt(m_hCurl, CURLOPT_WRITEFUNCTION, CurlWriteFunction);
			curl_easy_setopt(m_hCurl, CURLOPT_WRITEDATA, this);
			curl_easy_setopt(m_hCurl, CURLOPT_READFUNCTION, CurlReadFunction);
			curl_easy_setopt(m_hCurl, CURLOPT_READDATA, this);
			string link;
			if (fileID==L"") 
			{
				link="https://www.googleapis.com/drive/v2/files?q='root'+in+parents+and+trashed%3Dfalse";
				m_isIDListed.clear();
			}
			else
			{
				if (m_isIDListed[ws2s(fileID)]==false)
				{
					link="https://www.googleapis.com/drive/v2/files?q='"+ws2s(fileID)+"'+in+parents+and+trashed%3Dfalse";
					m_isIDListed[ws2s(fileID)]=true;
				}
				else
					m_ListingNeeded=false;
			}
			if (nextPageToken==L"")
			{
				link+="&maxResults=1000&fields=items(createdDate%2CdownloadUrl%2CfileExtension%2CfileSize%2Cid%2Clabels%2Ftrashed%2ClastModifyingUserName%2ClastViewedByMeDate%2CmimeType%2CmodifiedDate%2Cparents%2Ctitle)%2CnextPageToken";
			}
			else
			{
				link+="&maxResults=1000&pageToken="+ws2s(nextPageToken)+"&fields=items(createdDate%2CdownloadUrl%2CfileExtension%2CfileSize%2Cid%2Clabels%2Ftrashed%2ClastModifyingUserName%2ClastViewedByMeDate%2CmimeType%2CmodifiedDate%2Cparents%2Ctitle)%2CnextPageToken";
				m_ListingNeeded=true;
				m_isIDListed[ws2s(fileID)]=false;
			}

			curl_easy_setopt(m_hCurl, CURLOPT_URL, link.c_str());
												  }; break;

		case eGDRequestMethod::DOWNLOAD_METHOD: {
			curl_easy_setopt(m_hCurl, CURLOPT_WRITEFUNCTION, CurlWriteFunction);
			curl_easy_setopt(m_hCurl, CURLOPT_WRITEDATA, this);
			curl_easy_setopt(m_hCurl, CURLOPT_READFUNCTION, CurlReadFunction);
			curl_easy_setopt(m_hCurl, CURLOPT_READDATA, this);
			curl_easy_setopt(m_hCurl, CURLOPT_CUSTOMREQUEST, "GET"); 
			string m_tmpStr;

			curl_easy_setopt(m_hCurl, CURLOPT_URL, ws2s(files[FindFileByID(fileID)].downloadUrl).c_str());
												}; break;

		case eGDRequestMethod::UPLOAD_METHOD: {
			m_pUploadType=true;
			curl_easy_setopt(m_hCurl, CURLOPT_READFUNCTION, CurlReadFunction);
			curl_easy_setopt(m_hCurl, CURLOPT_READDATA, this);
			curl_easy_setopt(m_hCurl, CURLOPT_WRITEFUNCTION, CurlWriteFunction);
			curl_easy_setopt(m_hCurl, CURLOPT_WRITEDATA, this);
			curl_easy_setopt(m_hCurl, CURLOPT_UPLOAD, 1);
			if (Exist==false)
			{
				curl_easy_setopt(m_hCurl, CURLOPT_URL, "https://www.googleapis.com/upload/drive/v2/files?uploadType=multipart");
				curl_easy_setopt(m_hCurl, CURLOPT_CUSTOMREQUEST, "POST");
			}
			else
			{
				string link="https://www.googleapis.com/upload/drive/v2/files/"+ws2s(fileID)+"?uploadType=multipart";
				curl_easy_setopt(m_hCurl, CURLOPT_URL, link.c_str());
				curl_easy_setopt(m_hCurl, CURLOPT_CUSTOMREQUEST, "PUT");
			}

			m_poststring =	"\n\n--foo_bar_baz\n"
				"Content-Type: application/json; charset=UTF-8\n\n"
				"{\n"
				"\"title\": \""+filename+"\",\n"
				"\"mimeType\": \"application/octet-stream\",\n"
				"\"parents\": [\n"
				"{\n"
				"\"id\": \""+ws2s(folderID)+"\"\n"
				"}\n"
				"]\n"
				"}\n"
				"--foo_bar_baz\n"
				"Content-Type: application/octet-stream\n\n";

											  }; break;

		case eGDRequestMethod::CREATEFOLDER_METHOD: {
			if (Exist==false)
			{
				curl_easy_setopt(m_hCurl, CURLOPT_WRITEFUNCTION, CurlWriteFunction);
				curl_easy_setopt(m_hCurl, CURLOPT_WRITEDATA, this);
				curl_easy_setopt(m_hCurl, CURLOPT_CUSTOMREQUEST, "POST"); 
				curl_easy_setopt(m_hCurl, CURLOPT_URL, "https://www.googleapis.com/upload/drive/v2/files?uploadType=multipart");

				m_poststring =	"\n\n--foo_bar_baz\n"
					"Content-Type: application/json; charset=UTF-8\n\n"
					"{\n"
					"\"title\": \""+filename+"\",\n"//ws2s(GetDirName(path))
					"\"mimeType\": \"application/vnd.google-apps.folder\",\n"
					"\"parents\": [\n{\n\"id\": \""+ws2s(folderID)+"\"\n}\n]\n"
					"}\n"
					"--foo_bar_baz--\n";


				curl_easy_setopt(m_hCurl, CURLOPT_POSTFIELDS, m_poststring.c_str());
				curl_easy_setopt(m_hCurl, CURLOPT_POSTFIELDSIZE, (curl_off_t)m_poststring.length());
			}

													}; break;

		case eGDRequestMethod::DELETE_METHOD: {
			curl_easy_setopt(m_hCurl, CURLOPT_CUSTOMREQUEST, "DELETE");
			string URL="https://www.googleapis.com/drive/v2/files/"+ws2s(fileID);
			curl_easy_setopt(m_hCurl, CURLOPT_URL, URL);

											  }; break;
		default:
			return false;
		}

		return true;
	}

	bool CreateHeaders(eDepth depth) {
		m_currentDepth = depth;

		FreeHeadersList();

		string tmpStr;

		curl_easy_setopt(m_hCurl, CURLOPT_VERBOSE, 1);
		curl_easy_setopt(m_hCurl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
		curl_easy_setopt(m_hCurl, CURLOPT_SSL_VERIFYPEER , 1);
		curl_easy_setopt(m_hCurl, CURLOPT_SSL_VERIFYHOST , 1);
		
		tmpStr=ws2s(m_TempFileName);
		curl_easy_setopt(m_hCurl, CURLOPT_CAINFO , tmpStr.c_str());

		curl_easy_setopt(m_hCurl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(m_hCurl, CURLOPT_HEADER, 0);
		curl_easy_setopt(m_hCurl, CURLOPT_POST, 0); 

		/* dafaults */

		if (m_currentRequestMethod == eGDRequestMethod::AUTHORIZE_METHOD) {
			curl_easy_setopt(m_hCurl, CURLOPT_POST, 1);
			m_pHeadersList = curl_slist_append(m_pHeadersList, "Host: accounts.google.com");
			m_pHeadersList = curl_slist_append(m_pHeadersList, "Content-Type: application/x-www-form-urlencoded");
		}
		else
		{
			auth.access_token=GetVariable(m_AuthResponse,L"access_token");
			tmpStr = "Authorization: Bearer " + auth.access_token;
			m_pHeadersList = curl_slist_append(m_pHeadersList, tmpStr.c_str());
		}

		if (m_currentRequestMethod == eGDRequestMethod::GETLISTING_METHOD) {

		}

		if (m_currentRequestMethod == eGDRequestMethod::DOWNLOAD_METHOD) {

		}

		if (m_currentRequestMethod == eGDRequestMethod::UPLOAD_METHOD) {
			m_pHeadersList = curl_slist_append(m_pHeadersList,"Content-Type: multipart/related; boundary=\"foo_bar_baz\"");
			curl_easy_setopt(m_hCurl, CURLOPT_POST, 1);
		}


		if (m_currentRequestMethod == eGDRequestMethod::CREATEFOLDER_METHOD) {
			m_pHeadersList = curl_slist_append(m_pHeadersList,"Content-Type: multipart/related; boundary=\"foo_bar_baz\"");
			curl_easy_setopt(m_hCurl, CURLOPT_POST, 1);
		}

		curl_easy_setopt(m_hCurl, CURLOPT_HTTPHEADER, NULL); 
		curl_easy_setopt(m_hCurl, CURLOPT_HTTPHEADER, m_pHeadersList); 

		return true;
	};

	void FreeHeadersList() {
		if (m_pHeadersList) {
			curl_slist_free_all(m_pHeadersList);
			m_pHeadersList = NULL; 
		}
	}

private:

	static friend void CurlPerformThreadFunction(void* pParam) {
		CGDConnection* pGD = (CGDConnection*)pParam;

		while (pGD->m_curlPerformThreadWorkingFlag) { /* todo: insert control variable here */
			pGD->m_curlCanPerformEvent.Wait();

			if (!pGD->m_curlPerformThreadWorkingFlag) {
				break;
			}

			CURLcode curl_ret = curl_easy_perform(pGD->m_hCurl);

			if (pGD->m_currentRequestMethod == eGDRequestMethod::AUTHORIZE_METHOD) {
				pGD->m_dataPipe.Close();
			}

			if (pGD->m_currentRequestMethod == eGDRequestMethod::GETLISTING_METHOD) {
				pGD->m_dataPipe.Close();
			}

			if (pGD->m_currentRequestMethod == eGDRequestMethod::PROPFIND_METHOD) {
				pGD->m_dataPipe.Close();
			}

			if (pGD->m_currentRequestMethod == eGDRequestMethod::DOWNLOAD_METHOD) {
				pGD->m_dataPipe.Close();
			}		

			if (pGD->m_currentRequestMethod == eGDRequestMethod::UPLOAD_METHOD) {
				pGD->m_dataPipe.Close();
			}

			pGD->m_httpStatusHeaderReceivedEvent.Rise(); /* release headers waiters */

			pGD->m_curlPerformFinishedEvent.Rise();
			pGD->m_readyToHandleRequestEvent.Rise();
		}
	}
};


class CWebDAVRequest {

public:
	CWebDAVRequest() {};
	virtual ~CWebDAVRequest() {};
};

class CWebDAVResponce {

public:
	CWebDAVResponce() {};
	virtual ~CWebDAVResponce() {};
};

#endif

class WebDAVRequest;

//extern size_t CallbackRW_size, CallbackRW_requested_size;

extern enum tDavRequest;


class WebDAVRequest {
	string m_tmpStr;
private:
	void ClearMemberVariables();

public:
	curl_slist* m_pHeadersList; 
	unsigned char* m_bufRead;
	unsigned char* m_bufWrite;
	vector<string> m_bufHeader;
	vector<char> m_bufRemainder;
	size_t m_callbackRW_size;
	size_t m_callbackRW_requested_size;
	int m_bufSize;
	CURL *m_curl;
	char m_curlErrBuf[CURL_ERROR_SIZE];
	string m_request;
	string m_url;
	string m_host;
	string m_path;
	string m_auth;
	string m_depth;
	string m_accept;
	string m_charset;
	string m_encoding;
	string m_transferEncoding;
	string m_expect;
	string m_contentType;
	string m_contentSize;
	string m_utf8Request;

	WebDAVRequest();
	~WebDAVRequest();

	WebDAVRequest(CURL * curl);
	void SetHost(string s);
	void SetPath(string s);
	void Init();
	void SetRequestMethod(tDavRequest req);
	void SetAuth(string login, string pwd, bool basic);
	void SetCSize(size_t size);
	bool CreateHeaders();

	boost::interprocess::interprocess_upgradable_mutex *m_Mutex_DataReady;
	boost::interprocess::interprocess_upgradable_mutex *m_Mutex_ReaderWantsData;
	boost::interprocess::interprocess_upgradable_mutex *m_Mutex_WriterWantsData;
	boost::thread *m_pThread;

	bool m_useTempFiles;
	string m_tempFile;

	bool m_firstWriterRun;
	uint64_t m_writtenFileSize;

	bool m_readerWorkingFlag;
	bool m_writerWorkingFlag;

	bool m_EOFReachedFlag;

	bool m_threadRunningFlag;
	int m_lastCurlError;

	bool m_debugMode;

	CURLcode m_curlCode;

	CURLcode doPropfind(string Path);
	CURLcode doCREATEFOLDER(string Path);
	CURLcode doGet(string Path);
	CURLcode doPut(string Path);

	ErrorID CurlError(string &ErrStr);

	friend void ThreadPerform(WebDAVRequest * pWDR);
};


//bool my_curl_post_init(WebDAVRequest * wdr);
//
//size_t ReadMemoryCallback(void *contents, size_t size, size_t nmemb, void **userp);
//
//size_t HeaderReadCallback(void *contents, size_t size, size_t nmemb, void **userp);
//
//size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void **userp);

//bool ParseXMLResponce(string xmlString, vector <TElem> &elems, eXMLParsingType parsingType);


























