#include "stdafx.h"
#include <curl.h>
#include <vector>

#include "GDwraper.h"
#include "utf8.h"

#include "Common/config.h"

//using namespace pugi;

//size_t CallbackRW_size, CallbackRW_requested_size;

enum tDavRequest  {
	rPUT,
	rGET,
	rPROPFIND,
	rMKCOL,
	rDELETE,
};
//

int my_curl_debug_callback (CURL * curl, curl_infotype type, char * s, size_t n, void * ptr) {
	switch (type) {
		case CURLINFO_TEXT: printf("TEXT: "); break;
		case CURLINFO_HEADER_IN: printf("HEADER_IN: "); break; 
		case CURLINFO_HEADER_OUT: printf("HEADER_OUT: "); break; 
		case CURLINFO_DATA_IN: printf("DATA_IN: "); printf("%ld\r\n", atol(s));return 0;
		case CURLINFO_DATA_OUT: printf("DATA_OUT: "); printf("%ld\r\n", atol(s));return 0;
		case CURLINFO_SSL_DATA_IN: printf("SSL_DATA_IN: "); printf("%ld\r\n", atol(s));return 0;
		case CURLINFO_SSL_DATA_OUT: printf("SSL_DATA_OUT: "); printf("%ld\r\n", atol(s));return 0;
		default : printf("?? Something else: "); break;
	};

	s[n] = 0;
	printf("%s",s);
	return 0;
}

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void **userp) {
	/* some data form WebDAV server received, here we need to store it */

	size_t realsize = size * nmemb;
	char * c;
	string s;
	int n;
	size_t Remainder_size;
	
	if (!userp) { 
		return -1;
	}

	if (realsize < 1460) {
		s = "";
	}
	WebDAVRequest *wdr = ( WebDAVRequest *) userp;
	//CallbackRW_size = wdr->m_callbackRW_size;
	//CallbackRW_requested_size = wdr->m_callbackRW_requested_size;
  
	if (wdr->m_Mutex_WriterWantsData) {
		if ((s.find("Internal Server Error") != string::npos) || (s.find("Created") != string::npos)) {
			//wdr->m_Mutex_WriterWantsData->unlock();
			//wdr->m_Mutex_DataReady->lock();	// возвращаемся в readFile
			if (wdr->m_debugMode) printf("WCallback: EOF / Error\n");
		}

		return realsize;
	}

	if (wdr->m_readerWorkingFlag) {
		if (wdr->m_callbackRW_requested_size) {
			if (wdr->m_bufRemainder.capacity()) {
				memcpy( wdr->m_bufWrite + wdr->m_callbackRW_size ,  wdr->m_bufRemainder.data(), wdr->m_bufRemainder.capacity());
				wdr->m_callbackRW_size += wdr->m_bufRemainder.capacity();
				wdr->m_bufRemainder.clear();
				wdr->m_bufRemainder.shrink_to_fit();
			}

			if (!realsize) {
				wdr->m_EOFReachedFlag = true;
				//if (wdr->Debug) printf("WCallback (!realsize): ReaderWantsData unlock\n");
				wdr->m_Mutex_ReaderWantsData->unlock();
				//if (wdr->Debug) printf("WCallback (!realsize): DataReady lock\n");
				wdr->m_Mutex_DataReady->lock();
				return realsize;
			}

			if (wdr->m_callbackRW_requested_size < wdr->m_callbackRW_size + realsize) {
				wdr->m_bufRemainder.reserve(Remainder_size = ((wdr->m_callbackRW_size + realsize) - wdr->m_callbackRW_requested_size));
				c = wdr->m_bufRemainder.data();
				memcpy( wdr->m_bufRemainder.data(), (char*)contents + realsize - Remainder_size, Remainder_size);
				s = wdr->m_bufRemainder.data();
				memcpy( wdr->m_bufWrite + wdr->m_callbackRW_size , contents, realsize - Remainder_size);
				wdr->m_callbackRW_size += realsize - Remainder_size;
				//if (wdr->Debug) printf("WCallback (end of buf): ReaderWantsData unlock\n");
				wdr->m_Mutex_ReaderWantsData->unlock();
				//wdr->m_Mutex_DataReady->lock();	// возвращаемся в readFile
				//if (wdr->Debug) printf("WCallback (end of buf): DataReady lock\n");
				wdr->m_Mutex_DataReady->lock();
				//sharable_lock<interprocess_upgradable_mutex> lock(wdr->m_Mutex_DataReady, defer_lock);
				//wdr->m_Condition_DataReady.wait(lock);
				return realsize;
			}
		}
	}

	memcpy( wdr->m_bufWrite + wdr->m_callbackRW_size , (char*)contents, realsize);
	wdr->m_callbackRW_size += realsize;

	printf(" / WCallback: received %ld / ", realsize);
	
	return realsize;
}

