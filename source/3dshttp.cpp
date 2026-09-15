#include "3dshttp.h"
#include "3dslog.h"

#include <3ds.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <curl/curl.h>

//---------------------------------------------------------
// Shared network transport. Callbacks are drained on the main thread.
//---------------------------------------------------------

#define HTTP_RESPONSE_INITIAL_CAPACITY 4096
#define HTTP_HTTPC_TIMEOUT_NS (5ULL * 1000 * 1000 * 1000)
#define HTTP_WORKER_JOIN_MS 500
#define HTTP_CURL_REDIRECT_LIMIT 5
#define HTTP_WORKER_STACK_SIZE 0x10000   // mbedtls handshakes run on this stack
#define HTTP_SOC_BUFFER_SIZE 0x100000
#define HTTP_CURL_CONNECT_TIMEOUT_S 10L   // DNS + TCP + TLS handshake
#define HTTP_CURL_STALL_TIMEOUT_S 15L
// Larger writes avoid SD-card stalls.
#define HTTP_FILE_WRITE_BUFFER_SIZE 0x40000

typedef enum {
    HTTP_CALL_MEMORY = 0,
    HTTP_CALL_FILE,
} HttpCallKind;

// Queued request/completion record; request strings follow inline.
typedef struct HttpCall {
    HttpCallKind kind;
    const char *url;
    const char *postData;     // NULL for GET
    const char *contentType;  // NULL when unspecified
    const char *temporaryPath; // HTTP_CALL_FILE only
    const char *headers;      // NULL or "Name: value" lines separated by '\n'
    Http3dsCallback callback;
    void *callbackData;
    Http3dsProgress *progress;
    char *body;               // NULL when the request failed
    u32 bodyLength;
    int httpStatusCode;       // 0 when the request failed or the status was implausible
    bool success;
    bool transportFailed;     // set by httpSetTransportError
    struct HttpCall *next;
    char strings[];
} HttpCall;

static bool       httpReady = false;
static Thread     httpWorker = NULL;
static bool       httpWorkerRunning = false;
static httpcContext httpActiveContext;
static bool       httpContextLive = false;
static bool       httpShuttingDown = false;
static LightLock  httpTransferLock;
static char       httpUserAgent[256] = {0};
static LightLock  httpRequestLock;
static CondVar    httpRequestCond;
static HttpCall   *httpRequestHead = NULL, *httpRequestTail = NULL;
static LightLock  httpCompletionLock;
static HttpCall   *httpCompletionHead = NULL, *httpCompletionTail = NULL;
static bool       httpCurlLive = false;       // guarded by httpTransferLock
static volatile bool httpCurlCancel = false;
static CURL       *httpCurl = NULL;           // worker-owned; keeps connections until shutdown
static u32        *httpSocBuffer = NULL;      // NULL when SOC is unavailable

// Copies into the flexible tail storage.
static char *httpCopyString(char **cursor, const char *s)
{
    if(!s)
        return NULL;
    char *dst = *cursor;
    size_t n = strlen(s) + 1;
    memcpy(dst, s, n);
    *cursor += n;
    return dst;
}

static void httpFreeCall(HttpCall *call)
{
    free(call->body);
    free(call);
}

static void httpFreeList(HttpCall *call)
{
    while(call) {
        HttpCall *next = call->next;
        httpFreeCall(call);
        call = next;
    }
}

// Caller holds the lock guarding this list.
static void httpListPushLocked(HttpCall **head, HttpCall **tail, HttpCall *call)
{
    call->next = NULL;
    if(*tail)
        (*tail)->next = call;
    else
        *head = call;
    *tail = call;
}

static void httpQueueCompletion(HttpCall *call)
{
    LightLock_Lock(&httpCompletionLock);
    httpListPushLocked(&httpCompletionHead, &httpCompletionTail, call);
    LightLock_Unlock(&httpCompletionLock);
}

static HttpCall *httpDetachCompletions()
{
    LightLock_Lock(&httpCompletionLock);
    HttpCall *call = httpCompletionHead;
    httpCompletionHead = httpCompletionTail = NULL;
    LightLock_Unlock(&httpCompletionLock);
    return call;
}

