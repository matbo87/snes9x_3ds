#ifndef _3DSRA_HTTP_H_
#define _3DSRA_HTTP_H_

#include "rc_client.h"   // rc_api_request_t, rc_client_server_callback_t

// Async httpc transport for rc_client server calls.
// The worker thread performs httpc off the emu thread;
// completions are drained on the main thread.

bool raHttpInitialize(void);
void raHttpFinalize(void);

// rc_client server-call callback; pass to rc_client_create.
void raServerCall(const rc_api_request_t *request,
                  rc_client_server_callback_t callback,
                  void *callbackData, rc_client_t *client);

void raDrainCompletions(void);
void raHttpSetSyncMode(bool sync);
void raHttpSetUserAgent(const char *userAgent);

#endif
