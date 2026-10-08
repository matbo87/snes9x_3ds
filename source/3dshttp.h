#ifndef _3DSHTTP_H_
#define _3DSHTTP_H_

#include <3ds.h>

// Caller retains this state until the completion callback runs.
typedef struct Http3dsProgress {
    volatile u32 downloaded;
    volatile u32 total;
    volatile bool cancelRequested;   // set by the caller; cancels only this download
} Http3dsProgress;

typedef struct Http3dsResponse {
    int httpStatusCode;      // 0 when missing or implausible; the body may still be valid
    bool success;
    bool transportFailed;    // no usable response; body holds an error message
    u32 downloaded;
    const char *body;         // valid only during the completion callback
    u32 bodyLength;
} Http3dsResponse;

typedef void (*Http3dsCallback)(const Http3dsResponse *response, void *callbackData);

// Shared asynchronous HTTP transport. Responses are delivered only when
// http3dsDrainCompletions is called from the main thread.
bool http3dsInitialize(void);
void http3dsFinalize(void);
bool http3dsIsReady(void);
void http3dsSetUserAgent(const char *userAgent);

// headers: NULL or extra "Name: value" lines separated by '\n'.
// progress: optional per-request cancellation; routes through curl.
// Plain HTTP without progress uses httpc.
bool http3dsRequest(const char *url, const char *postData, const char *contentType,
                    const char *headers, Http3dsProgress *progress,
                    Http3dsCallback callback, void *callbackData);
bool http3dsGet(const char *url, Http3dsProgress *progress,
                Http3dsCallback callback, void *callbackData);
bool http3dsDownloadFile(const char *url, const char *temporaryPath,
                         Http3dsProgress *progress,
                         Http3dsCallback callback, void *callbackData);
void http3dsDrainCompletions(void);
void http3dsCancelActiveRequest(void);

// SOC stays initialized for curl and RA badge downloads.
bool http3dsSocIsReady(void);

#endif
