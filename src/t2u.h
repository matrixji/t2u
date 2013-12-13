#ifndef __t2u_h__
#define __t2u_h__


#ifdef WIN32
#else
typedef int t2u_socket;
#endif

/* forward rule mode, client for server */
typedef enum forward_mode_
{
    forward_client_mode,
    forward_server_mode,
}forward_mode;

/* forward context,
 * one context mapping to one udp socket pair */
typedef struct forward_context_ 
{
    int sock;
} forward_context;

/* forward rule */
typedef struct forward_rule_
{
    forward_mode mode;          /* client or server mode */
    const char *service;        /* service name, for identify the service */
    const char *address;        /* remote tcp address, only use for server mode */
    unsigned short *port;       /* tcp port, listen for client. connect for server */
} forward_rule;

forward_context *start_forward(t2u_socket udpsocket);
void stop_forward(forward_context *context);

/* add a forwarder, return 0 if success. */
int add_forward_rule (forward_context *context, forward_rule *rule);
int remove_forward_rule (forward_context *context, forward_rule *rule);

#endif
