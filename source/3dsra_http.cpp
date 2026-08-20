#include "3dsra_http.h"

#include "rc_client.h"

#include <3ds.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

//---------------------------------------------------------
// Network transport.
// rc_client callbacks are drained on the main thread.
// Synchronous menu/load paths use raSyncMode.
//---------------------------------------------------------

#define RA_RESPONSE_INITIAL_CAP (128 * 1024)
// httpcBeginRequest has no timeout parameter, so a watchdog enforces this budget.
#define RA_HTTP_TIMEOUT_NS (5ULL * 1000 * 1000 * 1000)
#define RA_HTTP_TIMEOUT_MS (RA_HTTP_TIMEOUT_NS / 1000000ULL)

static char *raResponseBuf = NULL;
static u32   raResponseCap = 0;

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

static Thread     raWorker = NULL;
static bool       raWorkerRunning = false;
static Thread     raRequestTimeoutThread = NULL;
static bool       raRequestTimeoutRunning = false;
static LightEvent raRequestArmed;
static LightEvent raRequestFinished;
// raHttpLock serializes raHttpPerform, so one context covers every request.
static httpcContext raActiveContext;
static bool       raContextLive = false;
static u64        raContextDeadline = 0;
static LightLock  raContextLock;
static bool       raSyncMode = false;
static char       raUserAgent[256] = {0};
static LightLock  raHttpLock;
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

static void raDisarmRequestTimeout()
{
    LightLock_Lock(&raContextLock);
    raContextDeadline = 0;
    LightLock_Unlock(&raContextLock);
    LightEvent_Signal(&raRequestFinished);
}

static void raCloseActiveContext(bool cancel)
{
    LightLock_Lock(&raContextLock);
    raContextLive = false;
    raContextDeadline = 0;
    LightLock_Unlock(&raContextLock);
    LightEvent_Signal(&raRequestFinished);

    // libctru says CloseContext can hang before the full body is read.
    if(cancel)
        httpcCancelConnection(&raActiveContext);
    httpcCloseContext(&raActiveContext);
}

static void raRequestTimeoutMain(void *arg)
{
    (void)arg;

    while(raRequestTimeoutRunning) {
        LightEvent_Wait(&raRequestArmed);
        if(!raRequestTimeoutRunning)
            break;

        while(raRequestTimeoutRunning) {
            LightLock_Lock(&raContextLock);
            u64 deadline = raContextLive ? raContextDeadline : 0;
            LightLock_Unlock(&raContextLock);
            if(!deadline)
                break;

            u64 now = osGetTime();
            s64 remainNs = now < deadline ? (s64)((deadline - now) * 1000000ULL) : 0;
            if(LightEvent_WaitTimeout(&raRequestFinished, remainNs) == 0)
                continue;

            // The finished signal may belong to the previous context.
            LightLock_Lock(&raContextLock);
            bool expired = raContextLive && raContextDeadline && osGetTime() >= raContextDeadline;
            if(expired)
                httpcCancelConnection(&raActiveContext);
            LightLock_Unlock(&raContextLock);
            if(expired)
                break;
        }
    }
}

