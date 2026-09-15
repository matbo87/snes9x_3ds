#include "3dsra_http.h"

#include "3dshttp.h"

#include <cstdlib>
#include <cstring>

struct RaServerCallContext {
    rc_client_server_callback_t callback;
    void *callbackData;
};

static void raServerResponseCallback(const Http3dsResponse *response, void *data)
{
    RaServerCallContext *context = static_cast<RaServerCallContext *>(data);
    rc_api_server_response_t raResponse = {};

    // Only transport failures are retryable; status 0 with a body is still a
    // real response for rcheevos.
    raResponse.http_status_code = response->transportFailed
        ? RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR : response->httpStatusCode;
    raResponse.body = response->body;
    raResponse.body_length = response->bodyLength;
    context->callback(&raResponse, context->callbackData);
    free(context);
}

bool raHttpInitialize(void)
{
    return http3dsIsReady();
}

void raHttpFinalize(void)
{
    // Transport lifetime is owned by 3dsmain.
}

void raHttpSetUserAgent(const char *userAgent)
{
    http3dsSetUserAgent(userAgent);
}

void raHttpCancelActiveRequest(void)
{
    http3dsCancelActiveRequest();
}

void raDrainCompletions(void)
{
    http3dsDrainCompletions();
}

void raServerCall(const rc_api_request_t *request,
                  rc_client_server_callback_t callback,
                  void *callbackData, rc_client_t *client)
{
    (void)client;
    RaServerCallContext *context = static_cast<RaServerCallContext *>(malloc(sizeof(*context)));
    if(!context) {
        rc_api_server_response_t response = {};
        response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        response.body = "Out of memory";
        response.body_length = strlen(response.body);
        callback(&response, callbackData);
        return;
    }
    context->callback = callback;
    context->callbackData = callbackData;
    if(!http3dsRequest(request->url, request->post_data, request->content_type, NULL,
                       raServerResponseCallback, context)) {
        free(context);
        rc_api_server_response_t response = {};
        response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        response.body = "Network unavailable";
        response.body_length = strlen(response.body);
        callback(&response, callbackData);
    }
}
