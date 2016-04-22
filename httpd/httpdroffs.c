/*
Connector to let httpd use the espfs filesystem to serve the files in it.
*/

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain
 * this notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 * Modified and enhanced by Thorsten von Eicken in 2015
 * ----------------------------------------------------------------------------
 */
#include "httpdroffs.h"
#include "roffs.h"
#include "cgi.h"

#define FLASH_PREFIX    "/flash/"

// The static files marked with FLAG_GZIP are compressed and will be served with GZIP compression.
// If the client does not advertise that he accepts GZIP send following warning message (telnet users for e.g.)
static const char *gzipNonSupportedMessage = "HTTP/1.0 501 Not implemented\r\nServer: esp8266-httpd/"HTTPDVER"\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: 52\r\n\r\nYour browser does not accept gzip-compressed data.\r\n";

//This is a catch-all cgi function. It takes the url passed to it, looks up the corresponding
//path in the filesystem and if it exists, passes the file through. This simulates what a normal
//webserver would do with static files.
int ICACHE_FLASH_ATTR 
cgiRoffsHook(HttpdConnData *connData) {
	ROFFS_FILE *file = connData->cgiData;
	int len=0;
	char buff[1024];
	char acceptEncodingBuffer[64];
	int isGzip;

	//os_printf("cgiEspFsHook conn=%p conn->conn=%p file=%p\n", connData, connData->conn, file);

	if (connData->conn==NULL) {
		//Connection aborted. Clean up.
		roffs_close(file);
		return HTTPD_CGI_DONE;
	}

	if (file==NULL) {

        //Get the URL including the prefix
        char *fileName = connData->url;

        //Strip the prefix
        if (os_strncmp(fileName, FLASH_PREFIX, strlen(FLASH_PREFIX)) == 0)
            fileName += strlen(FLASH_PREFIX);

		//First call to this cgi. Open the file so we can read it.
		file=roffs_open(fileName);
		if (file==NULL) {
			return HTTPD_CGI_NOTFOUND;
		}

		// The gzip checking code is intentionally without #ifdefs because checking
		// for FLAG_GZIP (which indicates gzip compressed file) is very easy, doesn't
		// mean additional overhead and is actually safer to be on at all times.
		// If there are no gzipped files in the image, the code bellow will not cause any harm.

		// Check if requested file was GZIP compressed
		isGzip = roffs_file_flags(file) & ROFFS_FLAG_GZIP;
		if (isGzip) {
			// Check the browser's "Accept-Encoding" header. If the client does not
			// advertise that he accepts GZIP send a warning message (telnet users for e.g.)
			httpdGetHeader(connData, "Accept-Encoding", acceptEncodingBuffer, 64);
			if (os_strstr(acceptEncodingBuffer, "gzip") == NULL) {
				//No Accept-Encoding: gzip header present
				httpdSend(connData, gzipNonSupportedMessage, -1);
				roffs_close(file);
				return HTTPD_CGI_DONE;
			}
		}

		connData->cgiData = file;
		httpdStartResponse(connData, 200);
		httpdHeader(connData, "Content-Type", httpdGetMimetype(connData->url));
		if (isGzip) {
			httpdHeader(connData, "Content-Encoding", "gzip");
		}
		httpdHeader(connData, "Cache-Control", "max-age=3600, must-revalidate");
		httpdEndHeaders(connData);
		return HTTPD_CGI_MORE;
	}

	len=roffs_read(file, buff, 1024);
	if (len>0) espconn_sent(connData->conn, (uint8 *)buff, len);
	if (len!=1024) {
		//We're done.
		roffs_close(file);
		return HTTPD_CGI_DONE;
	} else {
		//Ok, till next time.
		return HTTPD_CGI_MORE;
	}
}

typedef struct {
    HttpdConnData *connData;
} WRITE_STATE;

WRITE_STATE writeState = { NULL };

int ICACHE_FLASH_ATTR cgiRoffsWriteFile(HttpdConnData *connData)
{
    WRITE_STATE *state = &writeState;
    char fileName[128];
    int fileSize = 0;
    
    // check for the cleanup call
    if (connData->conn == NULL) {
        if (connection->file) {
            roffs_close(connection->file);
            connection->file = NULL;
        }
        return HTTPD_CGI_DONE;
    }

    if (state->connData) {
        char buf[128];
        os_sprintf(buf, "Flash filesystem write already in progress\r\n");
        errorResponse(connData, 400, buf);
        return HTTPD_CGI_DONE;
    }
    connData->cgiPrivData = state;
    state->connData = connData;
    
    if (!getStringArg(connData, "file", fileName, sizeof(fileName))) {
        errorResponse(connData, 400, "Missing file argument\r\n");
        return HTTPD_CGI_DONE;
    }
    if (!getIntArg(connData, "size", &fileSize))
        errorResponse(connData, 400, "Missing size argument\r\n");
        return HTTPD_CGI_DONE;
    }

    DBG("write-file: file %s, size %d\n", fileName, fileSize);

    startLoading(connection, NULL, fileSize);

    return HTTPD_CGI_MORE;
}
