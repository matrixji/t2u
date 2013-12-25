#ifndef __t2u_message_h__
#define __t2u_message_h__

#include <stdint.h>

///////////////////////////////////////////////////////////////////////////
// t2u message on udp
// +-------------------------------------------------------+
// |   4 magic   | 4 operation |    4 seq    |    4 ack    |
// +-------------------------------------------------------+
// |   4 chdl    |   4 shdl    |         RESERVED          |
// +-------------------------------------------------------+
// | payload (optional)                                    |
// |                           +---------------------------+
// |                           |
// +----------------------------
//

class t2u_message
{
public:
    t2u_message();

    uint32_t seq();
    
    static bool is_valid(const char *buffer, size_t length);
    
public:
    typedef enum {
        t2u_connect_request,
        t2u_connect_response,
    } t2u_operation;

private:
    uint32_t magic_;        // magic number
    uint32_t operation_;    // operation code
    uint32_t seq_;          // global seq
    uint32_t ack_;          // global ack
    uint32_t chdl_;         // client handle
    uint32_t shdl_;         // server handle
    char service_[16];      // service name, max to 16.

};

















#endif // __t2u_message_h__
