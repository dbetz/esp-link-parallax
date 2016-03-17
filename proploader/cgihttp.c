#include "cgihttp.h"
#include "cgi.h"

#define HTTP_PATH_MAX   32
#define HTTP_MSG_MAX    32

typedef struct {
    char path[HTTP_PATH_MAX];
    uint32_t callback;
    HttpdConnData *connData;
} Handler;

// only support one path for the moment
static Handler handler;

int ICACHE_FLASH_ATTR cgiHTTPHandleRequest(HttpdConnData *connData)
{
    Handler *h = &handler; // only one for now!
    int match = 0;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;

    // check for a literal match
    if (os_strcmp(h->path, connData->url) == 0)
        match = 1;
        
    // check for a wildcard match
    else {
        int len_m1 = os_strlen(h->path) - 1;
        if (h->path[len_m1] == '*' && os_strncmp(h->path, connData->url, len_m1) == 0)
            match = 1;
    }
    
    // check if we can handle this request
    if (!match)
        return HTTPD_CGI_NOTFOUND;
        
    // store the connection data for sending a response
    h->connData = connData;
    
    os_printf("HTTP_Request: callback %d, path '%s'\n", (int)h->callback, h->path);
    
    char *requestType;
    switch (connData->requestType) {
    case HTTPD_METHOD_GET:
        requestType = "GET";
        break;
    case HTTPD_METHOD_POST:
        requestType = "POST";
        break;
    default:
        requestType = "(unknown)";
        break;
    }
    
    char *vars = connData->getArgs ? connData->getArgs : "";

    cmdResponseStart(CMD_RESP_CB, h->callback, 4);
    cmdResponseBody(requestType, os_strlen(requestType));
    cmdResponseBody(connData->url, os_strlen(connData->url));
    cmdResponseBody(vars, os_strlen(vars));
    if (connData->post->buffLen > 0)
        cmdResponseBody(connData->post->buff, connData->post->buffLen);
    else
        cmdResponseBody("", 0);
    cmdResponseEnd();
    
    connData->post->len = 0;

    return HTTPD_CGI_MORE;
}

void ICACHE_FLASH_ATTR HTTP_Path(CmdPacket *cmd)
{
    Handler *h = &handler; // only one for now!
    uint32_t status = -1;
    uint32_t len;
    
    CmdRequest req;
    cmdRequest(&req, cmd);
  
    if (cmd->argc != 1) {
        os_printf("HTTP_Path: wrong number of arguments\n");
        goto done;
    }
    
    // get the callback
    h->callback = cmd->value;

    // get the path
    if ((len = cmdArgLen(&req)) > HTTP_PATH_MAX - 1) {
        os_printf("HTTP_Path: path too long\n");
        goto done;
    }
    cmdPopArg(&req, h->path, len);
    h->path[len] = '\0';
    
    os_printf("HTTP_Path: callback %d, path '%s'\n", (int)h->callback, h->path);

    status = 0;
    
done:
    cmdResponseStart(CMD_RESP_V, status, 0);
    cmdResponseEnd();
}

#define MAX_SENDBUFF_LEN 2600

void ICACHE_FLASH_ATTR HTTP_Response(CmdPacket *cmd)
{
    Handler *h = &handler; // only one for now!
    uint32_t status = -1;
    char *message;
    uint16_t messageLen;
    int code;
    
    CmdRequest req;
    cmdRequest(&req, cmd);
  
    if (cmd->argc != 1) {
        os_printf("HTTP_Response: wrong number of arguments\n");
        goto done;
    }
    
    // get the HTTP response code
    code = (int)cmd->value;
    
    // get the message
    cmdPopArgPtr(&req, (void **)&message, &messageLen);
    
    os_printf("HTTP_Response: code %d, message '%s'\n", code, message);

    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetOutputBuffer(h->connData, sendBuff, sizeof(sendBuff));
    
    char buf[20];
    os_sprintf(buf, "%d", messageLen);

    httpdStartResponse(h->connData, code);
    httpdHeader(h->connData, "Content-Length", buf);
    httpdEndHeaders(h->connData);
    httpdSend(h->connData, message, messageLen);
    httpdFlush(h->connData);
    
    h->connData->cgi = NULL;

    status = 0;
    
done:
    cmdResponseStart(CMD_RESP_V, status, 0);
    cmdResponseEnd();
}

