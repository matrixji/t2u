#ifndef __t2u_rule_h__
#define __t2u_rule_h__

/* add a new rule */
t2u_rule *t2u_add_rule(t2u_context *context, forward_mode mode, 
                       const char *service, const char *addr, unsigned short port);

/* delete a rule */
void t2u_delete_rule(t2u_rule *rule);

/* handle connect request in t2u data (udp) */
void t2u_rule_handle_connect_request(t2u_rule *rule, t2u_message_data *mdata);


#endif /* __t2u_rule_h__ */