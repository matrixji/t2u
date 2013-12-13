#include "t2u.h"

/**************************************************************************
 *  this is a small demo
 **************************************************************************
 */

int main(int argc, char *argv[])
{
    forward_context *c[10];

    printf ("init forward %p\n", c[0]=init_forward(1));
    printf ("init forward %p\n", c[1]=init_forward(1));
    printf ("init forward %p\n", c[2]=init_forward(2));

    free_forward(c[0]);
    free_forward(c[1]);
    free_forward(c[2]);
    return 0;
}
