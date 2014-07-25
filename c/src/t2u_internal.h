#ifndef __t2u_internal_h__
#define __t2u_internal_h__

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

#endif /* _MSC_VER */


#if defined __GNUC__
    #define closesocket close
#endif

/* log helpers */
typedef void(* log_callback)(int,const char *);
typedef void (*error_callback)(forward_context, forward_rule, int , char *);
typedef void (*unknown_callback)(forward_context, const char *, size_t);

log_callback get_log_func_();
error_callback get_error_func_();
unknown_callback get_unknown_func_();

#if defined _MSC_VER
#define LOG_(level, ...) do { \
    if (get_log_func_()) { \
        char mess_[1024]; \
        int n = sprintf(mess_, "[%s:%d] ", __FILE__, __LINE__); \
        n += sprintf(&mess_[n], ##__VA_ARGS__); \
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
        int n = sprintf(mess_, "[%s:%d] ", __FILE__, __LINE__); \
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
typedef struct t2u_event_data_
{
    struct event *event_;           /* the event */
    void *runner_;                  /* the runner */
    void *context_;                 /* the context */
    void *rule_;                    /* the rule */
    void *session_;                 /* the session */
    void *session_message_;         /* the session message if used */
    sock_t sock_;                   /* the socket */
    int err_;                       /* last operation result. 0 for ok */
    struct event *extra_event_;     /* the event */
    char *message_;                 /* with message data */
} t2u_event_data;


/* delete session */
void del_forward_session(void *s);

/* delete session */
void del_forward_session_later(void *s);

#endif /* __t2u_internal_h__ */
