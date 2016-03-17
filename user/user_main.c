#include <esp8266.h>
#include <config.h>

#ifdef PROPLOADER
#include "cgiprop.h"
#include "discovery.h"
#endif

// initialize the custom stuff that goes beyond esp-link
void app_init() {
#ifdef PROPLOADER
  initDiscovery();
  cgiPropInit();
#endif
}
