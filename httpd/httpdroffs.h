#ifndef HTTPDROFFS_H
#define HTTPDROFFS_H

#include <esp8266.h>
#include "roffs.h"
#include "espfsformat.h"
#include "cgi.h"
#include "httpd.h"

int cgiRoffsHook(HttpdConnData *connData);

#endif
