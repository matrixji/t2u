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

#include <sys/types.h>


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
forward_context create_forward_sock(sock_t s);

/*
 * create a forward context with send callback
 * this is useful if you want to use the udp socket pair do something extra.
 *
 * when forward context need to send some udp packets, will call the send callback.
 * and you should call send_forward, when udp packets in.
 */
forward_context create_forward_io(void (*send_cb)(forward_context c, const char *buffer, size_t length));

/*
 * destroy the context, and it's rules.
 * udp socket will not be closed, you should manage it by youself.
 */
void free_forward(forward_context c);

/*
 * if you want to send data with the socket.
 * Note: if the forward is create by create_forward_io, call forward_send will 
 *       use the send_cb to send the udp message.
 *       So this function is only useful for send data while your context is 
 *       created by create_forward_sock. 
 *       You can send data by native call send/write while your context is
 *       created by create_forward_io
 *      
 */
forward_context forward_send(forward_context c, const char *buffer, size_t length);

/*
 * udp input handle function. if your create your context using create_forward_io
 * when packets in, you need forward it to context via the call: forward_input.
 *
 * If this packet is valid t2u packet and proceed, it will return 0. else -1.
 * This will always return -1, if context is created by create_forward_sock.
 * This will always return 0, if set_unknown_callback is set. as unknow packet 
 * is already proceed by unknown_cb.
 */
int forward_input(forward_context c, const char *buffer, size_t length);

/*
 * unknown packets callback.
 * when context recv some unknown packet, it has a chance to forward to your callbakc handler.
 */
int set_unknown_callback(void (*unknown_cb)(forward_context c, const char *buffer, size_t length));

/*
 * return 0, if the packet is a valid t2u packet.
 * only check the 4 bytes magic.
 */
int is_valid_t2u_packet(const char *buffer, size_t length);

/* add a forward rule, return NULL if failed. */
forward_rule add_forward_rule (forward_context c, 
                               forward_mode mode, 
                               const char *service,
                               unsigned short port);

/* remove a forward rule */
void del_forward_rule (forward_rule rule);

/* log callback */
/* level: 0: debug, 1: info, 2: warning, 3: error */
void set_log_callback(void (*cb)(int level, const char *mess));

/* debug current internal variables */
// void debug_dump(int fd);

#ifdef __cplusplus
} ;
#endif

#endif /* __t2u_h__ */
