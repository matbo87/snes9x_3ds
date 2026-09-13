#include "3dsra_http.h"

#include "3dslog.h"
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

// Request/completion node. strings[] holds url/postData/contentType inline.
typedef struct RaCall {
    const char *url;
    const char *postData;     // NULL for GET
    const char *contentType;  // NULL when unspecified
    rc_client_server_callback_t callback;
    void *callbackData;
    char *body;               // NULL when the request failed
    u32 bodyLength;
    int httpStatusCode;       // 0 when the request failed
    struct RaCall *next;
    char strings[];
} RaCall;

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
static RaCall     *raRequestHead = NULL, *raRequestTail = NULL;
static LightLock  raCompletionLock;
static RaCall     *raCompletionHead = NULL, *raCompletionTail = NULL;

// Copies into the flexible tail storage.
static char *raCopyString(char **cursor, const char *s)
{
    if(!s)
        return NULL;
    char *dst = *cursor;
    size_t n = strlen(s) + 1;
    memcpy(dst, s, n);
    *cursor += n;
    return dst;
}

static void raFreeCall(RaCall *call)
{
    free(call->body);
    free(call);
}

static void raFreeList(RaCall *call)
{
    while(call) {
        RaCall *next = call->next;
        raFreeCall(call);
        call = next;
    }
}

// Caller holds the lock guarding this list.
static void raListPushLocked(RaCall **head, RaCall **tail, RaCall *call)
{
    call->next = NULL;
    if(*tail)
        (*tail)->next = call;
    else
        *head = call;
    *tail = call;
}

static void raQueueCompletion(RaCall *call)
{
    LightLock_Lock(&raCompletionLock);
    raListPushLocked(&raCompletionHead, &raCompletionTail, call);
    LightLock_Unlock(&raCompletionLock);
}

static RaCall *raDetachCompletions()
{
    LightLock_Lock(&raCompletionLock);
    RaCall *call = raCompletionHead;
    raCompletionHead = raCompletionTail = NULL;
    LightLock_Unlock(&raCompletionLock);
    return call;
}

static void raQueueRequest(RaCall *call)
{
    LightLock_Lock(&raRequestLock);
    raListPushLocked(&raRequestHead, &raRequestTail, call);
    CondVar_Signal(&raRequestCond);
    LightLock_Unlock(&raRequestLock);
}

