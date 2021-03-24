/*
 * link.h -- Liveness and AIT link protocols
 */
#ifndef _LINK_H_
#define _LINK_H_

#include <linux/types.h>

#ifndef ETH_P_DALE
#define ETH_P_DALE (0xDa1e)
#endif

#define MAX_PAYLOAD 44  // maxiumum number of AIT data octets

typedef enum {
    Init,       // = 0
    Ping,       // = 1
    Pong,       // = 2
    Got_AIT,    // = 3
    Ack_AIT,    // = 4
    Ack_Ack,    // = 5
    Proceed,    // = 6
    Error       // = 7
} protocol_t;

typedef struct user_state {
    __u8        outbound[44];   // outbound data buffer
    __u32       user_flags;     // flags controller by user
} user_state_t;

typedef struct link_state {
    __u8        inbound[44];    // inbound data buffer
    __u32       link_flags;     // flags controller by link
    __u8        frame[64];      // transport frame
    protocol_t  i;              // local protocol state
    protocol_t  u;              // remote protocol state
    __u16       len;            // payload length
    __u32       seq;            // sequence number
} link_state_t;

#define PROTO(i, u) (0200 | ((i) & 07) << 3 | ((u) & 07))
#define PARSE_PROTO(i, u, b) ({ \
    i = ((b) & 0070) >> 3;      \
    u = ((b) & 0007);           \
})

#define GET_FLAG(lval,rval) !!((lval) & (rval))
#define SET_FLAG(lval,rval) (lval) |= (rval)
#define CLR_FLAG(lval,rval) (lval) &= ~(rval)

#define LF_ID_A (((__u32)1)<<0) // endpoint role Alice
#define LF_ID_B (((__u32)1)<<1) // endpoint role Bob
#define LF_ENTL (((__u32)1)<<2) // link entangled
#define LF_FULL (((__u32)1)<<3) // outbound AIT full
#define LF_VALD (((__u32)1)<<4) // inbound AIT valid
#define LF_SEND (((__u32)1)<<5) // link sending AIT
#define LF_RECV (((__u32)1)<<6) // link receiving AIT

#define UF_FULL (((__u32)1)<<0) // inbound AIT full
#define UF_VALD (((__u32)1)<<1) // outbound AIT valid
#define UF_STOP (((__u32)1)<<2) // run=1, stop=0

#endif /* _LINK_H_ */
