// ---------------------------------------------------------------------------
//
// WolProxy.c: A forwarder for wake-on-lan magic packets. Receives magic 
//             packets on port 9, if send from outside of our LAN, this packet
//             is forwarded to the subnet broadcast.
//
//             Requires a port forwarding rule in your router: 
//                external port 9 UDP to internal port 9 UDP
//
//
// Author:     Martin Rothschink <martin.rothschink@axonet.de>
//
// Version:    1.0 released July 3, 2012
//             1.0.2 OpenVPN fix, released November 2014
//	           1.0.3 no interface on startup fix, released January 2015
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#if defined(WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>

#else
#include <errno.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <signal.h>
#define strcpy_s(a,b,c) strcpy(a,c)
#define strcat_s(a,b,c) strcat(a,c)
#define sprintf_s(a,b,c,d) sprintf(a,c,d)
#endif

#ifdef WIN32
#pragma comment( lib, "ws2_32.lib" )
#endif

#define PORT   9
#define PSIZE  102
#define BSIZE  256

#define COPYRIGHT "WolProxy 1.0.3, Copyright (c) AxoNet Software GmbH, Martin Rothschink 2012-2015"

static struct in_addr LocalAddr[10];
static struct in_addr Broadcast[10];
static struct in_addr Netmask[10];
static int AddrCount;
static int IsDaemon = 0;
static char *PidFile = NULL;

/*
** Write log message, either to console via printf or to syslog (linux)
*/

static void LogInfo(char *fmt, ...)
{
    char buffer[512];
    va_list args;

    va_start(args, fmt);
    vsprintf(buffer, fmt, args);
    va_end(args);

#ifdef WIN32
    printf("%s\n", buffer);
#else
    if (IsDaemon)
        syslog(LOG_INFO, "%s", buffer);
    else
        printf("%s\n", buffer);
#endif
}

/*
** exit process with error message and error number
*/

static void exitp(char *msg)
{
#ifdef WIN32
    fprintf(stderr, "Error %d: %s\n", errno, msg);
    exit(1);
#else
    if (IsDaemon)
    {
        int err = errno;
        if (err < sys_nerr)
        {
            const char *error = sys_errlist[err];
            syslog(LOG_ERR, "%s: %s", msg, error);
        }
        else
        {
            syslog(LOG_ERR, "Error %d: %s\n", err, msg);
        }
    }
    else
    {
        perror(msg);
    }

    exit(1);
#endif
}

/*
** Display interface data
*/

static void DumpInterfaceInfo(int i)
{
    char ip[20], bc[20], nm[20];
    strcpy_s(ip, sizeof(ip), inet_ntoa(LocalAddr[i]));
    strcpy_s(bc, sizeof(bc), inet_ntoa(Broadcast[i]));
    strcpy_s(nm, sizeof(nm), inet_ntoa(Netmask[i]));
    LogInfo("IP %-15s netmask %-15s broadcast %-15s\n", ip, nm, bc);
}


/*
** Get ip, netmask and broadcast address for all local interfaces
*/
static int GetInterfaces(void)
{
#ifdef WIN32
    INTERFACE_INFO info[20];
    DWORD dwBytesRead;
    struct in_addr dwBroadcast;
    unsigned int i, ifFound;

    AddrCount = 0;
    SOCKET s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        return 0;
    }

    // get interface list
    if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, NULL, 0, (void*)info, sizeof(info), &dwBytesRead, NULL, NULL) != 0)
    {
        closesocket(s);
        return 0;
    }

    ifFound = (dwBytesRead / sizeof(INTERFACE_INFO));
    LogInfo("found %d interfaces:\n", ifFound);

    for (i = 0; i < ifFound; i++)
    {
        if (info[i].iiFlags == (IFF_UP | IFF_BROADCAST | IFF_MULTICAST))
        {
            DWORD ip = info[i].iiAddress.AddressIn.sin_addr.s_addr;
            DWORD dwSubnet = info[i].iiNetmask.AddressIn.sin_addr.s_addr;
            dwBroadcast.s_addr = ip | ~(dwSubnet);
            memcpy(&LocalAddr[AddrCount], &info[i].iiAddress.AddressIn.sin_addr, sizeof(struct in_addr));
            memcpy(&Netmask[AddrCount], &info[i].iiNetmask.AddressIn.sin_addr, sizeof(struct in_addr));
            memcpy(&Broadcast[AddrCount], &dwBroadcast.s_addr, sizeof(struct in_addr));

            DumpInterfaceInfo(AddrCount);
            AddrCount++;
        }
    }

    closesocket(s);

    return AddrCount;