static void httpQueueRequest(HttpCall *call)
{
    LightLock_Lock(&httpRequestLock);
    httpListPushLocked(&httpRequestHead, &httpRequestTail, call);
    CondVar_Signal(&httpRequestCond);
    LightLock_Unlock(&httpRequestLock);
}

static HttpCall *httpPopRequestLocked()
{
    HttpCall *call = httpRequestHead;
    if(call) {
        httpRequestHead = call->next;
        if(!httpRequestHead)
            httpRequestTail = NULL;
    }
    return call;
}

// httpcDownloadData with a timeout per receive.
static Result httpDownloadDataTimeout(httpcContext *context, u8 *buffer, u32 size, u32 *downloadedsize, u64 timeout)
{
    if(downloadedsize)
        *downloadedsize = 0;

    u32 dlStart = 0;
    Result ret = httpcGetDownloadSizeState(context, &dlStart, NULL);
    if(R_FAILED(ret))
        return ret;

    u32 pos = 0;
    Result dlret = (Result)HTTPC_RESULTCODE_DOWNLOADPENDING;
    while(pos < size && dlret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) {
        dlret = httpcReceiveDataTimeout(context, &buffer[pos], size - pos, timeout);

        u32 dlPos = 0;
        ret = httpcGetDownloadSizeState(context, &dlPos, NULL);
        if(R_FAILED(ret))
            return ret;

        pos = (dlPos > dlStart) ? dlPos - dlStart : 0;
        if(pos > size) pos = size;
    }

    if(downloadedsize)
        *downloadedsize = pos;
    return dlret;
}

static void httpCloseActiveContext(bool cancel)
{
    LightLock_Lock(&httpTransferLock);
    httpContextLive = false;
    LightLock_Unlock(&httpTransferLock);

    // libctru says CloseContext can hang before the full body is read.
    if(cancel)
        httpcCancelConnection(&httpActiveContext);
    httpcCloseContext(&httpActiveContext);
}

// No-op before the worker opens the context.
void http3dsCancelActiveRequest(void)
{
    LightLock_Lock(&httpTransferLock);
    if(httpContextLive) {
        httpcCancelConnection(&httpActiveContext);
    }
    if(httpCurlLive)
        httpCurlCancel = true;
    LightLock_Unlock(&httpTransferLock);
}

// Shutdown also blocks the next context from opening.
static void httpBeginShutdown(void)
{
    LightLock_Lock(&httpTransferLock);
    httpShuttingDown = true;
    LightLock_Unlock(&httpTransferLock);

    http3dsCancelActiveRequest();
}

static void httpSetTransportError(HttpCall *call, const char *message, Result result = 0)
{
    call->transportFailed = true;
    if(result)
        log3dsWrite("[HTTP] %s (0x%08lX)", message, (unsigned long)result);
    else
        log3dsWrite("[HTTP] %s", message);

    call->body = strdup(message);
    if(!call->body)
        return;   // stays a bare "no response"

    call->bodyLength = strlen(call->body);
}

