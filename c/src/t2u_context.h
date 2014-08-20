#ifndef __t2u_context_h__
#define __t2u_context_h__

/* init */
t2u_context * t2u_add_context(t2u_runner *runner, sock_t sock);

/* context free */
void t2u_delete_context(t2u_context *context);

/* sene message data */
void t2u_send_message_data(t2u_context *context, char *data, size_t size, t2u_session * session);

#endif /* __t2u_context_h__ */