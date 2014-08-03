#ifndef __t2u_message_h__
#define __t2u_message_h__

/* add a t2u_message to send queue and do a sent */
t2u_message *t2u_add_request_message(t2u_session *session, char *payload, int payload_len);

/* delete a t2u_message */
void t2u_delete_request_message(t2u_message *message);

/* handle data response */
void t2u_message_handle_data_response(t2u_message *message, t2u_message_data *mdata);

/* handle retrans request */
void t2u_message_handle_retrans_request(t2u_message *message, t2u_message_data *mdata);

#endif /* __t2u_message_h__ */