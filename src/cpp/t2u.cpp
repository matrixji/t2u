#include "t2u.h"
#include "t2u_internal.h"

#include "t2u_message.h"

static void(*log_callback_func_)(int, const char *) = NULL;

// log callback
void set_log_callback(void (*cb)(int level, const char *mess))
{
    log_callback_func_ = cb;
}


// check packet 
int is_valid_t2u_packet(const char *buffer, size_t length)
{
    if (t2u_message::is_valid(buffer, length))
    {
        return 0; // valid
    }
    else
    {
        return -1;
    }
}