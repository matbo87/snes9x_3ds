#ifndef _3DSRA_HTTP_H_
#define _3DSRA_HTTP_H_

#include "rc_client.h"

// RetroAchievements adapter for the shared 3dshttp transport.
bool raHttpInitialize(void);
void raHttpFinalize(void);
void raHttpSetUserAgent(const char *userAgent);
void raHttpCancelActiveRequest(void);
void raDrainCompletions(void);

void raServerCall(const rc_api_request_t *request,
                  rc_client_server_callback_t callback,
                  void *callbackData, rc_client_t *client);

#endif