size_t ReadMemoryCallback(void *contents, size_t size, size_t nmemb, void **userp) {
	/* data requesting for sending to WebDAV server */

	size_t realsize = size * nmemb;
	
	if (!userp) { 
		return -1;
	}

	WebDAVRequest *wdr = ( WebDAVRequest *) userp;
	
	if (!wdr->m_callbackRW_requested_size && !wdr->m_callbackRW_size) {
		return 0;
	}
	
	size_t to_copy = ((wdr->m_callbackRW_requested_size - wdr->m_callbackRW_size) < realsize) ?
				(wdr->m_callbackRW_requested_size - wdr->m_callbackRW_size) : realsize;

	memcpy(contents, wdr->m_bufRead + wdr->m_callbackRW_size, to_copy);
	wdr->m_callbackRW_size += to_copy;
  
	printf(" / RCallback: requested: %ld, sent: %ld / ", realsize, to_copy);

	//return to_copy;
	if (wdr->m_request.find("PROPFIND") == string::npos) {
		if (wdr->m_callbackRW_size - to_copy + realsize >= wdr->m_callbackRW_requested_size) { // достигнут конец буфера?
			//if (wdr->Debug) printf("RCallback: WrWants-lock, DataReady-unlock\n");
			wdr->m_Mutex_WriterWantsData->unlock();
			wdr->m_Mutex_DataReady->lock();
		}
	}
	return to_copy;

}

size_t HeaderReadCallback(void *contents, size_t size, size_t nmemb, void **userp) {
  size_t realsize = size * nmemb;
  if (!userp) return -1;
  WebDAVRequest *wdr = ( WebDAVRequest *) userp;
  string s = (char*) contents;
  wdr->m_bufHeader.push_back(s);
  return realsize;
}


