/****************************************************************************
 Copyright (c) 2012      greathqy
 Copyright (c) 2012      cocos2d-x.org
 Copyright (c) 2013-2014 Chukong Technologies Inc.
 
 http://www.cocos2d-x.org
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#include "HttpClient.h"

#include <thread>
#include <queue>
#include <condition_variable>

#include <errno.h>

#include "base/CCVector.h"
#include "base/CCDirector.h"
#include "base/CCScheduler.h"

#include "curl/curl.h"

#include "platform/CCFileUtils.h"

NS_CC_BEGIN

namespace network {

static std::mutex       s_requestQueueMutex;
static std::mutex       s_responseQueueMutex;

static std::mutex       s_SleepMutex;
static std::condition_variable      s_SleepCondition;


#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
typedef int int32_t;
#endif

static bool s_need_quit = false;

static std::deque<HttpRequest*>*  s_requestQueue = nullptr;
static std::deque<HttpResponse*>* s_responseQueue = nullptr;

static HttpClient *s_pHttpClient = nullptr; // pointer to singleton

static char s_errorBuffer[CURL_ERROR_SIZE] = {0};

typedef size_t (*write_callback)(void *ptr, size_t size, size_t nmemb, void *stream);

static std::string s_cookieFilename = "";

// Callback function used by libcurl for collect response data
static size_t writeData(void *ptr, size_t size, size_t nmemb, void *stream)
{
    std::vector<char> *recvBuffer = (std::vector<char>*)stream;
    size_t sizes = size * nmemb;
    
    // add data to the end of recvBuffer
    // write data maybe called more than once in a single request
    recvBuffer->insert(recvBuffer->end(), (char*)ptr, (char*)ptr+sizes);
    
    return sizes;
}

// Callback function used by libcurl for collect header data
static size_t writeHeaderData(void *ptr, size_t size, size_t nmemb, void *stream)
{
    std::vector<char> *recvBuffer = (std::vector<char>*)stream;
    size_t sizes = size * nmemb;
    
    // add data to the end of recvBuffer
    // write data maybe called more than once in a single request
    recvBuffer->insert(recvBuffer->end(), (char*)ptr, (char*)ptr+sizes);
    
    return sizes;
}

static size_t write_file(void *ptr, size_t size, size_t nmemb, void *stream) {
	size_t written = fwrite(ptr, size, nmemb, (FILE*)stream);
	return written;
}


static int processGetTask(HttpRequest *request, write_callback callback, void *stream, long *errorCode, write_callback headerCallback, void *headerStream, char *errorBuffer);
static int processPostTask(HttpRequest *request, write_callback callback, void *stream, long *errorCode, write_callback headerCallback, void *headerStream, char *errorBuffer);
static int processPutTask(HttpRequest *request, write_callback callback, void *stream, long *errorCode, write_callback headerCallback, void *headerStream, char *errorBuffer);
static int processDeleteTask(HttpRequest *request, write_callback callback, void *stream, long *errorCode, write_callback headerCallback, void *headerStream, char *errorBuffer);
static int processDownloadTask(HttpRequest *request, int32_t *errorCode, write_callback headerCallback, void *headerStream, char* errorBuffer);
static void processResponse(HttpResponse* response, char* errorBuffer);

// Worker thread
void HttpClient::networkThread()
{    
    HttpRequest *request = nullptr;
    
    auto scheduler = Director::getInstance()->getScheduler();
    
    while (true) 
    {
        if (s_need_quit)
        {
            break;
        }
        
        // step 1: send http request if the requestQueue isn't empty
        request = nullptr;
        
        s_requestQueueMutex.lock();
        
        //Get request task from queue
        
        if (!s_requestQueue->empty())
        {
            request = s_requestQueue->front();
			s_requestQueue->pop_front();
        }
        
        s_requestQueueMutex.unlock();
        
        if (nullptr == request)
        {
            // Wait for http request tasks from main thread
            std::unique_lock<std::mutex> lk(s_SleepMutex); 
            s_SleepCondition.wait(lk);
            continue;
        }
        
        // step 2: libcurl sync access
        
        // Create a HttpResponse object, the default setting is http access failed
        HttpResponse *response = new HttpResponse(request);
        
        processResponse(response, s_errorBuffer);
        

        // add response packet into queue
        s_responseQueueMutex.lock();
        s_responseQueue->push_back(response);
        s_responseQueueMutex.unlock();
        
        if (nullptr != s_pHttpClient) {
            scheduler->performFunctionInCocosThread(CC_CALLBACK_0(HttpClient::dispatchResponseCallbacks, this));
        }
    }
    
    // cleanup: if worker thread received quit signal, clean up un-completed request queue
    s_requestQueueMutex.lock();
    s_requestQueue->clear();
    s_requestQueueMutex.unlock();
    
    
    if (s_requestQueue != nullptr) {
        delete s_requestQueue;
        s_requestQueue = nullptr;
        delete s_responseQueue;
        s_responseQueue = nullptr;
    }
    
}

// Worker thread
void HttpClient::networkThreadAlone(HttpRequest* request)
{
    // Create a HttpResponse object, the default setting is http access failed
    HttpResponse *response = new HttpResponse(request);
    char errorBuffer[CURL_ERROR_SIZE] = { 0 };
    processResponse(response, errorBuffer);

    auto scheduler = Director::getInstance()->getScheduler();
    scheduler->performFunctionInCocosThread([response, request]{
        const ccHttpRequestCallback& callback = request->getCallback();
        Ref* pTarget = request->getTarget();
        SEL_HttpResponse pSelector = request->getSelector();

        if (callback != nullptr)
        {
            callback(s_pHttpClient, response);
        }
        else if (pTarget && pSelector)
        {
            (pTarget->*pSelector)(s_pHttpClient, response);
        }
        response->release();
        // do not release in other thread
        request->release();
    });
}

/*int HttpClient::downloadImmediate(const std::string& url, const std::string& path,long& code){
	//set code as 404 first
	code = 404;
	if(url.empty() || path.empty()){
		return -1;
	}
	
	CURL* curl = nullptr;
	//check target status
	std::ofstream fout;
	fout.open(path,std::ios::binary);
	if(!fout){
		return -1;
	}
	fout.close();

	CURLcode _code;

	curl = curl_easy_init();
	int ret = -1;
	if(curl){
		FILE * fp = fopen(path.c_str(),"wb");
		curl_easy_setopt(curl,CURLOPT_URL,url);
		curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,NULL);
		curl_easy_setopt(curl,CURLOPT_WRITEDATA,fp);
		if(CURLE_OK != curl_easy_perform(curl)){
			//fail
		}else{
			_code = curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&code);
			if(_code != CURLE_OK || code >= 200 && code < 300){
				CCLOG("CURL curl_easy_getinfo failed: %s",curl_easy_strerror(_code));
			}
		}
		curl_easy_cleanup(curl);

		//get file size
		fseek(fp,0L,SEEK_END);
		ret = ftell(fp);
		fclose(fp);
	}
	return ret;
}*/