#else

    // unix/linux getifaddrs() implementation

    struct ifaddrs * ifap;
    AddrCount = 0;

    if (getifaddrs(&ifap) == 0)
    {
        struct ifaddrs * p = ifap;
        while (p)
        {
            int family = 0;

            if (p->ifa_addr != NULL)
                family = p->ifa_addr->sa_family;

            if ((family == AF_INET) &&
                ((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr > 0 &&
                (p->ifa_flags & IFF_LOOPBACK) == 0)
            {
                memcpy(&LocalAddr[AddrCount], &((struct sockaddr_in *)p->ifa_addr)->sin_addr, sizeof(struct in_addr));
                memcpy(&Netmask[AddrCount], &((struct sockaddr_in *)p->ifa_netmask)->sin_addr, sizeof(struct in_addr));
                memcpy(&Broadcast[AddrCount], &((struct sockaddr_in *)p->ifa_broadaddr)->sin_addr, sizeof(struct in_addr));

                DumpInterfaceInfo(AddrCount);
                AddrCount++;
            }
            p = p->ifa_next;
        }
        freeifaddrs(ifap);
    }

    return AddrCount;
#endif
}

#ifndef WIN32

/*
** Signal handler, exit program
*/

void handleQuitAndTerm(int sig)
{
    LogInfo("Signal %d received, closing wolproxy", sig);
    if (IsDaemon)
        unlink(PidFile);
    exit(0);
}

/*
** create a daemon process
*/
void CreateDaemon(char *logname, int facility)
{
    int i;
    pid_t pid;
    FILE *pidfile;

    if ((pid = fork()) != 0)
    {
        exit(0);
    }

    if (setsid() == -1)
    {
        fprintf(stderr, "%s can't be new leader of new session!\n", logname);
        exit(0);
    }

    chdir("/");
    umask(0);

    pidfile = fopen(PidFile, "w");
    if (pidfile != NULL)
    {
        fprintf(pidfile, "%d\n", getpid());
        fclose(pidfile);
    }
    else
    {
        exitp("Can't create pidfile");
    }

    openlog(logname, LOG_PID, facility);

}
#endif

/*
**Test ip address
** Return true if addr belongs to one of our local networks, false otherwise
*/

static int IsLocalIp(struct in_addr addr)
{
    int i;
    for (i = 0; i < AddrCount; i++)
    {
        unsigned long mynetwork = LocalAddr[i].s_addr & Netmask[i].s_addr;
        unsigned long other = addr.s_addr & Netmask[i].s_addr;
        if (mynetwork == other)
            return 1;
    }

    return 0;
}

/*
** send a single broadcast packet
*/

static void SendBroadcast(int s, char *data, struct sockaddr_in broadcast)
{
    int res = sendto(s, data, PSIZE, 0, (struct sockaddr *)&broadcast, sizeof(broadcast));
    if (res == PSIZE)
    {
        LogInfo("      %s\n", inet_ntoa(broadcast.sin_addr));
    }
}

/*
** Forward packet to all subnet broadcast addresses
*/

static void ForwardWol(int s, char *data)
{
    struct sockaddr_in broadcast;

    memset(&broadcast, 0, sizeof(broadcast));
    broadcast.sin_family = AF_INET;
    broadcast.sin_port = htons(PORT);

    if (AddrCount == 0)
    {
        broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        SendBroadcast(s, data, broadcast);
    }
    else
    {
        int i;
        for (i = 0; i < AddrCount; ++i)
        {
            broadcast.sin_addr.s_addr = Broadcast[i].s_addr;
            SendBroadcast(s, data, broadcast);
        }
    }
}

/*
** main
*/

int main(int argc, char* argv[])
{
    struct sockaddr_in myaddr, sender;
    char buffer[BSIZE];
    int i, s, l = sizeof(sender);
    int optval = 1;

#ifdef WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 0), &wsa);
    printf("\n%s\n", COPYRIGHT);
#else
    IsDaemon = (argc > 2 && strcmp(argv[1], "-d") == 0);

    signal(SIGINT, handleQuitAndTerm);
    signal(SIGQUIT, handleQuitAndTerm);
    signal(SIGTERM, handleQuitAndTerm);
    if (IsDaemon)
    {
        PidFile = argv[2];
        CreateDaemon("wolproxy", LOG_USER);
        LogInfo("Running as daemon: %s", COPYRIGHT);
    }
    else
    {
        printf("\n%s\n", COPYRIGHT);
    }
#endif

    if (!IsDaemon)
    {
        if (!GetInterfaces())
            exitp("Error: Found no interface? Exit!");
    }
    else
    {
        LogInfo("Waiting for interfaces to get up and running");
        while (!GetInterfaces())
            sleep(5);
        sleep(1);
        LogInfo("open socket");
    }

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        exitp("Can't create socket");
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char *)&optval, sizeof(optval)) == -1)
        exitp("Can't set broadcast socket option");

    memset(&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(PORT);
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr*)&myaddr, sizeof(myaddr)) == -1)
        exitp("Can't bind socket");

    LogInfo("Ready, waiting for wol packets...");
    while (1)
    {
        int res = recvfrom(s, buffer, sizeof(buffer), 0, (struct sockaddr*)&sender, &l);
        if (res == PSIZE)
        {
            if (buffer[0] == (char)-1 && buffer[5] == (char)-1 && buffer[6] != (char)-1)
            {
                char mac[30];
                char part[5];
                sprintf_s(mac, sizeof(mac), "%02x", (unsigned char)buffer[6]);
                for (i = 0; i < 5; i++)
                {
                    sprintf_s(part, sizeof(part), "-%02x", (unsigned char)buffer[7 + i]);
                    strcat_s(mac, sizeof(mac), part);
                }
                LogInfo("Received a magic packet for MAC %s from %s:%d \n", mac, inet_ntoa(sender.sin_addr), ntohs(sender.sin_port));

                // check sender address
                if (!IsLocalIp(sender.sin_addr))
                {
                    LogInfo("   forwarding to...\n");
                    ForwardWol(s, buffer);
                }
            }
        }
        else if (res == -1)
        {
            exitp("Can't receive data...");
        }
    }

    return 0;
}

