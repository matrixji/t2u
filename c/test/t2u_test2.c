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

#define T2U_UDP_PORT_S 12345
#define T2U_UDP_PORT_C 12346

#define TEST_TCP_PORT 13579

void test_log(int level, const char *mess)
{
    if (level >= 0)
    {
        /* only for info log and above. */
        fprintf(stdout, "%d %s", level, mess);
    }
}


typedef struct test_context_
{
    forward_context context;
    forward_rule rule;
    sock_t sock;
} test_context;


void cleanup_t2u(test_context *tc)
{
    free_forward(tc->context);

#ifdef _MSC_VER
    closesocket(tc->sock);
#else
    close(tc->sock);
#endif

    free(tc);
}

test_context *setup_t2u(int isserver)
{
    /* for c/s */
    test_context *tc = (test_context *)malloc(sizeof(test_context));
    struct sockaddr_in addr;

    tc->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (tc->sock == -1)
    {
#ifdef _MSC_VER
        fprintf(stderr, "socket failed. %d\n", WSAGetLastError());
#else
        fprintf(stderr, "socket failed. %d\n", errno);
#endif
        exit(1);
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (isserver)
    {
        addr.sin_port = htons(T2U_UDP_PORT_S);
    }
    else
    {
        addr.sin_port = htons(T2U_UDP_PORT_C);
    }
    if (bind(tc->sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
#ifdef _MSC_VER
        fprintf(stderr, "bind failed. %d\n", WSAGetLastError());
#else
        fprintf(stderr, "bind failed. %d\n", errno);
#endif
        exit(1);
    }

    if (isserver)
    {
        addr.sin_port = htons(T2U_UDP_PORT_C);
    }
    else
    {
        addr.sin_port = htons(T2U_UDP_PORT_S);
    }

    if (connect(tc->sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
#ifdef _MSC_VER
        fprintf(stderr, "connect failed. %d\n", WSAGetLastError());
#else
        fprintf(stderr, "connect failed. %d\n", errno);
#endif
        exit(1);
    }

    tc->context = create_forward(tc->sock);
   
    if (isserver)
    {
        tc->rule = add_forward_rule(tc->context, forward_server_mode, "test", "127.0.0.1", TEST_TCP_PORT);
    }
    else
    {
        tc->rule = add_forward_rule(tc->context, forward_client_mode, "test", "127.0.0.1", TEST_TCP_PORT);
    }

    return tc;
}


int main()
{
    set_log_callback(test_log);

#ifdef _MSC_VER
    WSADATA wsaData;
    WSAStartup(MAKEWORD(1, 2), &wsaData);
#endif

    test_context *tc_server = setup_t2u(1);
    test_context *tc_client = setup_t2u(0);

    printf("press enter to exit!\n");
    getchar();

    cleanup_t2u(tc_server);
    cleanup_t2u(tc_client);

}