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

#define IF_INDEX_MAX (16)

static user_state_t user_state[IF_INDEX_MAX];  // user state by if_index

user_state_t *
get_user_state(int if_index)
{
    if ((if_index > 0)
    &&  (if_index < IF_INDEX_MAX)) {
        return &user_state[if_index];
    }
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
    return NULL;
}

static octet_t proto_buf[1024];  // message-transfer buffer

int
transform(void *buffer, int n)
{
    if (n < sizeof(msg_hdr_t)) return -1;  // fail!
    msg_hdr_t *hdr = buffer;
    if (hdr->magic != MSG_MAGIC) return -1;  // fail!
    switch (hdr->op_code) {
        case OP_NONE: {
            // echo message as reply
            break;
        }
        case OP_READ: {
            user_state_t *user = get_user_state(hdr->if_index);
            if (!user) return -1;  // fail!
            link_state_t *link = get_link_state(hdr->if_index);
            if (!link) return -1;  // fail!

            msg_read_t *reply = buffer;
            reply->user = *user;
            reply->link = *link;
            n = sizeof(msg_read_t);

            break;
        }
        case OP_WRITE: {
            user_state_t *user = get_user_state(hdr->if_index);
            if (!user) return -1;  // fail!
            link_state_t *link = get_link_state(hdr->if_index);
            if (!link) return -1;  // fail!

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

//        DEBUG(dump_sockaddr(stdout, addr, addr_len));
        fputs("Reply: \n", stdout);
        hexdump(stdout, buffer, n);
    }

    rv = close(fd);
    return rv;
}

static int
simulate_transfer(int if_index, __u8 *payload)
{
    user_state_t *user = get_user_state(if_index);
    if (!user) return -1;  // fail!
    link_state_t *link = get_link_state(if_index);
    if (!link) return -1;  // fail!

    if (!GET_FLAG(user->user_flags, UF_BUSY)
    &&  !GET_FLAG(link->link_flags, LF_FULL)) {
        memcpy(link->inbound, payload, MAX_PAYLOAD);
        SET_FLAG(link->link_flags, LF_FULL);
        return 0;  // success
    }

    return 1;  // try again later
}

int
simulated_network()
{
    int rv;

    while (usleep(1000) == 0) {  // 0.001 second = 1,000 microseconds
        for (int if_index = 1; if_index < IF_INDEX_MAX; ++if_index) {
            user_state_t *user = get_user_state(if_index);
            if (!user) return -1;  // fail!
            link_state_t *link = get_link_state(if_index);
            if (!link) return -1;  // fail!

            if (GET_FLAG(user->user_flags, UF_FULL)
            &&  !GET_FLAG(link->link_flags, LF_BUSY)) {
                int dst = (if_index ^ 0xF);  // calculate destination
                rv = simulate_transfer(dst, user->outbound);
                if (rv < 0) return -1;  // fail!
                if (rv == 0) {
                    SET_FLAG(link->link_flags, LF_BUSY);
                }
            }

            if (!GET_FLAG(user->user_flags, UF_FULL)
            &&  GET_FLAG(link->link_flags, LF_BUSY)) {
                CLR_FLAG(link->link_flags, LF_BUSY);
            }
        }
    }
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
