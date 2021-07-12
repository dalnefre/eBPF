/*
 * link_srv.c -- link protocol lab server
 */
#include "proto.h"
#include "util.h"
#include "code.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "link_msg.h"

#define DEBUG(x) x /**/
#define TRACE(x)   /**/

#define IF_INDEX_MAX (16)

void
dump_state(FILE *f, int if_index, user_state_t *user, link_state_t *link)
{
    fprintf(f, "[%d] ", if_index);
    __u32 uf = user->user_flags;
    fprintf(f, "user@%p: %c%c%c%c ",
        user,
        '-',
        (GET_FLAG(uf, UF_STOP) ? 'R' : 's'),
        (GET_FLAG(uf, UF_FULL) ? 'F' : 'e'),
        (GET_FLAG(uf, UF_BUSY) ? 'B' : 'r'));
    __u32 lf = link->link_flags;
    fprintf(f, "link@%p: %c%c%c%c%c%c%c%c\n",
        link,
        '-',
        (GET_FLAG(lf, LF_RECV) ? 'R' : '-'),
        (GET_FLAG(lf, LF_SEND) ? 'S' : '-'),
        (GET_FLAG(lf, LF_FULL) ? 'F' : 'e'),
        (GET_FLAG(lf, LF_BUSY) ? 'B' : 'r'),
        (GET_FLAG(lf, LF_ENTL) ? '&' : '-'),
        (GET_FLAG(lf, LF_ID_B) ? 'B' : '-'),
        (GET_FLAG(lf, LF_ID_A) ? 'A' : '-'));
}

static user_state_t user_state[IF_INDEX_MAX];  // user state by if_index

user_state_t *
get_user_state(int if_index)
{
    if ((if_index > 0)
    &&  (if_index < IF_INDEX_MAX)) {
        return &user_state[if_index];
    }
    DEBUG(fprintf(stderr, "get_user_state(%d) FAIL!\n", if_index));
    return NULL;
}

static link_state_t link_state[IF_INDEX_MAX];  // link state by if_index

link_state_t *
get_link_state(int if_index)
{
    if ((if_index > 0)
    &&  (if_index < IF_INDEX_MAX)) {
        return &link_state[if_index];
    }
    DEBUG(fprintf(stderr, "get_link_state(%d) FAIL!\n", if_index));
    return NULL;
}

int
get_state(int if_index, user_state_t **uptr, link_state_t **lptr)
{
    *uptr = get_user_state(if_index);
    if (!*uptr) return -1;  // fail!
    *lptr = get_link_state(if_index);
    if (!*lptr) return -1;  // fail!
    DEBUG(dump_state(stdout, if_index, *uptr, *lptr));
    return 0;  // success
}

static octet_t proto_buf[1024];  // message-transfer buffer

int
transform(void *buffer, int n)
{
    user_state_t *user;
    link_state_t *link;

    if (n < sizeof(msg_hdr_t)) return -1;  // fail!
    msg_hdr_t *hdr = buffer;
    if (hdr->magic != MSG_MAGIC) return -1;  // fail!
    switch (hdr->op_code) {
        case OP_NONE: {
            // echo message as reply
            break;
        }
        case OP_READ: {
            if (get_state(hdr->if_index, &user, &link) < 0) return -1;

            msg_read_t *reply = buffer;
            reply->user = *user;
            reply->link = *link;
            n = sizeof(msg_read_t);

            break;
        }
        case OP_WRITE: {
            if (get_state(hdr->if_index, &user, &link) < 0) return -1;

            if (n < sizeof(msg_write_t)) return -1;  // fail!
            msg_write_t *msg = buffer;
            *user = msg->user;

            msg_read_t *reply = buffer;
            reply->user = *user;
            reply->link = *link;
            n = sizeof(msg_read_t);

            break;
        }
        default: {
            hdr->magic = -1;
//            n = 1;
            break;
        }
    }
    return n;  // success -- number of characters in reply
}