bool my_curl_post_init(WebDAVRequest * wdr) {
	curl_global_init(CURL_GLOBAL_ALL);
	wdr->m_curl = curl_easy_init();
	
	if (!wdr->m_curl) {
		return false;
	}

	curl_easy_setopt(wdr->m_curl, CURLOPT_ERRORBUFFER, &wdr->m_curlErrBuf);
	curl_easy_setopt(wdr->m_curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(wdr->m_curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(wdr->m_curl, CURLOPT_USERAGENT, HBLib::strings::toAstr(HB_APPLICATION_NAME_WITH_VERSION_NO_SPACES).c_str());
	curl_easy_setopt(wdr->m_curl, CURLOPT_SSL_VERIFYPEER, false);
	
	if (wdr->m_debugMode)	{
		curl_easy_setopt(wdr->m_curl, CURLOPT_VERBOSE, true);
	} else {
		curl_easy_setopt(wdr->m_curl, CURLOPT_VERBOSE, false);
	}

	curl_easy_setopt(wdr->m_curl, CURLOPT_FOLLOWLOCATION, true);
	curl_easy_setopt(wdr->m_curl, CURLOPT_WRITEFUNCTION, (void*)WriteMemoryCallback);
	curl_easy_setopt(wdr->m_curl, CURLOPT_WRITEDATA, (void *)wdr);
	curl_easy_setopt(wdr->m_curl, CURLOPT_READFUNCTION, (void*)ReadMemoryCallback);
	curl_easy_setopt(wdr->m_curl, CURLOPT_READDATA, (void *)wdr);
	curl_easy_setopt(wdr->m_curl, CURLOPT_HEADER, 0);
	curl_easy_setopt(wdr->m_curl, CURLOPT_HEADERFUNCTION, HeaderReadCallback);
	curl_easy_setopt(wdr->m_curl, CURLOPT_HEADERDATA, wdr);
	//curl_easy_setopt(wdr->curl, CURLOPT_DEBUGFUNCTION, my_curl_debug_callback);

	return true;
}

void WebDAVRequest::ClearMemberVariables() {
	m_request = m_host = m_auth = m_path = m_depth = m_accept = m_charset = m_encoding = m_transferEncoding = m_url = 
		m_expect = m_contentType = m_contentSize = "";
	m_pHeadersList = NULL;
	m_curl = NULL;
	m_bufRead = m_bufWrite = NULL;
	m_pThread = NULL;

	m_curlErrBuf[0] = 0;
	m_bufSize = 0;
	m_Mutex_DataReady = m_Mutex_ReaderWantsData = m_Mutex_WriterWantsData = NULL;

	m_threadRunningFlag = false;
}

WebDAVRequest::WebDAVRequest() {
	ClearMemberVariables();
};

WebDAVRequest::WebDAVRequest(CURL * curl) {
	ClearMemberVariables();
	m_curl = curl;
	Init();
};

WebDAVRequest::~WebDAVRequest(){
	/**/
}; 

void WebDAVRequest::Init() {
	ClearMemberVariables();
	//Debug = true;
	m_firstWriterRun = true;
	m_lastCurlError = 0;
	m_writerWorkingFlag = false;
	m_readerWorkingFlag = false;
	//request = "PROPFIND";
	//url = "https://webdav.yandex.ru";
	//path = "/";
	//SetHost(url);
	//SetPath(path);
	//depth = "1";
	//accept = "*/*";
	//charset = "iso-8859-1, utf-8, utf-16, *;q=0.1"; 
	//encoding = "deflate, gzip, x-gzip, identity, *;q=0";
	//expect = content_type = content_size = " ";
}

void WebDAVRequest::SetHost(string u) {
	m_host = StrWord(u,3,'/');
	m_host = StrWord(m_host,1,'/');
	//host = StrWord(host,1,':');
	m_url = u;
	m_utf8Request = u + urlencode(cp1251_to_utf8(m_path.c_str()));

	curl_easy_setopt(m_curl, CURLOPT_URL, m_utf8Request.c_str());
};

void WebDAVRequest::SetPath(string path) {
	
	// append slash at the beginning // todo: already done in other parent methods, do it in one place
	if (!path.empty() && path[0] != '/') {
		path = "/" + path;
	}

	// replace backslashes with slashes
	for (int i = 0; i < path.length(); i++) {
		if (path[i] == '\\') path[i] = '/';
	}

	m_path = path;
	m_utf8Request = m_url + urlencode(cp1251_to_utf8(m_path.c_str()));
	
	curl_easy_setopt(m_curl, CURLOPT_URL, m_utf8Request.c_str());
}


void WebDAVRequest::SetRequestMethod(tDavRequest req) {
	switch (req) {
	    case rGET: m_request = "GET"; break;
		case rPUT: m_request = "PUT"; break;
		case rPROPFIND: m_request = "PROPFIND"; break;
		case rMKCOL: m_request = "MKCOL"; break;
		case rDELETE: m_request = "DELETE"; break;
	}

	curl_easy_setopt(m_curl, CURLOPT_CUSTOMREQUEST, m_request.c_str()); 
};


void WebDAVRequest::SetAuth(string login, string pwd, bool basic) {
	vector <char> buf(255);
	string lp = login + ":" + pwd;
	b64encode((char *)lp.c_str(), &buf[0]);
	lp = "Basic ";
	lp += &buf[0];
	m_auth = lp;
};


bool WebDAVRequest::CreateHeaders() {
	if (m_pHeadersList) {
		curl_slist_free_all(m_pHeadersList); m_pHeadersList = NULL; 
	}

	m_bufHeader.clear(); 
	m_bufHeader.shrink_to_fit();

	if (m_host.empty() || m_auth.empty() /*|| request.empty()*/) {
		return false;
	}
	m_tmpStr = "Host: " + m_host;
	m_pHeadersList = curl_slist_append(m_pHeadersList,m_tmpStr.c_str());

	m_tmpStr = "Authorization: " + m_auth;
	m_pHeadersList = curl_slist_append(m_pHeadersList, m_tmpStr.c_str());

	m_tmpStr = "Depth: " + m_depth;    
	if (!m_depth.empty()) {
		m_pHeadersList = curl_slist_append(m_pHeadersList, m_tmpStr.c_str());
	}

	m_tmpStr = "Accept: " + m_accept; 
	if (!m_accept.empty()) {
		m_pHeadersList = curl_slist_append(m_pHeadersList, m_tmpStr.c_str());
	}

	m_tmpStr = "Accept-Charset: " + m_charset;  
	if (!m_charset.empty()) {
		m_pHeadersList = curl_slist_append(m_pHeadersList, m_tmpStr.c_str());
	}

	m_tmpStr = "Accept-Encoding: " + m_encoding; 
	if (!m_encoding.empty()) { 
		m_pHeadersList = curl_slist_append(m_pHeadersList, m_tmpStr.c_str());
	}

	m_tmpStr = "Transfer-Encoding: " + m_transferEncoding; 
	if (!m_transferEncoding.empty()) {
		m_pHeadersList = curl_slist_append(m_pHeadersList, m_tmpStr.c_str());
	}

	m_tmpStr = "Expect: " + m_expect; 
	if (!m_expect.empty()) {
		m_pHeadersList = curl_slist_append(m_pHeadersList, m_tmpStr.c_str());  
	}

	m_tmpStr = "Content-Type: " + m_contentType; 
	if (!m_contentType.empty()) {
		m_pHeadersList = curl_slist_append(m_pHeadersList, m_tmpStr.c_str());
	}

	m_tmpStr = "Content-Length: " + m_contentSize;
 	if (!m_contentSize.empty()) {
		m_pHeadersList = curl_slist_append(m_pHeadersList, m_tmpStr.c_str());
	}

	//slist = curl_slist_append(slist, "Pragma: no-cache");  
	//slist = curl_slist_append(slist, "Cache-Control: no-cache");  
	curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, NULL); 
	curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, m_pHeadersList); 
	m_bufHeader.clear();
	m_bufHeader.shrink_to_fit();
	
	return true;
};



