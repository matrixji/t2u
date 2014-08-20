#ifndef __t2u_session_h__
#define __t2u_session_h__

/* new session while connecting */
t2u_session *t2u_add_connecting_session(t2u_rule *rule, sock_t sock, uint64_t handle);

/* delete unestablished session */
void t2u_delete_connecting_session(t2u_session *session);

/* delete established session */
void t2u_delete_connected_session(t2u_session *session);

/* delete established session later, mark deleting, delete it when send queue flushed */
void t2u_delete_connected_session_later(t2u_session *session);

/* test and try delete session after delete later */
void t2u_try_delete_connected_session(t2u_session *session);

/* handler for connect response */
void t2u_session_handle_connect_response(t2u_session *session, t2u_message_data *mdata);

/* handler for data request */
void t2u_session_handle_data_request(t2u_session *session, t2u_message_data *mdata, int mdata_len);

/* tcp */
void t2u_session_process_tcp(evutil_socket_t sock, short events, void *arg);

/*
 * find the session in context using handle.
 * is connected = 1, find in established sessions, else in connecting sessions.
 */
t2u_session *find_session_in_context(t2u_context *context, uint64_t handle, int connected);

#endif /* __t2u_session_h__ */