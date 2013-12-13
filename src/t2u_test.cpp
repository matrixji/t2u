#include <stdio.h>
#include "t2u.h"
#include <sys/types.h>
#include <sys/socket.h>

/**************************************************************************
 *  this is a small demo
 **************************************************************************
 */

int main(int argc, char *argv[])
{
    forward_context *context;
    forward_rule rule = { forward_client_mode, NULL, "http", 0, 1088 };

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    context=init_forward(sock);

    rule.context = context;
    add_forward_rule(&rule);
    
    free_forward(context);
    
    return 0;
}