// Caller holds raHttpLock.
static bool raHttpPerform(const char *url, const char *postData, const char *contentType,
                          char **bufPtr, u32 *capPtr, u32 initialCap, u32 *lenOut, int *statusOut)
{
    *lenOut = 0;
    *statusOut = 0;

    char urlBuf[512];
    if(strncmp(url, "https://", 8) == 0) {
        snprintf(urlBuf, sizeof(urlBuf), "http://%s", url + 8);
        url = urlBuf;
    }

    httpcContext *context = &raActiveContext;
    HTTPC_RequestMethod method = postData ? HTTPC_METHOD_POST : HTTPC_METHOD_GET;
    if(R_FAILED(httpcOpenContext(context, method, url, 1)))
        return false;

    LightEvent_Clear(&raRequestFinished);
    LightLock_Lock(&raContextLock);
    raContextLive = true;
    raContextDeadline = osGetTime() + RA_HTTP_TIMEOUT_MS;
    LightLock_Unlock(&raContextLock);
    LightEvent_Signal(&raRequestArmed);

    httpcSetSSLOpt(context, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(context, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(context, "Connection", "Keep-Alive");
    httpcAddRequestHeaderField(context, "User-Agent", raUserAgent);

    if(postData) {
        httpcAddRequestHeaderField(context, "Content-Type",
            contentType ? contentType : "application/x-www-form-urlencoded");
        httpcAddPostDataRaw(context, (const u32 *)postData, (u32)strlen(postData));
    }

    if(R_FAILED(httpcBeginRequest(context))) {
        raCloseActiveContext(false);
        return false;
    }

    raDisarmRequestTimeout();

    u32 statusCode = 0;
    if(R_FAILED(httpcGetResponseStatusCodeTimeout(context, &statusCode, RA_HTTP_TIMEOUT_NS))) {
        raCloseActiveContext(true);
        return false;
    }

    if(!*bufPtr) {
        *bufPtr = (char *)malloc(initialCap);
        if(!*bufPtr) {
            raCloseActiveContext(true);
            return false;
        }
        *capPtr = initialCap;
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

// Inline transport reuses the shared response buffer.
static void raServerCallInline(const rc_api_request_t *request,
                               rc_client_server_callback_t callback, void *callbackData)
{
    LightLock_Lock(&raHttpLock);
    u32 len = 0;
    int status = 0;
    bool ok = raHttpPerform(request->url, request->post_data, request->content_type,
                            &raResponseBuf, &raResponseCap, RA_RESPONSE_INITIAL_CAP, &len, &status);
    LightLock_Unlock(&raHttpLock);

    rc_api_server_response_t response;
    memset(&response, 0, sizeof(response));
    response.http_status_code = ok ? status : 0;
    response.body = ok ? raResponseBuf : NULL;
    response.body_length = ok ? len : 0;
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
        LightLock_Lock(&raHttpLock);
        bool ok = raHttpPerform(req->url, req->postData, req->contentType, &body, &cap, 4096, &len, &status);
        LightLock_Unlock(&raHttpLock);

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

    if(raSyncMode || !raWorkerRunning) {
        raServerCallInline(request, callback, callbackData);
        return;
    }

    char *url = raStrdup(request->url);
    RaRequest *req = url ? (RaRequest *)malloc(sizeof(RaRequest)) : NULL;
    RaResponse *resp = req ? (RaResponse *)malloc(sizeof(RaResponse)) : NULL;
    if(!req || !resp) {
        free(url);
        free(req);
        raServerCallInline(request, callback, callbackData);
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
        raServerCallInline(request, callback, callbackData);
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

void raHttpSetSyncMode(bool sync)
{
    raSyncMode = sync;
}

void raHttpSetUserAgent(const char *userAgent)
{
    snprintf(raUserAgent, sizeof(raUserAgent), "%s", userAgent ? userAgent : "");
}

void raHttpInitialize(void)
{
    httpcInit(0x1000);

    LightLock_Init(&raContextLock);
    LightEvent_Init(&raRequestArmed, RESET_ONESHOT);
    LightEvent_Init(&raRequestFinished, RESET_ONESHOT);
    LightLock_Init(&raHttpLock);
    LightLock_Init(&raRequestLock);
    LightLock_Init(&raCompletionLock);
    CondVar_Init(&raRequestCond);

    raWorkerRunning = true;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    raWorker = threadCreate(raWorkerMain, NULL, 0x4000, prio + 1, 0, false);
    if(!raWorker)
        raWorkerRunning = false; // fall back to synchronous transport

    raRequestTimeoutRunning = true;
    raRequestTimeoutThread = threadCreate(raRequestTimeoutMain, NULL, 0x2000, prio + 1, -1, false);
    if(!raRequestTimeoutThread)
        raRequestTimeoutRunning = false;
}

void raHttpFinalize(void)
{
    // stop the worker, then discard any queued work (no callbacks at shutdown)
    if(raWorker) {
        LightLock_Lock(&raRequestLock);
        raWorkerRunning = false;
        CondVar_Signal(&raRequestCond);
        LightLock_Unlock(&raRequestLock);
        threadJoin(raWorker, UINT64_MAX);
        threadFree(raWorker);
        raWorker = NULL;
    }

    if(raRequestTimeoutThread) {
        raRequestTimeoutRunning = false;
        LightEvent_Signal(&raRequestFinished);
        LightEvent_Signal(&raRequestArmed);
        threadJoin(raRequestTimeoutThread, UINT64_MAX);
        threadFree(raRequestTimeoutThread);
        raRequestTimeoutThread = NULL;
    }
    raFreeQueues();

    free(raResponseBuf);
    raResponseBuf = NULL;
    raResponseCap = 0;

    httpcExit();
}