bool ParseXMLResponce(string xmlString, vector <TElem> &elems, eXMLParsingType parsingType) {
	string r;
	string time;
	char cstr [255];
	int len = xmlString.length();
	

	/* todo: investigate purpose */
	int i;
	int begBracket = -1;
	int endBracket;
	for (i = 0; i < len; i++) {
		if (xmlString[i] == '<') {
			begBracket = i;
		}

		if (xmlString[i] == '>') {
			if (begBracket == -1) {
				continue; 
			}
			endBracket = i;
			memcpy(cstr, xmlString.data() + begBracket, endBracket - begBracket + 1);
			cstr[endBracket - begBracket + 1] = 0;
			strlwr((char*)&cstr);
			xmlString.erase(begBracket, endBracket - begBracket + 1);
			xmlString.insert(begBracket, cstr);
			begBracket = -1;
		}
	}
 
	xml_document *xd = new xml_document;
	xml_node *xn = new xml_node;
	xml_parse_result result = xd->load((char_t*)xmlString.c_str());

	wstring wr;
	string BaseFolder = "";

	TElem elem;
	*xn = xd->first_child();
	r = xn->child_value();

	pugi::xpath_node node;
	pugi::xpath_node_set hdrs = xd->select_nodes("d:multistatus/d:response/d:href");
	for (pugi::xpath_node_set::const_iterator i = hdrs.begin(); i != hdrs.end(); ++i) {
		node = *i;
		*xn = node.parent();
		r = xn->child_value("d:href");
		if (!r.length()) continue;
		if ((i == hdrs.begin()) && (hdrs.begin() != hdrs.end()) && (parsingType == eXMLParsingType::ELEMENTS_LIST)) {
			BaseFolder = r;
			continue;
		}

		elem.type = FileType::Normal;
		
		if (r[r.length() - 1] == '/') {
			r.erase(r.length() - 1, 1);
			elem.type = FileType::Directory;
		}
		
		if (r.find("/") != string::npos) {
			if (r.find_last_of("/") != 0) {
				r.erase(0,r.find_last_of("/") + 1);
			} else {
				r.erase(0,r.find("/") + 1);
			}
		}
		
		*xn = xn->child("d:href");
		
		elem.unicode_name = r;
		r = utf8_to_cp1251(urldecode(r).c_str());
		elem.name = r;
		*xn = xn->parent().child("d:propstat").child("d:prop");
		elem.size = atol(xn->child_value("d:getcontentlength"));
		r = xn->child_value("d:getcontenttype");
		
		if (r.empty()) {
			r = xn->child_value("ns1:getcontenttype");
		}
		
		if ((r.find("directory") != string::npos) || r.empty()) {
			elem.type = FileType::Directory;
		}
		//if ((i == hdrs.begin()) && (hdrs.begin() != hdrs.end()) && (elem.type == FileType::Directory) && 
		//	(parsingType == eXMLParsingType::ELEMENTS_LIST)) continue;
		r = xn->child_value("d:getlastmodified");
		
		if (r.empty()) {
			r = xn->child_value("ns1:getlastmodified");
		}

		elem.time_Modified = ne_rfc1123_parse(r.c_str());
		r = xn->child_value("d:creationdate");
		
		if (r.empty()) {
			r = xn->child_value("ns1:creationdate");
		}

		elem.time_Created = ne_iso8601_parse(r.c_str());
		elems.push_back(elem);
	}

	delete xd;
	delete xn;
	
	return true;
}