static bool httpReadResponse(httpcContext *context, HttpCall *call)
{
    u32 statusCode = 0;
    Result result = httpcGetResponseStatusCodeTimeout(context, &statusCode, HTTP_HTTPC_TIMEOUT_NS);
    if(R_FAILED(result)) {
        httpSetTransportError(call, "Connection timed out", result);
        return false;
    }
    // Citra can report 0xFFFFFFFF here. Map invalid HTTP statuses to 0 so
    // conversion to int cannot collide with rcheevos' negative client-error codes.
    if(statusCode < 100 || statusCode > 599) {
        log3dsWrite("[HTTP] implausible status %lu, treating as no response", (unsigned long)statusCode);
        statusCode = 0;
    }

    // Content-Length seems unreliable for chunked RA payloads
    // (httpcGetDownloadSizeState returns contentsize 0)
    u32 cap = HTTP_RESPONSE_INITIAL_CAPACITY;
    char *body = (char *)malloc(cap);
    if(!body)
        return false;

    u32 totalRead = 0;
    Result ret = 0;
    do {
        if(totalRead + 4096 > cap) {
            char *grown = (char *)realloc(body, cap * 2);
            if(!grown) {
                free(body);
                return false;
            }
            body = grown;
            cap *= 2;
        }
        u32 readSize = 0;
        ret = httpDownloadDataTimeout(context, (u8 *)(body + totalRead), cap - totalRead - 1, &readSize, HTTP_HTTPC_TIMEOUT_NS);
        totalRead += readSize;
    } while(ret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

    if(R_FAILED(ret)) {
        free(body);
        httpSetTransportError(call, "Connection lost", ret);
        return false;
    }

    body[totalRead] = '\0';
    call->body = body;
    call->bodyLength = totalRead;
    call->httpStatusCode = (int)statusCode;
    return true;
}

// Copies the next '\n'-separated header line; NULL when none are left.
static const char *httpNextHeader(const char *cursor, char *line, size_t lineSize)
{
    if(!cursor || !*cursor)
        return NULL;
    const char *end = strchr(cursor, '\n');
    size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
    if(length >= lineSize)
        length = lineSize - 1;
    memcpy(line, cursor, length);
    line[length] = '\0';
    return end ? end + 1 : cursor + strlen(cursor);
}

// Performs a request through httpc.
static void httpPerformHttpc(HttpCall *call)
{
    httpcContext *context = &httpActiveContext;
    HTTPC_RequestMethod method = call->postData ? HTTPC_METHOD_POST : HTTPC_METHOD_GET;
    Result result = httpcOpenContext(context, method, call->url, 1);
    if(R_FAILED(result)) {
        httpSetTransportError(call, "Could not open connection", result);
        return;
    }

    LightLock_Lock(&httpTransferLock);
    if(httpShuttingDown) {
        LightLock_Unlock(&httpTransferLock);
        httpcCloseContext(context);
        return;
    }
    httpContextLive = true;
    LightLock_Unlock(&httpTransferLock);

    httpcSetKeepAlive(context, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(context, "Connection", "Keep-Alive");
    httpcAddRequestHeaderField(context, "User-Agent", httpUserAgent);

    char line[256];
    for(const char *h = call->headers; (h = httpNextHeader(h, line, sizeof(line))); ) {
        char *value = strchr(line, ':');
        if(!value)
            continue;
        *value++ = '\0';
        while(*value == ' ')
            value++;
        httpcAddRequestHeaderField(context, line, value);
    }

    if(call->postData) {
        httpcAddRequestHeaderField(context, "Content-Type",
            call->contentType ? call->contentType : "application/x-www-form-urlencoded");
        httpcAddPostDataRaw(context, (const u32 *)call->postData, (u32)strlen(call->postData));
    }

    // No body bytes are pending before BeginRequest succeeds.
    result = httpcBeginRequest(context);
    if(R_FAILED(result)) {
        httpSetTransportError(call, "Could not reach the server", result);
        httpCloseActiveContext(false);
        return;
    }

    // Failures after BeginRequest may leave unread body bytes.
    bool ok = httpReadResponse(context, call);
    call->success = ok && call->httpStatusCode >= 200 && call->httpStatusCode < 300;
    httpCloseActiveContext(!ok);
}

// SOC is process-wide, so curl and badge downloads share one session.
static void httpSocInit(void)
{
    httpSocBuffer = (u32 *)memalign(0x1000, HTTP_SOC_BUFFER_SIZE);
    if(httpSocBuffer && R_FAILED(socInit(httpSocBuffer, HTTP_SOC_BUFFER_SIZE))) {
        free(httpSocBuffer);
        httpSocBuffer = NULL;
    }
    if(!httpSocBuffer)
        log3dsWrite("[HTTP] SOC init failed; HTTPS and badge downloads disabled");
}

// Callers must have closed their sockets.
static void httpSocExit(void)
{
    if(!httpSocBuffer)
        return;
    socExit();
    free(httpSocBuffer);
    httpSocBuffer = NULL;
}

bool http3dsSocIsReady(void)
{
    return httpSocBuffer != NULL;
}

// The 3DS SSL module cannot handshake with GitHub (0xD8A0A03C). Peer
// verification is off by choice for now; no CA bundle is shipped.
typedef struct HttpCurlSink {
    HttpCall *call;
    FILE *out;      // HTTP_CALL_FILE only
    char *body;     // HTTP_CALL_MEMORY only
    u32 length;
    u32 cap;
    bool writeFailed;
} HttpCurlSink;

static size_t httpCurlWrite(char *data, size_t size, size_t count, void *user)
{
    HttpCurlSink *sink = (HttpCurlSink *)user;
    size_t n = size * count;
    if(sink->out) {
        if(fwrite(data, 1, n, sink->out) != n) {
            sink->writeFailed = true;
            return 0;
        }
    } else {
        if(sink->length + n + 1 > sink->cap) {
            u32 cap = sink->cap ? sink->cap : HTTP_RESPONSE_INITIAL_CAPACITY;
            while(sink->length + n + 1 > cap)
                cap *= 2;
            char *grown = (char *)realloc(sink->body, cap);
            if(!grown) {
                sink->writeFailed = true;
                return 0;
            }
            sink->body = grown;
            sink->cap = cap;
        }
        memcpy(sink->body + sink->length, data, n);
    }
    sink->length += n;
    if(sink->call->progress)
        sink->call->progress->downloaded = sink->length;
    return n;
}

static int httpCurlProgress(void *user, curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow)
{
    (void)dlnow; (void)ultotal; (void)ulnow;
    HttpCall *call = (HttpCall *)user;
    if(call->progress && dltotal > 0)
        call->progress->total = (u32)dltotal;
    bool cancelled = httpCurlCancel || (call->progress && call->progress->cancelRequested);
    return cancelled ? 1 : 0;
}

static const char *httpCurlErrorMessage(CURLcode code)
{
    switch(code) {
    case CURLE_ABORTED_BY_CALLBACK:  return "Request cancelled";
    case CURLE_OPERATION_TIMEDOUT:   return "Connection timed out";
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:      return "Could not reach the server";
    case CURLE_HTTP_RETURNED_ERROR:  return "Server returned an error";
    default:                         return "Connection lost";
    }
}

// Dropping the handle closes its cached connections before SOC goes away.
static void httpCurlRelease(void)
{
    if(!httpCurl)
        return;
    curl_easy_cleanup(httpCurl);
    httpCurl = NULL;
}

static void httpPerformCurl(HttpCall *call)
{
    if(!http3dsSocIsReady()) {
        httpSetTransportError(call, "Network unavailable");
        return;
    }
    if(!httpCurl) {
        httpCurl = curl_easy_init();
        if(!httpCurl) {
            httpSetTransportError(call, "Out of memory");
            return;
        }
    }

    // Cancelled while still queued behind another request.
    if(call->progress && call->progress->cancelRequested) {
        httpSetTransportError(call, "Request cancelled");
        return;
    }

    HttpCurlSink sink = {};
    sink.call = call;
    if(call->kind == HTTP_CALL_FILE) {
        sink.out = fopen(call->temporaryPath, "wb");
        if(!sink.out) {
            httpSetTransportError(call, "Could not create temporary file");
            return;
        }
        setvbuf(sink.out, NULL, _IOFBF, HTTP_FILE_WRITE_BUFFER_SIZE);
    }

    LightLock_Lock(&httpTransferLock);
    if(httpShuttingDown) {
        LightLock_Unlock(&httpTransferLock);
        if(sink.out) {
            fclose(sink.out);
            remove(call->temporaryPath);
        }
        return;
    }
    httpCurlLive = true;
    httpCurlCancel = false;
    LightLock_Unlock(&httpTransferLock);

    // Preserve curl's connection cache between requests.
    CURL *curl = httpCurl;
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, call->url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, httpUserAgent[0] ? httpUserAgent : "snes9x_3ds");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, (long)HTTP_CURL_REDIRECT_LIMIT);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, HTTP_CURL_CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, HTTP_CURL_STALL_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    // File downloads fail instead of saving HTTP error bodies.
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, sink.out ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, httpCurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, httpCurlProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, call);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    struct curl_slist *headers = NULL;
    char line[256];
    for(const char *h = call->headers; (h = httpNextHeader(h, line, sizeof(line))); ) {
        struct curl_slist *next = curl_slist_append(headers, line);
        if(next)
            headers = next;
    }
    if(call->postData) {
        snprintf(line, sizeof(line), "Content-Type: %s",
                 call->contentType ? call->contentType : "application/x-www-form-urlencoded");
        struct curl_slist *next = curl_slist_append(headers, line);
        if(next)
            headers = next;
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, call->postData);
    }
    if(headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);

    LightLock_Lock(&httpTransferLock);
    httpCurlLive = false;
    LightLock_Unlock(&httpTransferLock);

    const char *error = NULL;
    if(sink.writeFailed) {
        error = sink.out ? "Could not write temporary file" : "Out of memory";
    } else if(code != CURLE_OK) {
        log3dsWrite("[HTTP] curl: %s", curl_easy_strerror(code));
        error = httpCurlErrorMessage(code);
    }
    if(sink.out && fclose(sink.out) != 0 && !error)
        error = "Could not write temporary file";
    if(!error && !sink.out && !sink.body) {
        sink.body = (char *)calloc(1, 1);   // empty 2xx body
        if(!sink.body)
            error = "Out of memory";
    }

    call->httpStatusCode = (status >= 100 && status <= 599) ? (int)status : 0;
    if(error) {
        if(code != CURLE_HTTP_RETURNED_ERROR)
            call->httpStatusCode = 0;
        free(sink.body);
        if(sink.out)
            remove(call->temporaryPath);
        httpSetTransportError(call, error);
        return;
    }

    if(!sink.out) {
        sink.body[sink.length] = '\0';
        call->body = sink.body;
    }
    call->bodyLength = sink.length;
    call->success = call->httpStatusCode >= 200 && call->httpStatusCode < 300;
    if(!call->success && sink.out)
        remove(call->temporaryPath);
}

