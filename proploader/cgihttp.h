#ifndef __CMDHTTP_H__
#define __CMDHTTP_H__

#include "cmd.h"
#include "httpd.h"

int cgiHTTPHandleRequest(HttpdConnData *conn);
void HTTP_Path(CmdPacket *cmd);
void HTTP_Response(CmdPacket *cmd);

#endif