ErrorID WebDAVRequest::CurlError(string &ErrStr) {
	string s;
	m_lastCurlError = 0;
	if (m_curlErrBuf[0]) {
		s = m_curlErrBuf;
		m_curlErrBuf[0] = 0;
		ErrStr = s;
		return Error::CRITICAL;
	};

	for (int i = 0; i < m_bufHeader.size(); i++) {
		s = m_bufHeader[i];
		if (s.find("HTTP/1.1") == 0) {
			string m_tmpStr = StrWord(s, 2, ' ');
			m_lastCurlError = atol(m_tmpStr.c_str());
			s = s.substr(9, s.length());

			if (m_lastCurlError == 100)  {// HTTP/1.1 100 Continue
				continue;
			}
			break;
		}
	}

	ErrStr = s;
	if (s.find("Insufficient Storage") != string::npos) return Error::DISK_FULL;
	if (s.find("Entity too large") != string::npos) return Error::DISK_FULL;
	if (s.find("Method Not Allowed") != string::npos) return Error::HANDLE_EOF;
	if (s.find("Conflict") != string::npos) return Error::PATH_NOT_FOUND; //209
	if (s.find("Not Found") != string::npos) return Error::PATH_NOT_FOUND; // 404
	if (s.find("Unauthorized") != string::npos) return Error::ACCESS_DENIED;
	if (s.find("Authorization Required") != string::npos) return Error::ACCESS_DENIED;
	if (s.find("Internal Server Error") != string::npos) return Error::FAILED;
	
	return Error::SUCCESS;
}