//Configure curl's timeout property
static bool configureCURL(CURL *handle, char *errorBuffer)
{
    if (!handle) {
        return false;
    }
    
    int32_t code;
    code = curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errorBuffer);
    if (code != CURLE_OK) {
        return false;
    }
    code = curl_easy_setopt(handle, CURLOPT_TIMEOUT, HttpClient::getInstance()->getTimeoutForRead());
    if (code != CURLE_OK) {
        return false;
    }
    code = curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, HttpClient::getInstance()->getTimeoutForConnect());
    if (code != CURLE_OK) {
        return false;
    }
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
    
    // FIXED #3224: The subthread of CCHttpClient interrupts main thread if timeout comes.
    // Document is here: http://curl.haxx.se/libcurl/c/curl_easy_setopt.html#CURLOPTNOSIGNAL 
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

    return true;
}

class CURLRaii
{
    /// Instance of CURL
    CURL *_curl;
    /// Keeps custom header data
    curl_slist *_headers;
public:
    CURLRaii()
        : _curl(curl_easy_init())
        , _headers(nullptr)
    {
    }

    ~CURLRaii()
    {
        if (_curl)
            curl_easy_cleanup(_curl);
        /* free the linked list for header data */
        if (_headers)
            curl_slist_free_all(_headers);
    }

	CURL* getCURL(){ return _curl;}

    template <class T>
    bool setOption(CURLoption option, T data)
    {
        return CURLE_OK == curl_easy_setopt(_curl, option, data);
    }

    /**
     * @brief Inits CURL instance for common usage
     * @param request Null not allowed
     * @param callback Response write callback
     * @param stream Response write stream
     */
    bool init(HttpRequest *request, write_callback callback, void *stream, write_callback headerCallback, void *headerStream, char *errorBuffer)
    {
        if (!_curl)
            return false;
        if (!configureCURL(_curl, errorBuffer))
            return false;

        /* get custom header data (if set) */
        std::vector<std::string> headers=request->getHeaders();
        if(!headers.empty())
        {
            /* append custom headers one by one */
            for (std::vector<std::string>::iterator it = headers.begin(); it != headers.end(); ++it)
                _headers = curl_slist_append(_headers,it->c_str());
            /* set custom headers for curl */
            if (!setOption(CURLOPT_HTTPHEADER, _headers))
                return false;
        }
        if (!s_cookieFilename.empty()) {
            if (!setOption(CURLOPT_COOKIEFILE, s_cookieFilename.c_str())) {
                return false;
            }
            if (!setOption(CURLOPT_COOKIEJAR, s_cookieFilename.c_str())) {
                return false;
            }
        }

        return setOption(CURLOPT_URL, request->getUrl())
                && setOption(CURLOPT_WRITEFUNCTION, callback)
                && setOption(CURLOPT_WRITEDATA, stream)
                && setOption(CURLOPT_HEADERFUNCTION, headerCallback)
                && setOption(CURLOPT_HEADERDATA, headerStream);
        
    }

