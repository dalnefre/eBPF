/*
 * link_user.c -- XDP userspace control program
 *
 * Implement Liveness and AIT protocols in XDP
 */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

#include <bpf/bpf.h>

#include "code.h"
#include "../include/link.h"

#define DEBUG(x) x /**/

static const char *link_map_filename = "/sys/fs/bpf/xdp/globals/link_map";
static int link_map_fd;

#define ob_full(link)          GET_FLAG(link->link_flags, LF_FULL)
#define ob_valid(link)         GET_FLAG(link->user_flags, UF_VALD)
#define ob_set_valid(link)     SET_FLAG(link->user_flags, UF_VALD)
#define ob_clr_valid(link)     CLR_FLAG(link->user_flags, UF_VALD)
#define copy_payload(dst,src)  memcpy((dst), (src), MAX_PAYLOAD)
#define clear_payload(dst)     memset((dst), null, MAX_PAYLOAD)
#define ib_valid(link)         GET_FLAG(link->link_flags, LF_VALD)
#define ib_full(link)          GET_FLAG(link->user_flags, UF_FULL)
#define ib_set_full(link)      SET_FLAG(link->user_flags, UF_FULL)
#define ib_clr_full(link)      CLR_FLAG(link->user_flags, UF_FULL)

int
read_link_map(__u32 key, link_state_t *link)
{
    return bpf_map_lookup_elem(link_map_fd, &key, link);
}

int
write_link_map(__u32 key, link_state_t *link)
{
    return bpf_map_update_elem(link_map_fd, &key, link, BPF_ANY);
}

void
hexdump(FILE *f, void *data, size_t size)
{
    uint8_t *buffer = data;
    const int span = 16;
    size_t offset = 0;
    int i, j;

    while (offset < size) {
        fprintf(f, "%04zx:  ", offset);
        for (i = 0; i < span; ++i) {
            if (i == 8) {
                fputc(' ', f);  // gutter between 64-bit words
            }
            j = offset + i;
            if (j < size) {
                fprintf(f, "%02x ", buffer[j]);
            } else {
                fputs("   ", f);
            }
        }
        fputc(' ', f);
        fputc('|', f);
        for (i = 0; i < span; ++i) {
            j = offset + i;
            if (j < size) {
                uint8_t b = buffer[j];
                if ((0x20 <= b) && (b < 0x7F)) {
                    fprintf(f, "%c", (int)b);
                } else {
                    fputc('.', f);
                }
            } else {
                fputc(' ', f);
            }
        }
        fputc('|', f);
        fputc('\n', f);
        offset += span;
    }
    fflush(f);
}

void
dump_link_state(FILE *f, link_state_t *link)
{
    fprintf(stderr, "outbound[44] =\n");
    hexdump(stderr, link->outbound, 44);

    __u32 uf = link->user_flags;
    fprintf(stderr, "user_flags = 0x%08lx (%c%c%c%c)\n",
        (unsigned long)uf,
        '-',
        (GET_FLAG(uf, UF_STOP) ? 'R' : 's'),
        (GET_FLAG(uf, UF_VALD) ? 'V' : '-'),
        (GET_FLAG(uf, UF_FULL) ? 'F' : 'e'));

    fprintf(stderr, "inbound[44] =\n");
    hexdump(stderr, link->inbound, 44);

    __u32 lf = link->link_flags;
    fprintf(stderr, "link_flags = 0x%08lx (%c%c%c%c%c%c%c%c)\n",
        (unsigned long)lf,
        '-',
        (GET_FLAG(lf, LF_RECV) ? 'R' : '-'),
        (GET_FLAG(lf, LF_SEND) ? 'S' : '-'),
        (GET_FLAG(lf, LF_VALD) ? 'V' : '-'),
        (GET_FLAG(lf, LF_FULL) ? 'F' : 'e'),
        (GET_FLAG(lf, LF_ENTL) ? '&' : '-'),
        (GET_FLAG(lf, LF_ID_B) ? 'B' : '-'),
        (GET_FLAG(lf, LF_ID_A) ? 'A' : '-'));

    fprintf(stderr, "frame[64] =\n");
    hexdump(stderr, link->frame, 64);

    fprintf(stderr, "i,u = (%o,%o)\n", link->i, link->u);
    fprintf(stderr, "len = %u\n", link->len);
    fprintf(stderr, "seq = 0x%08lx\n", (unsigned long)link->seq);
}