CURLcode WebDAVRequest::doPropfind(string path) {
	my_curl_post_init(this);
	SetRequestMethod(rPROPFIND);
	printf("%s: %s\n", m_request.c_str(), path.c_str());
	
	if (!path.empty()) {
		// append slash at the beginning
		if ((path[0] != '/') && (path[0] != '\\')) {
			path = "/" + path;
		}

		// append slash at the end
		if ((m_depth == "1") && (path[path.length() - 1] != '/') && (m_url[m_url.length()-1] != '/')) {
			path = path + "/";
		}
	}

	SetPath(path.c_str());
	m_expect = m_contentType =  m_transferEncoding = " ";

	//content_type = "text/xml";
	//string s = "<?xml version=\"1.0\" ?>	<D:propfind xmlns:D=\"DAV:\">	<D:allprop/>	</D:propfind>";
	//char cstr[10];
	//m_CallbackRW_requested_size = s.length(); 
	//content_size = _ltoa(m_CallbackRW_requested_size, ((char*)&cstr), 10);

	m_contentType = m_contentSize = " ";

	CreateHeaders();
	m_callbackRW_size = 0;

	curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, this);
	//curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
	//curl_easy_setopt(curl, CURLOPT_HEADER, 0);
	//
	//curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, m_CallbackRW_requested_size);
	//curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, m_CallbackRW_requested_size);
	//
	//curl_easy_setopt(curl, CURLOPT_READDATA, this);
	//curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);

	m_readerWorkingFlag = false;

	//bufRead = (unsigned char *) malloc(m_CallbackRW_requested_size + 1);
	//memcpy(bufRead, s.data(), m_CallbackRW_requested_size);

	m_curlCode = curl_easy_perform(m_curl);

	//free(bufRead);
	curl_easy_cleanup(m_curl);

	return m_curlCode;
}


CURLcode WebDAVRequest::doMkcol(string Path) {
	my_curl_post_init(this);
	SetRequestMethod(rMKCOL);
	printf("%s: %s\n", m_request.c_str(), Path.c_str());

	m_expect = m_transferEncoding = m_accept = m_contentType = m_depth = " ";

	curl_easy_setopt(m_curl, CURLOPT_HEADER, 0);
	
	if ( !Path.empty() && Path != "/" ) {
		Path = "/" + Path + "/";
	}
	SetPath(Path.c_str());
	curl_easy_setopt(m_curl, CURLOPT_HTTPGET, 1);
	CreateHeaders();
	m_callbackRW_requested_size = 0; 
	m_curlCode = curl_easy_perform(m_curl);
	curl_easy_cleanup(m_curl);
	return m_curlCode;
};

CURLcode WebDAVRequest::doGet(string Path) {
	my_curl_post_init(this);
	SetRequestMethod(rGET);
	printf("%s: %s\n", m_request.c_str(), Path.c_str());
	if ((!Path.empty()) && (Path != "/")) Path = "/" + Path;
	SetPath(Path.c_str());

	m_depth = m_encoding = m_charset = " ";

	CreateHeaders();

	m_callbackRW_size = 0;

	curl_easy_setopt(m_curl, CURLOPT_HEADER, 0);

	m_Mutex_ReaderWantsData = new interprocess_upgradable_mutex;
	m_Mutex_DataReady = new interprocess_upgradable_mutex;
	
	if (m_debugMode) {
		printf("GET: ReaderWantsData lock\n");
	}

	m_Mutex_ReaderWantsData->lock();
	m_bufRemainder.clear();
	m_bufRemainder.shrink_to_fit();

	if (m_debugMode) {
		printf("GET: DataReady lock\n");
	}

	m_Mutex_DataReady->lock();

	m_readerWorkingFlag = false;
	m_EOFReachedFlag = false;

	m_pThread = new boost::thread(ThreadPerform, this);
	
	return CURLE_OK;
};


