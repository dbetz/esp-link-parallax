#include "esp8266.h"
#include "sscp.h"

#define SSCP_START  '$'
#define SSCP_MAX    128

static int sscp_initialized = 0;
static char sscp_buffer[SSCP_MAX];
static int sscp_inside;
static int sscp_length;

void ICACHE_FLASH_ATTR sscp_init(void)
{
    sscp_inside = 0;
    sscp_length = 0;
    sscp_initialized = 1;
}

static void ICACHE_FLASH_ATTR dump(char *tag, char *buf, int len)
{
    int i = 0;
    os_printf("%s[%d]: '", tag, len);
    while (i < len) {
        os_printf("%c", buf[i++]);
    }
    os_printf("'\n");
}

void ICACHE_FLASH_ATTR sscp_process(char *buf, short len)
{
    dump("sscp", buf, len);
}

void ICACHE_FLASH_ATTR sscp_filter(char *buf, short len, void (*outOfBand)(void *data, char *buf, short len), void *data)
{
    char *start = buf;
    
    if (!sscp_initialized)
        sscp_init();

    dump("filter", buf, len);

    while (--len >= 0) {
        if (sscp_inside) {
            if (*buf == '\r') {
                sscp_process(sscp_buffer, sscp_length);
                sscp_inside = 0;
                start = ++buf;
            }
            else if (sscp_length < SSCP_MAX)
                sscp_buffer[sscp_length++] = *buf++;
            else {
                // sscp command too long
                sscp_inside = 0;
                start = buf++;
            }
        }
        else {
            if (*buf == SSCP_START) {
                if (buf > start && outOfBand) {
                    dump("outOfBand", start, buf - start);
                    (*outOfBand)(data, start, buf - start);
                }
                sscp_inside = 1;
                sscp_length = 0;
                ++buf;
            }
            else {
                // just accumulate data outside of a command
                ++buf;
            }
        }
    }
    if (buf > start && outOfBand) {
        dump("outOfBand", start, buf - start);
        (*outOfBand)(data, start, buf - start);
    }
}

