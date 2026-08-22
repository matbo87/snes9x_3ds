#include "3dsra_http.h"

#include "rc_client.h"

#include <3ds.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

//---------------------------------------------------------
// Network transport.
// rc_client callbacks are drained on the main thread.
//---------------------------------------------------------

#define RA_RESPONSE_INITIAL_CAP 4096
#define RA_HTTP_TIMEOUT_NS (5ULL * 1000 * 1000 * 1000)
#define RA_HTTP_WORKER_JOIN_MS 500

typedef struct RaResponse {
    rc_client_server_callback_t callback;
    void *callbackData;
    char *body;
    u32 bodyLength;
    int httpStatusCode;
    struct RaResponse *next;
} RaResponse;

typedef struct RaRequest {
    char *url;
    char *postData;
    char *contentType;
    RaResponse *response;
    rc_client_server_callback_t callback;
    void *callbackData;
    struct RaRequest *next;
} RaRequest;

static bool       raHttpReady = false;
static Thread     raWorker = NULL;
static bool       raWorkerRunning = false;
static httpcContext raActiveContext;
static bool       raContextLive = false;
static bool       raContextClosed = false;
static LightLock  raContextLock;
static char       raUserAgent[256] = {0};
static LightLock  raRequestLock;
static CondVar    raRequestCond;
static RaRequest  *raRequestHead = NULL, *raRequestTail = NULL;
static LightLock  raCompletionLock;
static RaResponse *raCompletionHead = NULL, *raCompletionTail = NULL;

static char *raStrdup(const char *s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if(p)
        memcpy(p, s, n);
    return p;
}

static void raFreeRequest(RaRequest *req)
{
    if(!req)
        return;
    free(req->url);
    free(req->postData);
    free(req->contentType);
    free(req->response); // struct only: an unprocessed request has no body yet
    free(req);
}

static void raFreeResponse(RaResponse *resp)
{
    free(resp->body);
    free(resp);
}

static void raQueueCompletion(RaResponse *resp)
{
    resp->next = NULL;
    LightLock_Lock(&raCompletionLock);
    if(raCompletionTail)
        raCompletionTail->next = resp;
    else
        raCompletionHead = resp;
    raCompletionTail = resp;
    LightLock_Unlock(&raCompletionLock);
}

static RaResponse *raDetachCompletions()
{
    LightLock_Lock(&raCompletionLock);
    RaResponse *resp = raCompletionHead;
    raCompletionHead = raCompletionTail = NULL;
    LightLock_Unlock(&raCompletionLock);
    return resp;
}

static void raQueueRequest(RaRequest *req)
{
    LightLock_Lock(&raRequestLock);
    if(raRequestTail)
        raRequestTail->next = req;
    else
        raRequestHead = req;
    raRequestTail = req;
    CondVar_Signal(&raRequestCond);
    LightLock_Unlock(&raRequestLock);
}

static RaRequest *raPopRequestLocked()
{
    RaRequest *req = raRequestHead;
    if(req) {
        raRequestHead = req->next;
        if(!raRequestHead)
            raRequestTail = NULL;
    }
    return req;
}

// httpcDownloadData with a timeout per receive.
static Result raDownloadDataTimeout(httpcContext *context, u8 *buffer, u32 size, u32 *downloadedsize, u64 timeout)
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

        pos = dlPos - dlStart;
    }

    if(downloadedsize)
        *downloadedsize = pos;
    return dlret;
}

static void raCloseActiveContext(bool cancel)
{
    LightLock_Lock(&raContextLock);
    raContextLive = false;
    LightLock_Unlock(&raContextLock);

    // libctru says CloseContext can hang before the full body is read.
    if(cancel)
        httpcCancelConnection(&raActiveContext);
    httpcCloseContext(&raActiveContext);
}

// No-op before the worker opens the context.
void raHttpCancelActiveRequest(void)
{
    LightLock_Lock(&raContextLock);
    if(raContextLive)
        httpcCancelConnection(&raActiveContext);
    LightLock_Unlock(&raContextLock);
}