CURLcode WebDAVRequest::doPut(string Path) {
	my_curl_post_init(this);
	SetRequestMethod(rPUT);
	printf("%s: %s\n", m_request.c_str(), Path.c_str());

	if ((!Path.empty()) && (Path != "/")) {
		Path = "/" + Path;
	}

	SetPath(Path.c_str());
	m_expect = "100-continue";
	m_contentType = "application/binary";

	m_charset = m_depth = m_encoding = m_contentSize = m_transferEncoding = " ";

	m_firstWriterRun = true;

	if (m_useTempFiles) {
		m_writtenFileSize = 0;
		if (boost::filesystem::exists(m_tempFile)) {
			boost::filesystem::remove(m_tempFile);
		}
		m_bufHeader.clear();
		m_bufHeader.shrink_to_fit();
	}

	curl_easy_setopt(m_curl, CURLOPT_HEADER, 0);
	curl_easy_setopt(m_curl, CURLOPT_UPLOAD, 1);

	if (m_useTempFiles) {
		return CURLE_OK;
	}

	m_transferEncoding = "chunked";
	CreateHeaders();

	curl_easy_setopt(m_curl, CURLOPT_INFILESIZE_LARGE, 0);
	curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, 0);

	curl_easy_setopt(m_curl, CURLOPT_READDATA, this);

	m_Mutex_WriterWantsData = new interprocess_upgradable_mutex;
	m_Mutex_DataReady = new interprocess_upgradable_mutex;

	if (m_debugMode) {
		printf("PUT: WriterWantsData lock\n");
	}

	m_Mutex_WriterWantsData->lock();
	m_bufRemainder.clear();
	m_bufRemainder.shrink_to_fit();

	if (m_debugMode) { 
		printf("PUT: DataReady lock\n");
	}

	m_writerWorkingFlag = false;
	m_Mutex_DataReady->lock();

	m_pThread = new boost::thread(ThreadPerform, this);
	
	return CURLE_OK;
};

void ThreadPerform(WebDAVRequest * pWDR) {
	if (!pWDR) {
		return;
	}

	if (!pWDR->m_pThread) {
		return;
	}

	pWDR->m_threadRunningFlag = true;
	if (pWDR->m_debugMode) {
		printf("Thread (%ld): enter\n", pWDR->m_pThread);
	}

	if (!pWDR->m_readerWorkingFlag && !pWDR->m_writerWorkingFlag) {
		if (pWDR->m_debugMode) {
			printf("Thread (%ld): DataReady lock\n", pWDR->m_pThread);
		}

		pWDR->m_Mutex_DataReady->lock(); // вешаем поток
	}

	pWDR->m_curlCode = curl_easy_perform(pWDR->m_curl);

	curl_easy_cleanup(pWDR->m_curl);

	pWDR->m_EOFReachedFlag = true;

	if (pWDR->m_Mutex_ReaderWantsData) {
		if (pWDR->m_debugMode) printf("Thread (%ld): unlock ReaderWantsData (%i)\n", pWDR->m_pThread, pWDR->m_Mutex_ReaderWantsData->try_lock());
		pWDR->m_Mutex_ReaderWantsData->unlock(); // 
	}

	if (pWDR->m_Mutex_WriterWantsData) {
		if (pWDR->m_debugMode) { 
			printf("Thread (%ld): unlock WriterWantsData (%i)\n", pWDR->m_pThread, pWDR->m_Mutex_WriterWantsData->try_lock());
		}

		pWDR->m_Mutex_WriterWantsData->unlock(); // 
	}

	if (pWDR->m_debugMode) { 
		printf("Thread (%ld): all unlocked!\n", pWDR->m_pThread);
	}

	pWDR->m_threadRunningFlag = false;
};