    /// @param responseCode Null not allowed
    bool perform(long *responseCode)
    {
        if (CURLE_OK != curl_easy_perform(_curl))
            return false;
        CURLcode code = curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, responseCode);
        if (code != CURLE_OK || !(*responseCode >= 200 && *responseCode < 300)) {
            CCLOGERROR("Curl curl_easy_getinfo failed: %s", curl_easy_strerror(code));
            return false;
        }
        // Get some mor data.
        
        return true;
    }
};

//Process Get Request
static int processGetTask(HttpRequest *request, write_callback callback, void *stream, long *responseCode, write_callback headerCallback, void *headerStream, char *errorBuffer)
{
    CURLRaii curl;
    bool ok = curl.init(request, callback, stream, headerCallback, headerStream, errorBuffer)
            && curl.setOption(CURLOPT_FOLLOWLOCATION, true)
            && curl.perform(responseCode);
    return ok ? 0 : 1;
}

//Process POST Request
static int processPostTask(HttpRequest *request, write_callback callback, void *stream, long *responseCode, write_callback headerCallback, void *headerStream, char *errorBuffer)
{
    CURLRaii curl;
    bool ok = curl.init(request, callback, stream, headerCallback, headerStream, errorBuffer)
            && curl.setOption(CURLOPT_POST, 1)
            && curl.setOption(CURLOPT_POSTFIELDS, request->getRequestData())
            && curl.setOption(CURLOPT_POSTFIELDSIZE, request->getRequestDataSize())
            && curl.perform(responseCode);
    return ok ? 0 : 1;
}

//Process PUT Request
static int processPutTask(HttpRequest *request, write_callback callback, void *stream, long *responseCode, write_callback headerCallback, void *headerStream, char *errorBuffer)
{
    CURLRaii curl;
    bool ok = curl.init(request, callback, stream, headerCallback, headerStream, errorBuffer)
            && curl.setOption(CURLOPT_CUSTOMREQUEST, "PUT")
            && curl.setOption(CURLOPT_POSTFIELDS, request->getRequestData())
            && curl.setOption(CURLOPT_POSTFIELDSIZE, request->getRequestDataSize())
            && curl.perform(responseCode);
    return ok ? 0 : 1;
}

//Process DELETE Request
static int processDeleteTask(HttpRequest *request, write_callback callback, void *stream, long *responseCode, write_callback headerCallback, void *headerStream, char *errorBuffer)
{
    CURLRaii curl;
    bool ok = curl.init(request, callback, stream, headerCallback, headerStream, errorBuffer)
            && curl.setOption(CURLOPT_CUSTOMREQUEST, "DELETE")
            && curl.setOption(CURLOPT_FOLLOWLOCATION, true)
            && curl.perform(responseCode);
    return ok ? 0 : 1;
}

static int processDownloadTask(HttpRequest *request, write_callback callback, long *responseCode, write_callback headerCallback, void *headerStream, char *errorBuffer)
{
	CURLRaii curl;
	FILE* fp;
	fp = fopen(request->getFilename(), "wb");
	if(!fp){
		return 1;
	}
	bool ok = curl.init(request, callback, fp, headerCallback, headerStream, errorBuffer)
		&& curl.setOption(CURLOPT_CUSTOMREQUEST, "GET")
		&& curl.setOption(CURLOPT_FOLLOWLOCATION, true)
		&& curl.setOption(CURLOPT_TIMEOUT, 240L)
		&& curl.setOption(CURLOPT_CONNECTTIMEOUT, 0L)
		&& curl.perform(responseCode);

	long size = ftell(fp);
	fclose(fp);

	if(!ok){
		remove(request->getFilename());
		(*responseCode) = 600; //force an internal error
		return 1;
	}

	//compare file size and content length
	
	double contentLength = 0;
	auto _curl = curl.getCURL();
	curl_easy_getinfo(_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &contentLength);
	long _contentLength = floor(contentLength + 0.5);

	if(size != _contentLength){
		remove(request->getFilename());
		(*responseCode) = 600; //force an internal error
		return 1;
	}
	
	return 0;
}


