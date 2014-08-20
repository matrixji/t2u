#ifndef __t2u_internal_h__
#define __t2u_internal_h__

#include <time.h>
#include "t2u_thread.h"
#include "t2u_rbtree.h"

#ifdef __GNUC__
#include <netinet/in.h>
#endif

#if defined _MSC_VER

#ifndef int32_t
typedef __int32 int32_t;
#endif

#ifndef uint32_t
typedef unsigned __int32 uint32_t;
#endif

#ifndef int16_t
typedef __int16 int16_t;
#endif

#ifndef uint16_t
typedef unsigned __int16 uint16_t;
#endif

typedef int socklen_t;

#endif /* _MSC_VER */


#if defined __GNUC__
    #define closesocket close
#endif

/* log helpers */
typedef void (*log_callback)(int, const char *);
typedef void (*error_callback)(forward_context, forward_rule, int , char *);
typedef void (*unknown_callback)(forward_context, const char *, size_t);

log_callback get_log_func_();
error_callback get_error_func_();
unknown_callback get_unknown_func_();

#if defined _MSC_VER
#define LOG_(level, ...) do { \
    if (get_log_func_()) { \
        char mess_[1024]; \
        time_t t; \
        struct tm tmp; \
        t = time(NULL); \
        localtime_s(&tmp, &t); \
        char ts[64]; \
        strftime(ts, sizeof(ts), "%y-%m-%d %H:%M:%S", &tmp); \
        int n = sprintf_s(mess_, sizeof(mess_), "[%s] [%s:%d] ", ts, __FILE__, __LINE__); \
        n += sprintf_s(&mess_[n], sizeof(mess_)-n, ##__VA_ARGS__); \
        if (n < (int)sizeof(mess_) - 2) { \
            if (mess_[n-1] != '\n') {\
                mess_[n++] = '\n'; \
                mess_[n++] = '\0'; \
                                    } \
            get_log_func_() (level, mess_) ; \
                        } \
            } \
} while (0)
#else
#define LOG_(level, fmt...) do { \
    if (get_log_func_()) { \
        char mess_[1024]; \
        time_t t; \
        struct tm tmp; \
        t = time(NULL); \
        localtime_r(&t, &tmp); \
        char ts[64]; \
        strftime(ts, sizeof(ts), "%y-%m-%d %H:%M:%S", &tmp); \
        int n = sprintf(mess_, "[%s] [%s:%d] ", ts, __FILE__, __LINE__); \
        n += sprintf(&mess_[n], ##fmt); \
        if (n < (int)sizeof(mess_) - 2) { \
            if (mess_[n-1] != '\n') {\
                mess_[n++] = '\n'; \
                mess_[n++] = '\0'; \
            } \
            get_log_func_() (level, mess_) ; \
        } \
    } \
} while (0)
#endif

/* event with data */
typedef struct t2u_event_
{
    struct event *event_;           /* the event */
    struct event *extra_event_;     /* the event */
    struct t2u_runner_ *runner_;    /* the runner */
    struct t2u_context_ *context_;  /* the context */
    struct t2u_rule_ *rule_;        /* the rule */
    struct t2u_session_ *session_;  /* the session */
    struct t2u_message_ *message_;  /* the session message if used */
    int err_;                       /* last operation result. 0 for ok */
} t2u_event;


/* oper type */
enum t2u_mess_oper
{
    connect_request,
    connect_response,
    close_request,
    close_response,
    data_request,
    data_response,
    retrans_request,
};


/* t2u udp message */
typedef struct t2u_message_data_
{
    uint32_t magic_;            /* magic number, see T2U_MESS_MAGIC */
    uint16_t version_;          /* version, current 0x0001 */
    uint16_t oper_;             /* operation code, see t2u_mess_oper */
    uint64_t handle_;           /* handle for mapping session */
    uint32_t seq_;              /* handle based seq */
    char payload[0];            /* payload */
} t2u_message_data;

#define T2U_PAYLOAD_MAX (1400)
#define T2U_MESS_BUFFER_MAX (T2U_PAYLOAD_MAX + sizeof(t2u_message_data))
#define T2U_MESS_MAGIC (0x5432552E) /* "T2U." */

