#ifndef __t2u_message_h__
#define __t2u_message_h__

#include <stdint.h>



class t2u_message
{
public:
    t2u_message();
    

public:
    typedef enum {
        t2u_connect,
        t2u_
    } t2u_operation;

private:
    uint32_t seq_;          // global seq
    uint32_t ack_;          // global ack
    uint32_t operation_;    // operation code
    uint32_t csig_;         // client signature
    uint32_t ssig_;         // server signature

};

















#endif // __t2u_message_h__
