#ifndef __t2u_h__
#define __t2u_h__

/**************************************************************************
 * normal tcp protocol:
 *     c1 ----- tcp ----- s1
 *     c2 ----- tcp ----- s2
 *
 *
 * over t2u mode:
 *     c1.0 ---+
 *             + -- tcp --- local client forwarder ---+
 *     c1.1 ---+                                      |
 *                                                    + one udp socket +
 *     c2.0 ---+                                      |                |
 *             + -- tcp --- local client forwarder ---+                |
 *     c2.1 ---+                                                    internet
 *                                                                     |
 *     s1 ====== tcp ====== local server forwarder ---+                |
 *                                                    + one udp socket +
 *     s2 ====== tcp ====== local server forwarder ---+
 *
 **************************************************************************
 */

#ifdef __cplusplus
extern "C" {
#endif


#ifdef WIN32
    typedef SOCKET sock_t;
#else
    typedef int sock_t;
#endif



/* forward rule mode, client or server */
typedef enum forward_mode_
{
    forward_client_mode,    /* tcp's client should work with a client forwarder together */
    forward_server_mode,    /* server forwarder */
} forward_mode;

/* forward context,
 * one context mapping to one udp socket pair */
typedef void *forward_context;

/* forward rule */
typedef void *forward_rule;

/* create a forward context with the udp socket pair 
 * if using this in STUN mode. you need to STUN it by yourself.
 */
forward_context init_forward(sock_t udpsocket);

/*
 * destroy the context, and it's rules.
 * udp socket will not be closed, you should manage it by youself.
 */
void free_forward(forward_context context);

/* add a forward rule, return NULL if failed. */
forward_rule add_forward_rule (forward_context context, 
                               forward_mode mode, 
                               const char *service,
                               unsigned short port);

/* remove a forward rule */
void del_forward_rule (forward_rule rule);

/* log callback */
/* level: 0: debug, 1: info, 2: warning, 3: error */
void set_log_callback(void (*cb)(int level, const char *message));

/* debug current internal variables */
// void debug_dump(int fd);

#ifdef __cplusplus
} ;
#endif

#endif /* __t2u_h__ */