static void httpWorkerMain(void *arg)
{
    (void)arg;

    LightLock_Lock(&httpRequestLock);
    while(httpWorkerRunning) {
        HttpCall *call = httpPopRequestLocked();
        if(!call) {
            CondVar_Wait(&httpRequestCond, &httpRequestLock);
            continue;
        }
        LightLock_Unlock(&httpRequestLock);

        // httpc handles plain HTTP only; use curl for HTTPS and file transfers.
        if(call->kind == HTTP_CALL_FILE || !strncmp(call->url, "https://", 8))
            httpPerformCurl(call);
        else
            httpPerformHttpc(call);
        httpQueueCompletion(call);

        LightLock_Lock(&httpRequestLock);
    }
    LightLock_Unlock(&httpRequestLock);
    httpCurlRelease();
}

static bool httpQueueCall(HttpCallKind kind, const char *url, const char *postData,
                            const char *contentType, const char *headers,
                            const char *temporaryPath, Http3dsProgress *progress,
                            Http3dsCallback callback, void *callbackData)
{
    if(!httpReady || !url || !url[0] || !callback)
        return false;

    size_t urlLen = strlen(url) + 1;
    size_t postLen = postData ? strlen(postData) + 1 : 0;
    size_t contentTypeLen = contentType ? strlen(contentType) + 1 : 0;
    size_t headersLen = headers ? strlen(headers) + 1 : 0;
    size_t pathLen = temporaryPath ? strlen(temporaryPath) + 1 : 0;
    HttpCall *call = (HttpCall *)malloc(sizeof(HttpCall) + urlLen + postLen + contentTypeLen
                                        + headersLen + pathLen);
    if(!call)
        return false;

    char *cursor = call->strings;
    call->url = httpCopyString(&cursor, url);
    call->postData = httpCopyString(&cursor, postData);
    call->contentType = httpCopyString(&cursor, contentType);
    call->headers = httpCopyString(&cursor, headers);
    call->temporaryPath = httpCopyString(&cursor, temporaryPath);
    call->kind = kind;
    call->callback = callback;
    call->callbackData = callbackData;
    call->progress = progress;
    call->body = NULL;
    call->bodyLength = 0;
    call->httpStatusCode = 0;
    call->success = false;
    call->transportFailed = false;
    if(progress) {
        progress->downloaded = 0;
        progress->total = 0;
        progress->cancelRequested = false;
    }
    httpQueueRequest(call);
    return true;
}

