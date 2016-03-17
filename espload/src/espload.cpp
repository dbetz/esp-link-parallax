#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "fastproploader.h"
#include "utils.h"
#include "sock.h"

#define DEF_DISCOVER_PORT   2000
#define DEF_RESET_PIN       12

#define MAX_IF_ADDRS        10

typedef int XbeeAddrList;

int initialBaudRate = 115200;
int finalBaudRate = 921600;
int terminalBaudRate = 115200;
int resetPin = DEF_RESET_PIN;
int verbose = 0;

int discover(XbeeAddrList &addrs, int timeout);
int discover1(IFADDR *ifaddr, XbeeAddrList &addrs, int timeout);
int TerminalMode(const char *hostName, int pstMode);
void Usage();

int main(int argc, char *argv[])
{
    XbeeAddrList addrs;
    char *infile = NULL;
    char *ipaddr = NULL;
    LoadType loadType = ltDownloadAndRun;
    int terminalMode = 0;
    int pstMode = 0;
    int ret, i;

    /* get the arguments */
    for (i = 1; i < argc; ++i) {

        /* handle switches */
        if (argv[i][0] == '-') {
            switch(argv[i][1]) {
            case 'e':
                loadType = ltDownloadAndProgramAndRun;
                break;
            case 'i':
                if (argv[i][2])
                    ipaddr = &argv[i][2];
                else if (++i < argc)
                    ipaddr = argv[i];
                else
                    Usage();
                break;
            case 'r':
                if (argv[i][2])
                    resetPin = atoi(&argv[i][2]);
                else if (++i < argc)
                    resetPin = atoi(argv[i]);
                else
                    Usage();
                break;
            case 't':
                terminalMode = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case '?':
                /* fall through */
            default:
                Usage();
                break;
            }
        }

        /* handle the input filename */
        else {
            if (infile)
                Usage();
            infile = argv[i];
        }
    }
    
    if (infile) {
        if (!ipaddr) {
            printf("error: must specify IP address or host name with -i\n");
            return 1;
        }
        if (fastLoad(ipaddr, infile, loadType) < 0)
            return 1;
    }
    
    else if (!terminalMode) {
        if ((ret = discover(addrs, 2000)) < 0) {
            printf("error: discover failed: %d\n", ret);
            return 1;
        }
    }
    
    if (terminalMode) {
        setBaudRate(ipaddr, terminalBaudRate);
        printf("[ Entering terminal mode. Type ESC or Control-C to exit. ]\n");
        TerminalMode(ipaddr, pstMode);
    }
    
    return 0;
}

void Usage()
{
    printf("\
usage: espload\n\
         [ -e ]            write program to the EEPROM\n\
         [ -i <addr> ]     IP address or host name of module to load\n\
         [ -r <pin> ]      pin to use for resetting the Propeller (default is %d)\n\
         [ -t ]            enter terminal mode after loading\n\
         [ -v ]            display verbose debugging output\n\
         [ <name> ]        file to load (discover modules if not given)\n", DEF_RESET_PIN);
    exit(1);
}

int discover(XbeeAddrList &addrs, int timeout)
{
    IFADDR ifaddrs[MAX_IF_ADDRS];
    int cnt, i;
    
    if ((cnt = GetInterfaceAddresses(ifaddrs, MAX_IF_ADDRS)) < 0)
        return -1;
    
    for (i = 0; i < cnt; ++i) {
        int ret;
        if ((ret = discover1(&ifaddrs[i], addrs, timeout)) < 0)
            return ret;
    }
    
    return 0;
}

int discover1(IFADDR *ifaddr, XbeeAddrList &addrs, int timeout)
{
    uint8_t txBuf[1024]; // BUG: get rid of this magic number!
    uint8_t rxBuf[1024]; // BUG: get rid of this magic number!
    SOCKADDR_IN bcastaddr;
    SOCKADDR_IN addr;
    SOCKET sock;
    int cnt;
    
    /* create a broadcast socket */
    if (OpenBroadcastSocket(DEF_DISCOVER_PORT, &sock) != 0) {
        printf("error: OpenBroadcastSocket failed\n");
        return -2;
    }
        
    /* build a broadcast address */
    bcastaddr = ifaddr->bcast;
    bcastaddr.sin_port = htons(DEF_DISCOVER_PORT);
    
    /* send the broadcast packet */
    sprintf((char *)txBuf, "Me here! Ignore this message.\n");
    if ((cnt = SendSocketDataTo(sock, txBuf, strlen((char *)txBuf), &bcastaddr)) != (int)strlen((char *)txBuf)) {
        perror("error: SendSocketDataTo failed");
        CloseSocket(sock);
        return -1;
    }

    /* receive Xbee responses */
    while (SocketDataAvailableP(sock, timeout)) {

        /* get the next response */
        memset(rxBuf, 0, sizeof(rxBuf));
        if ((cnt = ReceiveSocketDataAndAddress(sock, rxBuf, sizeof(rxBuf) - 1, &addr)) < 0) {
            printf("error: ReceiveSocketData failed\n");
            CloseSocket(sock);
            return -3;
        }
        rxBuf[cnt] = '\0';
        
        if (!strstr((char *)rxBuf, "Me here!"))
            printf("from %s got: %s", AddressToString(&addr), rxBuf);
    }
    
    /* close the socket */
    CloseSocket(sock);
    
    /* return successfully */
    return 0;
}

int TerminalMode(const char *hostName, int pstMode)
{
    SOCKADDR_IN addr;
    SOCKET sock;
    
    if (GetInternetAddress(hostName, 23, &addr) != 0) {
        printf("error: invalid host name or IP address '%s'\n", hostName);
        return -1;
    }
    
    if (ConnectSocket(&addr, &sock) != 0) {
        printf("error: connect failed\n");
        return -1;
    }
    
    SocketTerminal(sock, 0, pstMode);
    
    CloseSocket(sock);
    
    return 0;
}