int
server(void *buffer, size_t limit)
{
    int fd, rv, n;
    struct sockaddr_storage address;
    socklen_t addr_len;

    fd = create_socket();
    if (fd < 0) {
        perror("create_socket() failed");
        return -1;  // failure
    }

    rv = bind_socket(fd);
    if (rv < 0) {
        perror("bind_socket() failed");
        return -1;  // failure
    }

    while (true) {
        memset(buffer, 0, limit);  // clear buffer
        struct sockaddr *addr = clr_sockaddr(&address, &addr_len); 
        n = recvfrom(fd, buffer, limit, 0, addr, &addr_len);
        if (n < 0) {
            perror("recvfrom() failed");
            return -1;  // failure
        }

        DEBUG(dump_sockaddr(stdout, addr, addr_len));
        fputs("Message: \n", stdout);
        hexdump(stdout, buffer, n);

        n = transform(buffer, n);
        if (n < 0) {
            fputs("Fail!\n", stdout);
            continue;  // skip reply on error
        }

        n = sendto(fd, buffer, n, 0, addr, addr_len);
        if (n < 0) {
            perror("sendto() failed");
            return -1;  // failure
        }

        TRACE(dump_sockaddr(stdout, addr, addr_len));
        fputs("Reply: \n", stdout);
        hexdump(stdout, buffer, n);
    }

    rv = close(fd);
    return rv;
}

static int
simulate_transfer(int if_index, __u8 *payload)
{
    user_state_t *user;
    link_state_t *link;

    if (get_state(if_index, &user, &link) < 0) {
        return -1;  // fail!
    }
    if (!GET_FLAG(user->user_flags, UF_BUSY)
    &&  !GET_FLAG(link->link_flags, LF_FULL)) {
        DEBUG(fputs("Transfer: \n", stdout));
        DEBUG(hexdump(stdout, payload, MAX_PAYLOAD));
        memcpy(link->inbound, payload, MAX_PAYLOAD);
        SET_FLAG(link->link_flags, LF_FULL);
        return 0;  // success
    }

    return 1;  // try again later
}

//#define NETWORK_PERIOD 1000  // 0.001 seconds = 1,000 microseconds
#define NETWORK_PERIOD 3000000  // 3 second = 3,000,000 microseconds

int
simulated_network()
{
    int rv, if_index;

    while (usleep(NETWORK_PERIOD) == 0) {
        DEBUG(fputs("IF scan...\n", stdout));
        for (if_index = 1; if_index < IF_INDEX_MAX; ++if_index) {
            user_state_t *user;
            link_state_t *link;

            if (get_state(if_index, &user, &link) < 0) {
                return -1;  // fail!
            }

            if (GET_FLAG(user->user_flags, UF_FULL)
            &&  !GET_FLAG(link->link_flags, LF_BUSY)) {
                int dst = (if_index ^ 0xF);  // calculate destination
                DEBUG(printf("AIT %d->%d\n", if_index, dst));
                rv = simulate_transfer(dst, user->outbound);
                if (rv < 0) return -1;  // fail!
                if (rv == 0) {
                    DEBUG(printf("(%d) SET LF_BUSY\n", if_index));
                    SET_FLAG(link->link_flags, LF_BUSY);
                }
            }

            if (!GET_FLAG(user->user_flags, UF_FULL)
            &&  GET_FLAG(link->link_flags, LF_BUSY)) {
                DEBUG(printf("(%d) CLR LF_BUSY\n", if_index));
                CLR_FLAG(link->link_flags, LF_BUSY);
            }
        }
    }
    DEBUG(fputs("timer fail!\n", stderr));
    return 0;  // success
}

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

int
main(int argc, char *argv[])
{
    int rv;
    pid_t pid;

    // set new default protocol options
    proto_opt.family = AF_INET;
    proto_opt.sock_type = SOCK_DGRAM;
    proto_opt.ip_proto = IPPROTO_UDP;

    rv = parse_args(&argc, argv);
    if (rv != 0) return rv;

    fputs(argv[0], stdout);
    print_proto_opt(stdout);

    // create API server process
    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        return -1;  // failure
    } else if (pid == 0) {  // child process
        rv = server(proto_buf, sizeof(proto_buf));
        fprintf(stderr, "server exit=%d\n", rv);
        exit(rv);
    }
    fprintf(stderr, "server pid=%d\n", pid);

    // create simulated network process
    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        return -1;  // failure
    } else if (pid == 0) {  // child process
        rv = simulated_network();
        fprintf(stderr, "network exit=%d\n", rv);
        exit(rv);
    }
    fprintf(stderr, "network pid=%d\n", pid);

    // ignore termination signals so we can clean up children
    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    // wait for child processes to exit
    fflush(stdout);
    for (;;) {
        rv = wait(NULL);
        if (rv < 0) {
            if (errno != ECHILD) {
                perror("wait() failed");
            }
            break;
        }
    }
    fputs("parent exit.\n", stderr);

    return 0;  // success
}
