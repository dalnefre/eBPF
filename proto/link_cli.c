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
prep_read(int if_index, void *buffer, size_t limit)
{
    if (limit < sizeof(msg_read_t)) return -1;  // too small
    memset(buffer, 0, limit);  // clear buffer
    msg_read_t *msg = buffer;
    msg->hdr.magic = MSG_MAGIC;
    msg->hdr.op_code = OP_READ;
    msg->hdr.if_index = if_index;
    return (sizeof(msg_none_t));
}

int
prep_write(int if_index, void *buffer, size_t limit, user_state_t *user)
{
    if (limit < sizeof(msg_read_t)) return -1;  // too small
    memset(buffer, 0, limit);  // clear buffer
    msg_read_t *msg = buffer;
    msg->hdr.magic = MSG_MAGIC;
    msg->hdr.op_code = OP_WRITE;
    msg->hdr.if_index = if_index;
    msg->user = *user;
    return (sizeof(msg_write_t));
}

static octet_t proto_buf[1024];  // message-transfer buffer

int
do_transaction(void *buffer, int size, size_t limit)
{
    int fd, rv, n; 
    struct sockaddr_storage address;
    socklen_t addr_len;

    fd = create_socket();
    if (fd < 0) {
        perror("create_socket() failed");
        return -1;  // failure
    }

    struct sockaddr *to_addr = set_sockaddr(&address, &addr_len); 
    DEBUG(dump_sockaddr(stdout, to_addr, addr_len));

    n = sendto(fd, buffer, size, 0, to_addr, addr_len);
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
client(int if_index, void *buffer, size_t limit)
{
    int n;

    n = prep_read(if_index, buffer, limit);
    if (n <= 0) {
        fprintf(stderr, "exceeded message buffer size of %zu\n", limit);
        return -1;  // failure
    }
    n = do_transaction(buffer, n, limit);
    if (n < 0) return -1;  // failure

    return 0;  // success
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

    if (proto_opt.if_index <= 0) {
        fprintf(stderr, "usage: %s if=<interface>\n", argv[0]);
        return 1;
    }

    rv = client(proto_opt.if_index, proto_buf, sizeof(proto_buf));

    return rv;
}
