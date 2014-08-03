#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef __GNUC__
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <Windows.h>
typedef int socklen_t;
#endif


#include "t2u.h"

void test_log(int level, const char *mess)
{
    if (level >= 0)
    {
        /* only for info log and above. */
        fprintf(stdout, "%d %s", level, mess);
    }
}

void usage(char *cmd)
{
    printf("%s server <udp-port> <service-name> <service-addr> <service-port>\n", cmd);
    printf("%s client <server-addr> <server-port> <service-name> <listen-port>\n", cmd);
    exit(1);
}

int main(int argc, char *argv[])
{
    /* for c/s */
    forward_context context;
    forward_rule rule;
    sock_t sock;
    struct sockaddr_in addr;
    //unsigned short port = 12345;
    int isserver = 1;

    if (argc != 6)
    {
        usage(argv[0]);
    }

#ifdef _MSC_VER
    WSADATA wsaData;
    WSAStartup(MAKEWORD(1, 2), &wsaData);
#endif

    if (strcmp(argv[1], "server") == 0)
    {
        isserver = 1;
    }
    else if (strcmp(argv[1], "client") == 0)
    {
        isserver = 0;
    }
    else
    {
        usage(argv[1]);
    }


    set_log_callback(test_log);

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
    {
#ifdef _MSC_VER
        fprintf(stderr, "socket failed. %d\n", WSAGetLastError());
#else
        fprintf(stderr, "socket failed. %d\n", errno);
#endif
        return 1;
    }

    addr.sin_family = AF_INET;
    if (isserver)
    {
        addr.sin_addr.s_addr = inet_addr("0.0.0.0");
        addr.sin_port = htons(atoi(argv[2]));

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
#ifdef _MSC_VER
            fprintf(stderr, "bind failed. %d\n", WSAGetLastError());
#else
            fprintf(stderr, "bind failed. %d\n", errno);
#endif
            return 1;
        }

        char buff[64];
        socklen_t len = sizeof(addr);
        recvfrom(sock, buff, sizeof(buff), 0, (struct sockaddr *)&addr, &len);

        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
#ifdef _MSC_VER
            fprintf(stderr, "connect failed. %d\n", WSAGetLastError());
#else
            fprintf(stderr, "connect failed. %d\n", errno);
#endif
            return 1;
        }
        send(sock, "hello\0", 6, 0);
    }
    else
    {
        addr.sin_addr.s_addr = inet_addr(argv[2]);
        addr.sin_port = htons(atoi(argv[3]));

        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
#ifdef _MSC_VER
            fprintf(stderr, "connect failed. %d\n", WSAGetLastError());
#else
            fprintf(stderr, "connect failed. %d\n", errno);
#endif
            return 1;
        }

        char buff[64];
        send(sock, "hello\0", 6, 0);
        recv(sock, buff, sizeof(buff), 0);
        if (strcmp(buff, "hello") != 0)
        {
            fprintf(stderr, "hello failed.\n");
            return 1;
        }
    }

    /* now using the udp tunnel */
    context = create_forward(sock);
    set_context_option(context, 3, 64);

    if (isserver)
    {
        rule = add_forward_rule(context, forward_server_mode, argv[3], argv[4], atoi(argv[5]));
    }
    else
    {
        rule = add_forward_rule(context, forward_client_mode, argv[4], "0.0.0.0", atoi(argv[5]));
    }

    printf("press any key to exit.\n");
    getchar();

    free_forward(context);

#ifdef WIN32
    WSACleanup();
#endif

    return 0;
}