// Process Response
static void processResponse(HttpResponse* response, char* errorBuffer)
{
    auto request = response->getHttpRequest();
    long responseCode = -1;
    int retValue = 0;

    // Process the request -> get response packet
    switch (request->getRequestType())
    {
    case HttpRequest::Type::GET: // HTTP GET
        retValue = processGetTask(request,
            writeData, 
            response->getResponseData(), 
            &responseCode,
            writeHeaderData,
            response->getResponseHeader(),
            errorBuffer);
        break;

    case HttpRequest::Type::POST: // HTTP POST
        retValue = processPostTask(request,
            writeData, 
            response->getResponseData(), 
            &responseCode,
            writeHeaderData,
            response->getResponseHeader(),
            errorBuffer);
        break;

    case HttpRequest::Type::PUT:
        retValue = processPutTask(request,
            writeData,
            response->getResponseData(),
            &responseCode,
            writeHeaderData,
            response->getResponseHeader(),
            errorBuffer);
        break;

    case HttpRequest::Type::DELETE:
        retValue = processDeleteTask(request,
            writeData,
            response->getResponseData(),
            &responseCode,
            writeHeaderData,
            response->getResponseHeader(),
            errorBuffer);
        break;
	case HttpRequest::Type::DOWNLOAD:
		retValue = processDownloadTask(request,
			write_file,
			&responseCode,
			writeHeaderData,
			response->getResponseHeader(),
			errorBuffer);
		break;
    default:
        CCASSERT(true, "CCHttpClient: unkown request type, only GET and POSt are supported");
        break;
    }

    // write data to HttpResponse
    response->setResponseCode(responseCode);

    if (retValue != 0) 
    {
        response->setSucceed(false);
        response->setErrorBuffer(errorBuffer);
    }
    else
    {
        response->setSucceed(true);
    }
}

// HttpClient implementation
HttpClient* HttpClient::getInstance()
{
    if (s_pHttpClient == nullptr) {
        s_pHttpClient = new HttpClient();
    }
    
    return s_pHttpClient;
}

void HttpClient::destroyInstance()
{
    CC_SAFE_DELETE(s_pHttpClient);
}

void HttpClient::enableCookies(const char* cookieFile) {
    if (cookieFile) {
        s_cookieFilename = std::string(cookieFile);
    }
    else {
        s_cookieFilename = (FileUtils::getInstance()->getWritablePath() + "cookieFile.txt");
    }
}

HttpClient::HttpClient()
: _timeoutForConnect(30)
, _timeoutForRead(60)
{
}

HttpClient::~HttpClient()
{
    s_need_quit = true;
    
    if (s_requestQueue != nullptr) {
        s_SleepCondition.notify_one();
    }
    
    s_pHttpClient = nullptr;
}

//Lazy create semaphore & mutex & thread
bool HttpClient::lazyInitThreadSemphore()
{
    if (s_requestQueue != nullptr) {
        return true;
    } else {
        
        s_requestQueue = new std::deque<HttpRequest*>;
        s_responseQueue = new std::deque<HttpResponse*>;
        
		s_need_quit = false;
		
        auto t = std::thread(CC_CALLBACK_0(HttpClient::networkThread, this));
        t.detach();
    }
    
    return true;
}

//Add a get task to queue
void HttpClient::send(HttpRequest* request)
{    
    if (false == lazyInitThreadSemphore()) 
    {
        return;
    }
    
    if (!request)
    {
        return;
    }
        
    request->retain();
    
    if (nullptr != s_requestQueue) {
        s_requestQueueMutex.lock();
        s_requestQueue->push_back(request);
        s_requestQueueMutex.unlock();
        
        // Notify thread start to work
        s_SleepCondition.notify_one();
    }
}

void HttpClient::sendImmediate(HttpRequest* request)
{
    if(!request)
    {
        return;
    }

    request->retain();
    auto t = std::thread(&HttpClient::networkThreadAlone, this, request);
    t.detach();
}

// Poll and notify main thread if responses exists in queue
void HttpClient::dispatchResponseCallbacks()
{
    // log("CCHttpClient::dispatchResponseCallbacks is running");
    //occurs when cocos thread fires but the network thread has already quited
    if (nullptr == s_responseQueue) {
        return;
    }
    HttpResponse* response = nullptr;
    
    s_responseQueueMutex.lock();

    if (!s_responseQueue->empty())
    {
        response = s_responseQueue->front();;
        s_responseQueue->pop_front();
    }
    
    s_responseQueueMutex.unlock();
    
    if (response)
    {
        HttpRequest *request = response->getHttpRequest();
        const ccHttpRequestCallback& callback = request->getCallback();
        Ref* pTarget = request->getTarget();
        SEL_HttpResponse pSelector = request->getSelector();

        if (callback != nullptr)
        {
            callback(this, response);
        }
        else if (pTarget && pSelector)
        {
            (pTarget->*pSelector)(this, response);
        }
        
        response->release();
        // do not release in other thread
        request->release();
    }
}

}

NS_CC_END


