#ifndef __t2u_runner_h__
#define __t2u_runner_h__

typedef struct control_data_
{
    void (* func_) (void *);        /* callback function */
    void *arg_;
    int error_;                     /* callback error code */
} control_data;

typedef struct t2u_runner_
{
    t2u_mutex_t mutex_;             /* mutex */
    t2u_cond_t cond_;               /* event for runner stop event */
    struct event_base *base_;       /* event base */
    struct rbtree *event_tree_;     /* all event base, <event *, > */
    int running_;                   /* 0 not running, 1 for running */
    t2u_thr_t thread_;              /* main run thread handle */
    t2u_thr_id tid_;                /* main run thread id */
    evutil_socket_t sock_[2];       /* control socket for internal message */
    struct event* event_;           /* control event for internal message processing */
} t2u_runner;


/* start the runner */
void t2u_runner_start(t2u_runner *runner);

/* stop the runner */
void t2u_runner_stop(t2u_runner *runner);

/* init the runner */
t2u_runner *t2u_runner_new();

/* destroy the runner */
void t2u_runner_delete(t2u_runner *runner);

/* add context */
int t2u_runner_add_context(t2u_runner *runner, t2u_context *context);

/* del context */
int t2u_runner_delete_context(t2u_runner *runner, t2u_context *context);

/* add rule */
int t2u_runner_add_rule(t2u_runner *runner, t2u_rule *rule);

/* delete rule */
int t2u_runner_delete_rule(t2u_runner *runner, t2u_rule *rule);

/* add session */
int t2u_runner_add_session(t2u_runner *runner, t2u_session *session);

/* delete session */
int t2u_runner_delete_session(t2u_runner *runner, t2u_session *session);

void t2u_runner_control(t2u_runner *runner, control_data *cdata);


#endif /* __t2u_runner_h__ */
