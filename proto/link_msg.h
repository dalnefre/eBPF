/*
 * link_msg.h -- Link client/server messaging
 */
#ifndef _LINK_MSG_H_
#define _LINK_MSG_H_

#include "../include/link.h"

typedef enum {
    OP_NONE,
    OP_READ,
    OP_WRITE,
    OP_CODE_MAX
} op_code_t;

#define MSG_MAGIC (0xEC)

typedef struct msg_hdr {
    char            magic;          // magic number = MSG_MAGIC
    char            op_code;        // operation code (op_code_t)
    char            if_index;       // interface index
    char            _reserved_;     // reserved for future use
} msg_hdr_t;

typedef struct msg_none {
    msg_hdr_t       hdr;            // message header
} msg_none_t;

typedef struct msg_read {
    msg_hdr_t       hdr;            // message header
    user_state_t    user;           // user state
    link_state_t    link;           // link state
} msg_read_t;

typedef struct msg_write {
    msg_hdr_t       hdr;            // message header
    user_state_t    user;           // user state
} msg_write_t;

#endif /* _LINK_MSG_H_ */
