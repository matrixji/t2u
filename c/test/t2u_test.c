#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

#include "t2u.h"

/**************************************************************************
 * this is a small demo
 *
 * it will serv local http server (80) on other port (1080),
 *     and serv local ssh server (22) on other port (2222),
 * t2u work as a udp tunnel.
 *
 *
 * client -> tcp:1080 on t2u:client -> udp .. udp -> t2u:server -> tcp:80
 * client -> tcp:2222 on t2u:client -> udp .. udp -> t2u:server -> tcp:22
 *
 *
 **************************************************************************
 */

void test_log(int level, const char *mess)
{
    if (level >= 0)
    {
        /* only for info log and above. */
        fprintf(stdout, "%d %s", level, mess);
    }
}

int main(int argc, char *argv[])
{
    /* for c/s */
    forward_context context_s, context_c;
    forward_rule rule_s[2], rule_c[2];
    int sock_s, sock_c;
    struct sockaddr_in addr_s, addr_c;
    unsigned short port_c = 12345;
    unsigned short port_s = 23456;


    set_log_callback(test_log);

    /* we using localhost udp as tunnel,  */
    /* so you can use tcpdump on lo interface to check the udp packet. */

    /* client side listen */
    sock_c = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_c == -1)
    {
        fprintf(stderr, "socket failed. %s\n", strerror(errno));
        return 1;
    }
    addr_c.sin_family = AF_INET;
    addr_c.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr_c.sin_port = htons(port_c);

    if (bind(sock_c, (struct sockaddr *)&addr_c, sizeof(addr_c)) == -1)
    {
        fprintf(stderr, "socket failed. %s\n", strerror(errno));
        return 1;
    }

    /* server side listen */
    sock_s = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_s == -1)
    {
        fprintf(stderr, "bind failed. %s\n", strerror(errno));
        return 1;
    }
    addr_s.sin_family = AF_INET;
    addr_s.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr_s.sin_port = htons(port_s);

    if (bind(sock_s, (struct sockaddr *)&addr_s, sizeof(addr_s)) == -1)
    {
        fprintf(stderr, "bind failed. %s\n", strerror(errno));
        return 1;
    }

    /* connect each other */
    if (connect(sock_c, (struct sockaddr *)&addr_s, sizeof(addr_s)) == -1)
    {
        fprintf(stderr, "connect failed. %s\n", strerror(errno));
        return 1;
    }
    if (connect(sock_s, (struct sockaddr *)&addr_c, sizeof(addr_c)) == -1)
    {
        fprintf(stderr, "connect failed. %s\n", strerror(errno));
        return 1;
    }

    /* now using the udp tunnel */

    context_s=create_forward(sock_s);
    rule_s[0] = add_forward_rule(context_s, forward_server_mode, "test_http", "127.0.0.1", 80);
    rule_s[1] = add_forward_rule(context_s, forward_server_mode, "test_ssh", "127.0.0.1", 22);


    context_c=create_forward(sock_c);
    rule_c[0] = add_forward_rule(context_c, forward_client_mode, "test_http", "127.0.0.1", 1080);
    rule_c[1] = add_forward_rule(context_c, forward_client_mode, "test_ssh", "127.0.0.1", 2222);
    
    if (argc > 1)
    {
        sleep(atoi(argv[1]));   
    }
    else
    {
        printf("press any key to exit.\n");
        getchar();
    }

    free_forward(context_s);
    free_forward(context_c);

    return 0;
}