bool http3dsRequest(const char *url, const char *postData, const char *contentType,
                    const char *headers, Http3dsCallback callback, void *callbackData)
{
    return httpQueueCall(HTTP_CALL_MEMORY, url, postData, contentType, headers,
                           NULL, NULL, callback, callbackData);
}

bool http3dsGet(const char *url, Http3dsCallback callback, void *callbackData)
{
    return http3dsRequest(url, NULL, NULL, NULL, callback, callbackData);
}

bool http3dsDownloadFile(const char *url, const char *temporaryPath,
                         Http3dsProgress *progress,
                         Http3dsCallback callback, void *callbackData)
{
    if(!temporaryPath || !temporaryPath[0])
        return false;
    return httpQueueCall(HTTP_CALL_FILE, url, NULL, NULL, NULL, temporaryPath,
                           progress, callback, callbackData);
}

void http3dsDrainCompletions()
{
    HttpCall *call = httpDetachCompletions();
    while(call) {
        HttpCall *next = call->next;

        Http3dsResponse response = {};
        response.httpStatusCode = call->httpStatusCode;
        response.success = call->success;
        response.transportFailed = call->transportFailed;
        response.downloaded = call->bodyLength;
        response.body = call->body;
        response.bodyLength = call->bodyLength;
        call->callback(&response, call->callbackData);

        httpFreeCall(call);
        call = next;
    }
}

