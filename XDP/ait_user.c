/*
 * ait_user.c -- XDP userspace control program
 *
 * Implement atomic information transfer protocol in XDP
 */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <bpf/bpf.h>

static const char *ait_map_filename = "/sys/fs/bpf/xdp/globals/ait_map";
static int ait_map_fd;

int
read_ait_map(__u32 key, __u64 *value_ptr)
{
    return bpf_map_lookup_elem(ait_map_fd, &key, value_ptr);
}

int
write_ait_map(__u32 key, __u64 value)
{
    return bpf_map_update_elem(ait_map_fd, &key, &value, BPF_ANY);
}

int
dump_ait_map()
{
    __u32 key;
    __u64 value;

    for (key = 0; key < 4; ++key) {

        if (read_ait_map(key, &value) < 0) {
            perror("read_ait_map() failed");
            return -1;  // failure
        }

        __u8 *bp = (__u8 *)&value;
        printf("ait_map[%d] = %02x %02x %02x %02x %02x %02x %02x %02x (%lld)\n",
            key,
            bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7],
            value);

    }

    return 0;  // success
}

void
ait_rcvd(__u64 ait)
{
#if 1
    printf("%.8s", (char *)&ait);
#else
    __u8 *bp = (__u8 *)&ait;
    printf("%02x %02x %02x %02x %02x %02x %02x %02x \"%.8s\"\n",
        bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7],
        (char *)&ait);
#endif
}

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

#define ETH_P_DALE (0xda1e)

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
    static __u8 proto_init[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // dst_mac = broadcast
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // src_mac = broadcast
        0xda, 0x1e,                          // protocol ethertype
        0x04, 0x86,                          // array (size=6)
        0x80,                                // state = 0
        0x80,                                // other = 0
        0x10, 0x82, 0x00, 0x00,              // count = 0 (+INT, pad=0)
        0xff, 0xff,                          // neutral fill...
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
        rv = sendto(fd, proto_init, sizeof(proto_init), 0, addr, addr_len);
        if (rv < 0) {
            perror("sendto() failed");
        }

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
    __u64 count;
    __u64 value;

    // get initial count
    if (read_ait_map(3, &count) < 0) {
        perror("read_ait_map(3) failed");
        return -1;  // failure
    }

    for (;;) {

        // wait for some activity
        usleep(1000000);  // 1 second = 1,000,000 microseconds

        // check updated value
        if (read_ait_map(3, &value) < 0) {
            perror("read_ait_map(3) failed");
            return -1;  // failure
        }
        if (count == value) {
            // try to kick-start protocol
            if (send_init_msg(if_index) < 0) {
                return -1;  // failure
            }
        } else {
            count = value;
        }

    }
}

int
reader()  // read AIT data (and display it)
{
    __u64 value;

    for (;;) {

        // check for inbound AIT
        if (read_ait_map(1, &value) < 0) {
            perror("read_ait_map(1) failed");
            return -1;  // failure
        }

        if (value != -1) {  // ait present

            // clear inbound AIT
            if (write_ait_map(1, -1) < 0) {
                perror("write_ait_map(1) failed");
                return -1;  // failure
            }

            // display AIT received
#if 1
            printf("%.8s", (char *)&value);
#else
            __u8 *bp = (__u8 *)&value;
            printf("%02x %02x %02x %02x %02x %02x %02x %02x \"%.8s\"\n",
                bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7],
                (char *)&value);
#endif

        }
    }

}

int
writer()  // write AIT data (from console)
{
    __u64 value;

    for (;;) {

        // check for outbound AIT space
        if (read_ait_map(0, &value) < 0) {
            perror("read_ait_map(0) failed");
            return -1;  // failure
        }

        if (value == -1) {  // space available

            // get data from console
            if (!fgets((char *)&value, sizeof(value), stdin)) {
                return 1;  // EOF (or error)
            }

            // send AIT
            if (write_ait_map(0, value) < 0) {
                perror("write_ait_map(0) failed");
                return -1;  // failure
            }

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

    // get access to AIT map
    rv = bpf_obj_get(ait_map_filename);
    if (rv < 0) {
        perror("bpf_obj_get() failed");
        return -1;  // failure
    }
    ait_map_fd = rv;

    if (dump_ait_map() < 0) return -1;  // failure

    // create process to monitor/maintain liveness
    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        return -1;  // failure
    } else if (pid == 0) {  // child process
        rv = monitor(if_index);
        exit(rv);
    }
    printf("monitor pid=%d\n", pid);

    // create process to read AIT data
    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        return -1;  // failure
    } else if (pid == 0) {  // child process
        rv = reader();
        exit(rv);
    }
    printf("reader pid=%d\n", pid);

    // create process to write AIT data
    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        return -1;  // failure
    } else if (pid == 0) {  // child process
        rv = writer();
        exit(rv);
    }
    printf("writer pid=%d\n", pid);

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
    fputs("parent exit.\n", stdout);

    return 0;  // success
}