// Shutdown also blocks the next context from opening.
static void raCancelLiveContext(void)
{
    LightLock_Lock(&raContextLock);
    raContextClosed = true;
    LightLock_Unlock(&raContextLock);

    raHttpCancelActiveRequest();
}

static bool raHttpPerform(const char *url, const char *postData, const char *contentType,
                          char **bufPtr, u32 *capPtr, u32 *lenOut, int *statusOut)
{
    *lenOut = 0;
    *statusOut = 0;

    httpcContext *context = &raActiveContext;
    HTTPC_RequestMethod method = postData ? HTTPC_METHOD_POST : HTTPC_METHOD_GET;
    if(R_FAILED(httpcOpenContext(context, method, url, 1)))
        return false;

    LightLock_Lock(&raContextLock);
    if(raContextClosed) {
        LightLock_Unlock(&raContextLock);
        httpcCloseContext(context);
        return false;
    }
    raContextLive = true;
    LightLock_Unlock(&raContextLock);

    httpcSetKeepAlive(context, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(context, "Connection", "Keep-Alive");
    httpcAddRequestHeaderField(context, "User-Agent", raUserAgent);

    if(postData) {
        httpcAddRequestHeaderField(context, "Content-Type",
            contentType ? contentType : "application/x-www-form-urlencoded");
        httpcAddPostDataRaw(context, (const u32 *)postData, (u32)strlen(postData));
    }

    // The response/body waits below carry the request timeout
    if(R_FAILED(httpcBeginRequest(context))) {
        raCloseActiveContext(false);
        return false;
    }

    u32 statusCode = 0;
    if(R_FAILED(httpcGetResponseStatusCodeTimeout(context, &statusCode, RA_HTTP_TIMEOUT_NS))) {
        raCloseActiveContext(true);
        return false;
    }

    if(!*bufPtr) {
        *bufPtr = (char *)malloc(RA_RESPONSE_INITIAL_CAP);
        if(!*bufPtr) {
            raCloseActiveContext(true);
            return false;
        }
        *capPtr = RA_RESPONSE_INITIAL_CAP;
    }

    u32 totalRead = 0;
    Result ret = 0;
    bool growFailed = false;
    do {
        if(totalRead + 4096 > *capPtr) {
            char *grown = (char *)realloc(*bufPtr, *capPtr * 2);
            if(!grown) {
                growFailed = true;
                break;
            }
            *bufPtr = grown;
            *capPtr *= 2;
        }
        u32 readSize = 0;
        ret = raDownloadDataTimeout(context, (u8 *)(*bufPtr + totalRead), *capPtr - totalRead - 1, &readSize, RA_HTTP_TIMEOUT_NS);
        totalRead += readSize;
    } while(ret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

    if(growFailed || R_FAILED(ret)) {
        raCloseActiveContext(true);
        return false;
    }

    raCloseActiveContext(false);

    (*bufPtr)[totalRead] = '\0';
    *lenOut = totalRead;
    *statusOut = (int)statusCode;
    return true;
}

// rc_client reads a zero status as a transport failure.
static void raReportCallFailed(rc_client_server_callback_t callback, void *callbackData)
{
    rc_api_server_response_t response;
    memset(&response, 0, sizeof(response));
    callback(&response, callbackData);
}

static void raWorkerMain(void *arg)
{
    (void)arg;

    LightLock_Lock(&raRequestLock);
    while(raWorkerRunning) {
        RaRequest *req = raPopRequestLocked();
        if(!req) {
            CondVar_Wait(&raRequestCond, &raRequestLock);
            continue;
        }
        LightLock_Unlock(&raRequestLock);

        char *body = NULL;
        u32 cap = 0, len = 0;
        int status = 0;
        bool ok = raHttpPerform(req->url, req->postData, req->contentType, &body, &cap, &len, &status);

        if(!ok)
            free(body);

        RaResponse *resp = req->response;
        req->response = NULL;
        resp->callback = req->callback;
        resp->callbackData = req->callbackData;
        resp->body = ok ? body : NULL;
        resp->bodyLength = ok ? len : 0;
        resp->httpStatusCode = ok ? status : 0;
        raQueueCompletion(resp);

        raFreeRequest(req);
        LightLock_Lock(&raRequestLock);
    }
    LightLock_Unlock(&raRequestLock);
}

void raServerCall(const rc_api_request_t *request,
                  rc_client_server_callback_t callback,
                  void *callbackData, rc_client_t *client)
{
    (void)client;

    char *url = raStrdup(request->url);
    RaRequest *req = url ? (RaRequest *)malloc(sizeof(RaRequest)) : NULL;
    RaResponse *resp = req ? (RaResponse *)malloc(sizeof(RaResponse)) : NULL;
    if(!req || !resp) {
        free(url);
        free(req);
        raReportCallFailed(callback, callbackData);
        return;
    }

    req->url = url;
    req->postData = raStrdup(request->post_data);
    req->contentType = raStrdup(request->content_type);
    req->response = resp;
    req->callback = callback;
    req->callbackData = callbackData;
    req->next = NULL;

    if((request->post_data && !req->postData) || (request->content_type && !req->contentType)) {
        raFreeRequest(req);
        raReportCallFailed(callback, callbackData);
        return;
    }

    raQueueRequest(req);
}

void raDrainCompletions()
{
    RaResponse *resp = raDetachCompletions();
    while(resp) {
        RaResponse *next = resp->next;

        rc_api_server_response_t response;
        memset(&response, 0, sizeof(response));
        response.http_status_code = resp->httpStatusCode;
        response.body = resp->body;
        response.body_length = resp->bodyLength;
        resp->callback(&response, resp->callbackData);

        raFreeResponse(resp);
        resp = next;
    }
}

static void raFreeQueues()
{
    RaRequest *req = raRequestHead;
    while(req) {
        RaRequest *next = req->next;
        raFreeRequest(req);
        req = next;
    }
    raRequestHead = raRequestTail = NULL;

    RaResponse *resp = raCompletionHead;
    while(resp) {
        RaResponse *next = resp->next;
        raFreeResponse(resp);
        resp = next;
    }
    raCompletionHead = raCompletionTail = NULL;
}

void raHttpSetUserAgent(const char *userAgent)
{
    snprintf(raUserAgent, sizeof(raUserAgent), "%s", userAgent ? userAgent : "");
}

bool raHttpInitialize(void)
{
    if(R_FAILED(httpcInit(0x1000)))
        return false;
    raHttpReady = true;

    LightLock_Init(&raContextLock);
    LightLock_Init(&raRequestLock);
    LightLock_Init(&raCompletionLock);
    CondVar_Init(&raRequestCond);
    raContextLive = false;
    raContextClosed = false;

    raWorkerRunning = true;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    raWorker = threadCreate(raWorkerMain, NULL, 0x4000, prio + 1, 0, false);

    if(!raWorker) {
        raWorkerRunning = false;
        raHttpFinalize();
        return false;
    }

    return true;
}

void raHttpFinalize(void)
{
    if(!raHttpReady)
        return;

    // stop the worker, then discard any queued work (no callbacks at shutdown)
    bool workerStuck = false;
    if(raWorker) {
        LightLock_Lock(&raRequestLock);
        raWorkerRunning = false;
        CondVar_Signal(&raRequestCond);
        LightLock_Unlock(&raRequestLock);

        // The worker checks raWorkerRunning between requests; cancel any in-flight I/O
        raCancelLiveContext();

        // A timed-out join returns RD_TIMEOUT, which is not negative: R_FAILED misses it.
        if(threadJoin(raWorker, RA_HTTP_WORKER_JOIN_MS * 1000000ULL) != 0)
            workerStuck = true;
        else {
            threadFree(raWorker);
            raWorker = NULL;
        }
    }

    // A live worker still owns its queue entry, the shared context and the httpc session.
    if(workerStuck)
        return;

    raFreeQueues();

    httpcExit();
    raHttpReady = false;
}
