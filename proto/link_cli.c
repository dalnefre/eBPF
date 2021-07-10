/*
 * link_cli.c -- link protocol lab client
 */
#include "proto.h"
#include "util.h"
#include "code.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "link_msg.h"

#define DEBUG(x) x /**/
#define TRACE(x)   /**/

void
dump_link_state(FILE *f, user_state_t *user, link_state_t *link)
{
    fprintf(f, "outbound[44] =\n");
    hexdump(f, user->outbound, 44);

    __u32 uf = user->user_flags;
    fprintf(f, "user_flags = 0x%08lx (%c%c%c%c)\n",
        (unsigned long)uf,
        '-',
        (GET_FLAG(uf, UF_STOP) ? 'R' : 's'),
        (GET_FLAG(uf, UF_FULL) ? 'F' : 'e'),
        (GET_FLAG(uf, UF_BUSY) ? 'B' : 'r'));

    fprintf(f, "inbound[44] =\n");
    hexdump(f, link->inbound, 44);

    __u32 lf = link->link_flags;
    fprintf(f, "link_flags = 0x%08lx (%c%c%c%c%c%c%c%c)\n",
        (unsigned long)lf,
        '-',
        (GET_FLAG(lf, LF_RECV) ? 'R' : '-'),
        (GET_FLAG(lf, LF_SEND) ? 'S' : '-'),
        (GET_FLAG(lf, LF_FULL) ? 'F' : 'e'),
        (GET_FLAG(lf, LF_BUSY) ? 'B' : 'r'),
        (GET_FLAG(lf, LF_ENTL) ? '&' : '-'),
        (GET_FLAG(lf, LF_ID_B) ? 'B' : '-'),
        (GET_FLAG(lf, LF_ID_A) ? 'A' : '-'));

    fprintf(f, "frame[64] =\n");
    hexdump(f, link->frame, 64);

    fprintf(f, "i,u = (%o,%o)\n", link->i, link->u);
    fprintf(f, "len = %u\n", link->len);
    fprintf(f, "seq = 0x%08lx\n", (unsigned long)link->seq);
}

int
create_message(void *buffer, size_t size,
    op_code_t op_code, int if_index, user_state_t *user)
{
    if (size < sizeof(msg_hdr_t)) return -1;  // too small
    memset(buffer, 0, size);  // clear buffer
    msg_hdr_t *hdr = buffer;
    hdr->magic = MSG_MAGIC;
    hdr->op_code = op_code;
    switch (op_code) {
        case OP_READ: {
            msg_none_t *msg = buffer;
            msg->hdr.if_index = if_index;
            size = sizeof(msg_none_t);
            break;
        }
        case OP_WRITE: {
            msg_write_t *msg = buffer;
            msg->hdr.if_index = if_index;
            msg->user = *user;
            size = sizeof(msg_none_t);
            break;
        }
        default: {
            size = sizeof(msg_none_t);
            break;
        }
    }
    return size;
}

static octet_t proto_buf[1024];  // message-transfer buffer

int
client(void *buffer, size_t limit)
{
    int fd, rv, n; 
    struct sockaddr_storage address;
    socklen_t addr_len;

    fd = create_socket();
    if (fd < 0) {
        perror("create_socket() failed");
        return -1;  // failure
    }

    n = create_message(buffer, limit, OP_READ, 3, NULL);
    if (n <= 0) {
        fprintf(stderr, "exceeded message buffer size of %zu\n", limit);
        return -1;  // failure
    }

    struct sockaddr *to_addr = set_sockaddr(&address, &addr_len); 
    DEBUG(dump_sockaddr(stdout, to_addr, addr_len));

    n = sendto(fd, buffer, n, 0, to_addr, addr_len);
    if (n < 0) {
        perror("sendto() failed");
        return -1;  // failure
    }

    TRACE(dump_sockaddr(stdout, to_addr, addr_len));
    fputs("Message: \n", stdout);
    hexdump(stdout, buffer, n);

    fputs("Awaiting reply...\n", stdout);
    memset(buffer, 0, limit);  // clear buffer
    struct sockaddr *from_addr = clr_sockaddr(&address, &addr_len); 
    n = recvfrom(fd, buffer, limit, 0, from_addr, &addr_len);
    if (n < 0) {
        perror("recvfrom() failed");
        return -1;  // failure
    }

    DEBUG(dump_sockaddr(stdout, to_addr, addr_len));
    fputs("Reply: \n", stdout);
    TRACE(hexdump(stdout, buffer, n));

    msg_read_t *msg = buffer;
    user_state_t *user = &msg->user;
    link_state_t *link = &msg->link;
    dump_link_state(stdout, user, link);

    rv = close(fd);
    return rv;
}

int
main(int argc, char *argv[])
{
    // set new default protocol options
    proto_opt.family = AF_INET;
    proto_opt.sock_type = SOCK_DGRAM;
    proto_opt.ip_proto = IPPROTO_UDP;

    int rv = parse_args(&argc, argv);
    if (rv != 0) return rv;

    fputs(argv[0], stdout);
    print_proto_opt(stdout);

    rv = client(proto_buf, sizeof(proto_buf));

    return rv;
}