int
init_link_map(int if_index)
{
    link_state_t link_state;
    link_state_t *link = &link_state;
    int fd;
    struct ifreq ifr;
    int rv;

    if (read_link_map(if_index, link) < 0) {
        perror("read_link_map() failed");
        return -1;  // failure
    }

    fd = socket(AF_PACKET, SOCK_RAW, ETH_P_DALE);
    if (fd < 0) {
        perror("socket() failed");
        return -1;  // failure
    }

    ifr.ifr_addr.sa_family = AF_PACKET;
    ifr.ifr_ifindex = if_index;

    // convert if_index into interface name
    rv = ioctl(fd, SIOCGIFNAME, &ifr);
    if (rv < 0) {
        perror("ioctl(SIOCGIFNAME) failed");
        return -1;  // failure
    }

    // get hardware (mac) address from interface
    rv = ioctl(fd, SIOCGIFHWADDR, &ifr);
    if (rv < 0) {
        perror("ioctl(SIOCGIFHWADDR) failed");
        return -1;  // failure
    }

    // copy src_mac and eth_proto into link_map
    struct ethhdr *eth = (struct ethhdr *)link->frame;
    eth->h_proto = htons(ETH_P_DALE);
    memcpy(eth->h_source, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    if (write_link_map(if_index, link) < 0) {
        perror("write_link_map() failed");
        return -1;  // failure
    }

    DEBUG(fprintf(stderr, "LINK_STATE [%d]\n", if_index));
    DEBUG(dump_link_state(stderr, link));

    rv = close(fd);
    return rv;
}

int
get_link_status(int if_index, int fd, int *status, void *mac_addr)
{
    struct ifreq ifr;
    int rv;

    // convert if_index into interface name
    ifr.ifr_addr.sa_family = AF_PACKET;
    ifr.ifr_ifindex = if_index;
    rv = ioctl(fd, SIOCGIFNAME, &ifr);
    if (rv < 0) return rv;

    // get link status from interface
    struct ethtool_value value = {
        .cmd = ETHTOOL_GLINK,
    };
    ifr.ifr_data = ((char *) &value);
    rv = ioctl(fd, SIOCETHTOOL, &ifr);
    if (rv < 0) return rv;
    *status = value.data;

    // get mac address (optionally)
    if (mac_addr) {
        rv = ioctl(fd, SIOCGIFHWADDR, &ifr);
        if (rv < 0) return rv;
        memcpy(mac_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    }

    return 0;
}

int
send_init_msg(int if_index)
{
    static __u8 proto_init[64] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // dst_mac = broadcast
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // src_mac = broadcast
        0xDa, 0x1e,                          // protocol ethertype
        INT2SMOL(0),                         // state = {i:0, u:0}
        INT2SMOL(0),                         // payload len = 0
    };
    int fd, rv;

    // create socket
    fd = socket(AF_PACKET, SOCK_RAW, ETH_P_DALE);
    if (fd < 0) {
        perror("socket() failed");
        return -1;  // failure
    }

    // check link status
    int status = 0;  // 0=down, 1=up
    rv = get_link_status(if_index, fd, &status, &proto_init[ETH_ALEN]);
    if (rv < 0) {

        perror("get_link_status() failed");

    } else if (status == 0) {  // link down

        rv = 1;  // try again later...

    } else {  // link up

        // send message
        struct sockaddr_storage address;
        memset(&address, 0, sizeof(address));

        struct sockaddr_ll *sll = (struct sockaddr_ll *)&address;
        sll->sll_family = AF_PACKET;
        sll->sll_protocol = htons(ETH_P_DALE);
        sll->sll_ifindex = if_index;

        socklen_t addr_len = sizeof(*sll);
        struct sockaddr *addr = (struct sockaddr *)sll;
        rv = sendto(fd, proto_init, ETH_ZLEN, 0, addr, addr_len);
        if (rv < 0) {
            perror("sendto() failed");
        }
        DEBUG(hexdump(stderr, proto_init, sizeof(proto_init)));
        fprintf(stderr, "init sent.\n");

    }

    if (close(fd) < 0) {
        perror("close() failed");
        return -1;  // failure
    }

    return rv;
}

int
monitor(int if_index)  // monitor and maintain liveness
{
    link_state_t link_state;
    link_state_t *link = &link_state;
    __u32 prev_seq;

    // get initial count
    if (read_link_map(if_index, link) < 0) {
        perror("read_link_map() failed");
        return -1;  // failure
    }
    prev_seq = link->seq;

    for (;;) {

        // wait for some activity
        usleep(1000000);  // 1 second = 1,000,000 microseconds

        // check updated value
        if (read_link_map(if_index, link) < 0) {
            perror("read_link_map() failed");
            return -1;  // failure
        }
        if (link->seq == prev_seq) {
            // try to kick-start protocol
            if (send_init_msg(if_index) < 0) {
                return -1;  // failure
            }
        } else {
            prev_seq = link->seq;
        }

    }
}

int
reader(int if_index)  // read AIT data (and display it)
{
    link_state_t link_state;
    link_state_t *link = &link_state;

    for (;;) {

        // get link state
        if (read_link_map(if_index, link) < 0) {
            perror("read_link_map() failed");
            return -1;  // failure
        }

        if (ib_valid(link) && !ib_full(link)) {  // ait available

            // update link state
            ib_set_full(link);
            if (write_link_map(if_index, link) < 0) {
                perror("write_link_map() failed");
                return -1;  // failure
            }
            DEBUG(fprintf(stderr, "inbound FULL set.\n"));

            // display AIT received
            fprintf(stderr, "inbound AIT:\n");
            hexdump(stderr, link->inbound, MAX_PAYLOAD);

        } else if (!ib_valid(link) && ib_full(link)) {

            // clear link state
            ib_clr_full(link);
            if (write_link_map(if_index, link) < 0) {
                perror("write_link_map() failed");
                return -1;  // failure
            }
            DEBUG(fprintf(stderr, "inbound FULL cleared.\n"));

        }

    }

}

int
writer(int if_index)  // write AIT data (from console)
{
    link_state_t link_state;
    link_state_t *link = &link_state;

    for (;;) {

        // get link state
        if (read_link_map(if_index, link) < 0) {
            perror("read_link_map() failed");
            return -1;  // failure
        }

        if (!ob_full(link) && !ob_valid(link)) {  // space available

            // get data from console
            clear_payload(link->outbound);
            char *str = (char *)link->outbound + 2;
            if (!fgets(str, MAX_PAYLOAD - 2, stdin)) {
                return 1;  // EOF (or error)
            }
            int len = strlen(str);
            link->outbound[0] = octets;
            link->outbound[1] = INT2SMOL(len);

            // display AIT to be sent
            fprintf(stderr, "outbound AIT:\n");
            hexdump(stderr, link->outbound, MAX_PAYLOAD);

            // send AIT
            ob_set_valid(link);
            if (write_link_map(if_index, link) < 0) {
                perror("write_link_map() failed");
                return -1;  // failure
            }
            DEBUG(fprintf(stderr, "outbound VALD set.\n"));

        } else if (ob_full(link) && ob_valid(link)) {  // transfer in progress

            // clear link state
            ob_clr_valid(link);
            if (write_link_map(if_index, link) < 0) {
                perror("write_link_map() failed");
                return -1;  // failure
            }
            DEBUG(fprintf(stderr, "outbound VALD cleared.\n"));

        }

    }

}

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

int
main(int argc, char *argv[])
{
    int rv;
    pid_t pid;

    // determine interface index
    int if_index = 0;
    if (argc == 2) {
        if_index = atoi(argv[1]);
        if (!if_index) {
            if_index = if_nametoindex(argv[1]);
        }
    }
    if (!if_index) {
        fprintf(stderr, "usage: %s <interface>\n", argv[0]);
        return 1;  // exit
    }

    // get access to Link map
    rv = bpf_obj_get(link_map_filename);
    if (rv < 0) {
        perror("bpf_obj_get() failed");
        return -1;  // failure
    }
    link_map_fd = rv;

    if (init_link_map(if_index) < 0) return -1;  // failure

    // create process to monitor/maintain liveness
    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        return -1;  // failure
    } else if (pid == 0) {  // child process
        rv = monitor(if_index);
        exit(rv);
    }
    fprintf(stderr, "monitor pid=%d\n", pid);

    // create process to read AIT data
    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        return -1;  // failure
    } else if (pid == 0) {  // child process
        rv = reader(if_index);
        exit(rv);
    }
    fprintf(stderr, "reader pid=%d\n", pid);

    // create process to write AIT data
    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        return -1;  // failure
    } else if (pid == 0) {  // child process
        rv = writer(if_index);
        exit(rv);
    }
    fprintf(stderr, "writer pid=%d\n", pid);

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
