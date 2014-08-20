#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <event2/event.h>
#include <assert.h>

#ifdef __GNUC__
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <Windows.h>
#endif

#include "t2u.h"
#include "t2u_internal.h"

#define T2U_UDP_PORT_S 12345
#define T2U_UDP_PORT_C 12346

#define TEST_TCP_PORT_ORIG 13579
#define TEST_TCP_PORT_MAPP 24680

void test_log(int level, const char *mess)
{
    if (level >= 0)
    {
        /* only for info log and above. */
        fprintf(stdout, "%d %s", level, mess);
    }
}

static struct event_base *g_base = NULL;

typedef struct client_context_
{
    sock_t sock;
    struct event *ev;
    struct event *ev2;
} client_context;

typedef struct server_context_
{
    sock_t sock;
    struct event *ev;
    rbtree *clients;
} server_context;


typedef struct test_context_
{
    forward_context context;
    forward_rule rule;
    sock_t sock;
} test_context;

#if defined __GNUC__
static void* test_loop_(void *arg)
#elif defined _MSC_VER
static DWORD __stdcall test_loop_(void * arg)
#else
#error "Compiler not support."
#endif
{
    struct event_base *base = (struct event_base *)arg;
    int r = event_base_dispatch(base);

#if defined __GNUC__
    return NULL;
#elif defined _MSC_VER
    return 0;
#endif
}


static void client_recv_cb_(evutil_socket_t sock, short events, void *arg)
{
    client_context *cc = (client_context *)arg;
    static unsigned int buff[1024];
    static int r = 0;
    int i = 0;
    static unsigned int p = 0;

    if (r < sizeof(buff))
    {
        int a = recv(cc->sock, ((char *)buff) + r, sizeof(buff) - r, 0);
        if (a < 0)
        {
            LOG_(3, "recv got < 0");
            exit(1);
        }
        r += a;
    }

    if (r == sizeof(buff))
    {
        for (i = 0; i < sizeof(buff) / sizeof(buff[0]); i++)
        {
            unsigned int tmp = htonl(buff[i]);
            assert(tmp == (++p));
        }
        printf(".");
        fflush(stdout);

        r = 0;
    }
}

static void client_time_cb_(evutil_socket_t sock, short events, void *arg)
{
    client_context *cc = (client_context *)arg;
    static int tt = 1;
    tt*=2;
    struct timeval t = { tt, 0 };
    int r = 0;
    int i = 0;

    static unsigned int p = 0;

    unsigned int buff[1024];
    for (i = 0; i < sizeof(buff)/sizeof(buff[0]); i++)
    {
        buff[i] = htonl(++p);
    }

    event_add(cc->ev, &t);
    while (r < sizeof(buff))
    {
        int a = send(cc->sock, ((char *)buff)+r, sizeof(buff)-r, 0);
        if (a < 0)
        {
            LOG_(3, "send got < 0");
            exit(1);
        }
        r += a;
    }
}

static void tcp_cb_(evutil_socket_t sock, short events, void *arg)
{
    char buff[1024];

    int r = recv(sock, buff, sizeof(buff), 0);
    if (r > 0)
    {
        int s = 0;
        while (s < r)
        {
            s += send(sock, buff + s, r - s, 0);
        }
    }
}

static void tcp_listen_cb_(evutil_socket_t sock, short events, void *arg)
{
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);
    sock_t ns = accept(sock, (struct sockaddr *)&addr, &addr_len);
    server_context *sc = (server_context *)arg;

    if (ns > 0)
    {
        client_context *cc = (client_context *)malloc(sizeof(client_context));
        cc->sock = ns;
        cc->ev = event_new(g_base, cc->sock, EV_READ | EV_PERSIST, tcp_cb_, cc);
        event_add(cc->ev, NULL);
        rbtree_insert(sc->clients, cc, cc);
    }
    else
    {
        fprintf(stderr, "accept failed.\n");
    }
}

server_context *setup_server()
{
    struct sockaddr_in addr;
    server_context *sc = (server_context *)malloc(sizeof(server_context));
    
    sc->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sc->sock == -1)
    {
        fprintf(stderr, "socket failed.\n");
        free(sc);
        return NULL;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(TEST_TCP_PORT_ORIG);

    if (-1 == bind(sc->sock, (const struct sockaddr *)&addr, sizeof(addr)))
    {
        fprintf(stderr, "bind failed.\n");
        free(sc);
        return NULL;
    }
    
    if (-1 == listen(sc->sock, 64))
    {
        fprintf(stderr, "bind failed.\n");
        free(sc);
        return NULL;
    }

    if (!g_base)
    {
        g_base = event_base_new();
    }

    sc->ev = event_new(g_base, sc->sock, EV_READ | EV_PERSIST, tcp_listen_cb_, sc);
    event_add(sc->ev, NULL);

    sc->clients = rbtree_init(NULL);
    return sc;
}

static void cleanup_server(server_context *sc)
{
    /* close clients in sc->clients */
    while (sc->clients->root)
    {
        client_context * cc = sc->clients->root->key;
        rbtree_remove(sc->clients, cc);

        event_free(cc->ev);
        closesocket(cc->sock);
        free(cc);
    }

    event_free(sc->ev);
    closesocket(sc->sock);
}


client_context *setup_client()
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(TEST_TCP_PORT_MAPP);
    struct timeval t = { 1, 0 };

    client_context * cc = (client_context *)malloc(sizeof(client_context));
    cc->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(cc->sock, (const struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        fprintf(stderr, "connect failed.\n");
        exit(1);
    }

    cc->ev = evtimer_new(g_base, client_time_cb_, cc);
    event_add(cc->ev, &t);

    cc->ev2 = event_new(g_base, cc->sock, EV_READ | EV_PERSIST, client_recv_cb_, cc);
    event_add(cc->ev2, NULL);

    return cc;
}


static void cleanup_client(client_context *cc)
{
    event_free(cc->ev);
    event_free(cc->ev2);

    closesocket(cc->sock);
    free(cc);
}

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
        tc->rule = add_forward_rule(tc->context, forward_server_mode, "test", "127.0.0.1", TEST_TCP_PORT_ORIG);
    }
    else
    {
        tc->rule = add_forward_rule(tc->context, forward_client_mode, "test", "127.0.0.1", TEST_TCP_PORT_MAPP);
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
    t2u_thr_t test_thr;

    test_context *tc_server = setup_t2u(1);
    test_context *tc_client = setup_t2u(0);

    set_context_option(tc_server->context, CTX_SESSION_TIMEOUT, 10);


    server_context *sc = setup_server();
    client_context *cc = setup_client();

    t2u_thr_create(&test_thr, test_loop_, g_base);

    printf("press enter to exit!\n");
    getchar();

    cleanup_t2u(tc_server);
    cleanup_t2u(tc_client);

    cleanup_server(sc);
    cleanup_client(cc);

    
    /* wait */
    t2u_thr_join(test_thr);

}