static void httpFreeQueues()
{
    httpFreeList(httpRequestHead);
    httpRequestHead = httpRequestTail = NULL;

    httpFreeList(httpCompletionHead);
    httpCompletionHead = httpCompletionTail = NULL;
}

void http3dsSetUserAgent(const char *userAgent)
{
    snprintf(httpUserAgent, sizeof(httpUserAgent), "%s", userAgent ? userAgent : "");
}

bool http3dsInitialize(void)
{
    if(httpReady)
        return true;
    if(R_FAILED(httpcInit(0x1000)))
        return false;
    httpReady = true;

    if(curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        log3dsWrite("[HTTP] curl init failed; HTTPS requests will fail");

    httpSocInit();
    LightLock_Init(&httpTransferLock);
    LightLock_Init(&httpRequestLock);
    LightLock_Init(&httpCompletionLock);
    CondVar_Init(&httpRequestCond);
    httpContextLive = false;
    httpShuttingDown = false;

    httpWorkerRunning = true;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    httpWorker = threadCreate(httpWorkerMain, NULL, HTTP_WORKER_STACK_SIZE, prio + 1, 0, false);

    if(!httpWorker) {
        httpWorkerRunning = false;
        http3dsFinalize();
        return false;
    }

    return true;
}

void http3dsFinalize(void)
{
    if(!httpReady)
        return;

    // stop the worker, then discard any queued work (no callbacks at shutdown)
    if(httpWorker) {
        LightLock_Lock(&httpRequestLock);
        httpWorkerRunning = false;
        CondVar_Signal(&httpRequestCond);
        LightLock_Unlock(&httpRequestLock);

        // Wake an idle worker and cancel active I/O.
        httpBeginShutdown();

        // Only 0 means joined; RD_TIMEOUT is positive, so R_FAILED would miss it.
        if(threadJoin(httpWorker, HTTP_WORKER_JOIN_MS * 1000000ULL) != 0)
            return;

        threadFree(httpWorker);
        httpWorker = NULL;
    }

    httpFreeQueues();

    httpSocExit();   // after the worker dropped curl's connections
    curl_global_cleanup();
    httpcExit();
    httpReady = false;
}

bool http3dsIsReady(void)
{
    return httpReady;
}
