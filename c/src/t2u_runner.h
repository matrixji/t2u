#ifndef __t2u_runner_h__
#define __t2u_runner_h__

/* run some function with userdata in current runner */
void t2u_runner_control(t2u_runner *runner, control_data *cdata);

/* alloc new t2u_event */
t2u_event *t2u_event_new();

/* cleanup t2u_event */
void t2u_delete_event(t2u_event *ev);

/* new a runner */
t2u_runner * t2u_runner_new();

/* delete runner */
void t2u_delete_runner(t2u_runner *runner);

/* check runner has context? */
int t2u_runner_has_context(t2u_runner *runner);

#endif /* __t2u_runner_h__ */