typedef struct t2u_message_
{
    struct t2u_session_ *session_;  /* parent session */
    t2u_message_data *data_;        /* message to send or recv */
    size_t len_;                    /* length of message */
    uint32_t seq_;                  /* session based seq */
    unsigned long send_retries_;    /* retry send count */
    t2u_event *ev_timeout_;         /* timeout event */
} t2u_message;

/* session */
typedef struct t2u_session_
{
    struct t2u_rule_ *rule_;                /* parent rule */
    sock_t sock_;                           /* with the socket */
    uint64_t handle_;                       /* handle */
    int status_;                            /* 0 for non, 1 for connecting, 2 for establish, 3 for closing */
    uint32_t send_buffer_count_;
    uint32_t send_seq_;                     /* send seq */
    rbtree *send_mess_;                     /* send message list */
    uint32_t recv_buffer_count_;
    uint32_t recv_seq_;                     /* recv seq */
    rbtree *recv_mess_;                     /* recv message list */
    unsigned long connect_retries_;         /* retry count */
    t2u_event *ev_;                         /* the connect,data event */
    uint32_t retry_seq_;                    /* retry seq */
    time_t last_send_ts_;                   /* timestamp for timeout check */
} t2u_session;

typedef struct t2u_rule_
{
    forward_mode mode_;             /* mode c/s */
    sock_t listen_sock_;            /* listen socket if in client mode */
    t2u_event *ev_listen_;          /* event for listen socket */
    char *service_;                 /* service name */
    struct t2u_context_ *context_;  /* the context */
    rbtree *sessions_;              /* sub sessions */
    rbtree *connecting_sessions_;   /* sessions not in establish */
    struct sockaddr_in conn_addr_;  /* address for connect if server mode */

    unsigned long utimeout_;        /* timeout for message */
    unsigned long uretries_;        /* retries for message */
    unsigned long udp_slide_window_;/* slide window for udp packets */
    unsigned long session_timeout_; /* session timeout in seconds */

} t2u_rule;

typedef struct t2u_context_
{
    sock_t sock_;
    struct t2u_runner_ *runner_;
    rbtree *rules_;
    t2u_event *ev_udp_;

    unsigned long utimeout_;        /* timeout for message */
    unsigned long uretries_;        /* retries for message */
    unsigned long udp_slide_window_;/* slide window for udp packets */
    unsigned long session_timeout_; /* session timeout in seconds */

    int debug_bandwidth_;           /* simulate bandwidth in bit/second */
    int debug_latency_;
    int debug_packet_loss_;
    unsigned long long 
        debug_bytes_sent_;
    unsigned long long 
        debug_bytes_drop_;
    unsigned long long 
        debug_bw_time_start_;
} t2u_context;

typedef struct t2u_runner_
{
    t2u_mutex_t mutex_;             /* mutex */
    t2u_cond_t cond_;               /* event for runner stop event */
    rbtree *contexts_;              /* context tree */
    struct event_base *base_;       /* event base */
    int running_;                   /* 0 not running, 1 for running */
    t2u_thr_t thread_;              /* main run thread handle */
    t2u_thr_id tid_;                /* main run thread id */
    evutil_socket_t sock_[2];       /* control socket for internal message */
    struct event* control_event_;   /* control event for internal message processing */
} t2u_runner;


typedef struct control_data_
{
    /* callback function */
    void (* func_) (t2u_runner *, void *); 
    void *arg_;
    // int error_;     /* callback error code */
} control_data;

#if defined(__x86_64__) || defined(_WIN64)
#define ntoh64(x) ntohl(x)
#define hton64(x) htonl(x)
#else
#define ntoh64(x) (((uint64_t)ntohl((x)&0xffffffff) << 32) | ((uint64_t)ntohl((x)>>32)))
#define hton64(x) (((uint64_t)htonl((x)&0xffffffff) << 32) | ((uint64_t)htonl((x)>>32)))
#endif


#include "t2u_runner.h"
#include "t2u_context.h"
#include "t2u_rule.h"
#include "t2u_session.h"
#include "t2u_message.h"


#endif /* __t2u_internal_h__ */