static RaCall *raPopRequestLocked()
{
    RaCall *call = raRequestHead;
    if(call) {
        raRequestHead = call->next;
        if(!raRequestHead)
            raRequestTail = NULL;
    }
    return call;
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

        pos = (dlPos > dlStart) ? dlPos - dlStart : 0;
        if(pos > size) pos = size;
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

// rcheevos copies the body as a retryable error message.
static void raSetTransportError(RaCall *call, const char *message)
{
    call->body = strdup(message);
    if(!call->body)
        return;   // stays a bare "no response"

    call->bodyLength = strlen(call->body);
    call->httpStatusCode = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
}

static bool raReadResponse(httpcContext *context, RaCall *call)
{
    u32 statusCode = 0;
    if(R_FAILED(httpcGetResponseStatusCodeTimeout(context, &statusCode, RA_HTTP_TIMEOUT_NS))) {
        raSetTransportError(call, "Connection timed out");
        return false;
    }
    
    // Citra can report 0xFFFFFFFF here. Map invalid HTTP statuses to 0 so
    // conversion to int cannot collide with rcheevos' negative client-error codes.
    if(statusCode < 100 || statusCode > 599) {
        log3dsWrite("[RA] http: implausible status %lu, treating as no response", statusCode);
        statusCode = 0;
    }

    // Content-Length seems unreliable for chunked RA payloads
    // (httpcGetDownloadSizeState returns contentsize 0)
    u32 cap = RA_RESPONSE_INITIAL_CAP;
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
        ret = raDownloadDataTimeout(context, (u8 *)(body + totalRead), cap - totalRead - 1, &readSize, RA_HTTP_TIMEOUT_NS);
        totalRead += readSize;
    } while(ret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

    if(R_FAILED(ret)) {
        free(body);
        raSetTransportError(call, "Connection lost");
        return false;
    }

    body[totalRead] = '\0';
    call->body = body;
    call->bodyLength = totalRead;
    call->httpStatusCode = (int)statusCode;


    return true;
}

// Fills in the response fields of call; they stay zeroed on failure.
static void raHttpPerform(RaCall *call)
{
    httpcContext *context = &raActiveContext;
    HTTPC_RequestMethod method = call->postData ? HTTPC_METHOD_POST : HTTPC_METHOD_GET;
    if(R_FAILED(httpcOpenContext(context, method, call->url, 1))) {
        raSetTransportError(call, "Could not open connection");
        return;
    }

    LightLock_Lock(&raContextLock);
    if(raContextClosed) {
        LightLock_Unlock(&raContextLock);
        httpcCloseContext(context);
        return;
    }
    raContextLive = true;
    LightLock_Unlock(&raContextLock);

    httpcSetKeepAlive(context, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(context, "Connection", "Keep-Alive");
    httpcAddRequestHeaderField(context, "User-Agent", raUserAgent);

    if(call->postData) {
        httpcAddRequestHeaderField(context, "Content-Type",
            call->contentType ? call->contentType : "application/x-www-form-urlencoded");
        httpcAddPostDataRaw(context, (const u32 *)call->postData, (u32)strlen(call->postData));
    }

    // No body bytes are pending before BeginRequest succeeds.
    if(R_FAILED(httpcBeginRequest(context))) {
        raSetTransportError(call, "Could not reach the server");
        raCloseActiveContext(false);
        return;
    }

    // Failures after BeginRequest may leave unread body bytes.
    bool ok = raReadResponse(context, call);
    raCloseActiveContext(!ok);
}

static void raReportCallFailed(rc_client_server_callback_t callback, void *callbackData)
{
    rc_api_server_response_t response;
    memset(&response, 0, sizeof(response));
    response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    response.body = "Out of memory";
    response.body_length = strlen(response.body);
    callback(&response, callbackData);
}

static void raWorkerMain(void *arg)
{
    (void)arg;

    LightLock_Lock(&raRequestLock);
    while(raWorkerRunning) {
        RaCall *call = raPopRequestLocked();
        if(!call) {
            CondVar_Wait(&raRequestCond, &raRequestLock);
            continue;
        }
        LightLock_Unlock(&raRequestLock);

        raHttpPerform(call);
        raQueueCompletion(call);

        LightLock_Lock(&raRequestLock);
    }
    LightLock_Unlock(&raRequestLock);
}

void raServerCall(const rc_api_request_t *request,
                  rc_client_server_callback_t callback,
                  void *callbackData, rc_client_t *client)
{
    (void)client;

    size_t urlLen  = request->url ? strlen(request->url) + 1 : 0;
    size_t postLen = request->post_data ? strlen(request->post_data) + 1 : 0;
    size_t ctLen   = request->content_type ? strlen(request->content_type) + 1 : 0;

    RaCall *call = urlLen ? (RaCall *)malloc(sizeof(RaCall) + urlLen + postLen + ctLen) : NULL;
    if(!call) {
        raReportCallFailed(callback, callbackData);
        return;
    }

    char *cursor = call->strings;
    call->url = raCopyString(&cursor, request->url);
    call->postData = raCopyString(&cursor, request->post_data);
    call->contentType = raCopyString(&cursor, request->content_type);
    call->callback = callback;
    call->callbackData = callbackData;
    call->body = NULL;
    call->bodyLength = 0;
    call->httpStatusCode = 0;

    raQueueRequest(call);
}

void raDrainCompletions()
{
    RaCall *call = raDetachCompletions();
    while(call) {
        RaCall *next = call->next;

        rc_api_server_response_t response;
        memset(&response, 0, sizeof(response));
        response.http_status_code = call->httpStatusCode;
        response.body = call->body;
        response.body_length = call->bodyLength;
        call->callback(&response, call->callbackData);

        raFreeCall(call);
        call = next;
    }
}

static void raFreeQueues()
{
    raFreeList(raRequestHead);
    raRequestHead = raRequestTail = NULL;

    raFreeList(raCompletionHead);
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
    if(raWorker) {
        LightLock_Lock(&raRequestLock);
        raWorkerRunning = false;
        CondVar_Signal(&raRequestCond);
        LightLock_Unlock(&raRequestLock);

        // Wake an idle worker and cancel active I/O.
        raCancelLiveContext();

        // Only 0 means joined; RD_TIMEOUT is positive, so R_FAILED would miss it.
        if(threadJoin(raWorker, RA_HTTP_WORKER_JOIN_MS * 1000000ULL) != 0)
            return;

        threadFree(raWorker);
        raWorker = NULL;
    }

    raFreeQueues();

    httpcExit();
    raHttpReady = false;
}
