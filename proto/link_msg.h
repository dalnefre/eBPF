/*
 * link_msg.h -- Link client/server messaging
 */
#ifndef _LINK_MSG_H_
#define _LINK_MSG_H_

typedef enum {
    OP_NONE,
    OP_READ,
    OP_WRITE,
    OP_CODE_MAX
} op_code_t;

#define MSG_MAGIC (0xEC)

typedef struct msg_hdr {
    char        magic;          // magic number = MSG_MAGIC
    char        op_code;        // operation code (op_code_t)
    char        arg_1;          // first argument
    char        arg_2;          // second argument
} msg_hdr_t;

#endif /* _LINK_MSG_H_ */
