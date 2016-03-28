#include <esp8266.h>
#include "discovery.h"
#include "config.h"

#define DISCOVER_PORT       2000

static struct espconn discoverConn;
static esp_udp discoverUdp;

static void ICACHE_FLASH_ATTR discoverRecvCallback(void *arg, char *data, unsigned short len)
{
    struct espconn *conn = (struct espconn *)arg;
    remot_info *remoteInfo;
    struct espconn responseConn;
    esp_udp responseUdp;
    uint32_t *rxNext;
    uint32_t myAddress = (conn->proto.udp->local_ip[3] << 24)
                       | (conn->proto.udp->local_ip[2] << 16)
                       | (conn->proto.udp->local_ip[1] << 8)
                       |  conn->proto.udp->local_ip[0];
    os_printf("DISCOVER: myAddress %08lx\n", myAddress);

    // check to see if we already replied to this request
    rxNext = (uint32_t *)data;
    if (len > sizeof(uint32_t)) {
        if (*rxNext++ != 0) {
            os_printf("DISCOVER: received a bogus discover request\n");
            return;
        }
        len -= sizeof(uint32_t);
        while (len >= sizeof(uint32_t)) {
os_printf("DISCOVER: Checking %08lx\n", *rxNext);
            if (*rxNext++ == myAddress) {
                os_printf("DISCOVER: already replied\n");
                return;
            }
            len -= sizeof(uint32_t);
        }
    }

    // insert a random delay to avoid udp packet collisions from multiple modules
    os_delay_us(10000 + (unsigned)os_random() % 50000);

    if (espconn_get_connection_info(conn, &remoteInfo, 0) != 0) {
        os_printf("DISCOVER: espconn_get_connection_info failed\n");
        return;
    }

    responseConn.type = ESPCONN_UDP;
    responseConn.state = ESPCONN_NONE;
    responseConn.proto.udp = &responseUdp;
    os_memcpy(responseConn.proto.udp->remote_ip, remoteInfo->remote_ip, 4);
    responseConn.proto.udp->remote_port = remoteInfo->remote_port;

    if (espconn_create(&responseConn) == 0) {
        char buf[1024];
        int len = os_sprintf(buf,
            "{ "
              "\"name\": \"%s\", "
              "\"description\": \"%s\", "
              "\"reset pin\": \"%d\", "
              "\"rx pullup\": \"%s\""
            " }\n",
            flashConfig.hostname,
            flashConfig.sys_descr,
            flashConfig.reset_pin,
            flashConfig.rx_pullup ? "enabled" : "disabled");
        espconn_send(&responseConn, (uint8 *)buf, len);
        espconn_delete(&responseConn);
    }
}

void ICACHE_FLASH_ATTR initDiscovery(void)
{
    discoverConn.type = ESPCONN_UDP;
    discoverConn.state = ESPCONN_NONE;
    discoverUdp.local_port = DISCOVER_PORT;
    discoverConn.proto.udp = &discoverUdp;
    
    if (espconn_create(&discoverConn) != 0) {
        os_printf("DISCOVER: espconn_create failed\n");
        return;
    }

    if (espconn_regist_recvcb(&discoverConn, discoverRecvCallback) != 0) {
        os_printf("DISCOVER: espconn_regist_recvcb failed\n");
        return;
    }
        
    os_printf("DISCOVER: initialized\n");
}
