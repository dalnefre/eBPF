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
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_packet.h>

#define ETH_P_DALE (0xda1e)

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
        return -1;  // failure
    }

    if (close(fd) < 0) {
        perror("close() failed");
        return -1;  // failure
    }

    return 0;  // success
}

int
main(int argc, char *argv[])
{
    int rv;
    __u64 value;

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

    rv = bpf_obj_get(ait_map_filename);
    if (rv < 0) {
        perror("bpf_obj_get() failed");
        return -1;  // failure
    }
    ait_map_fd = rv;

    if (dump_ait_map() < 0) return -1;  // failure

    // wait for link liveness to be established
    __u64 count;
    if (read_ait_map(3, &count) < 0) {
        perror("read_ait_map() failed");
        return -1;  // failure
    }
    for (;;) {
        usleep(1000000);  // 1 seconds = 1,000,000 microseconds
        if (read_ait_map(3, &value) < 0) {
            perror("read_ait_map() failed");
            return -1;  // failure
        }
        if (count != value) {
            printf("Ready... (%lld)\n", value);
            break;  // exit loop
        }
        if (send_init_msg(if_index) < 0) {
            return -1;  // failure
        }
    }

    // ait read/write loop
    for (;;) {
        if (read_ait_map(1, &value) < 0) {
            perror("read_ait_map() failed");
            return -1;  // failure
        }
        if (value == -1) {  // no ait present
            fflush(stdout);
            if (read_ait_map(0, &value) < 0) {
                perror("read_ait_map() failed");
                return -1;  // failure
            }
            if (value != -1) continue;  // no space for outbound ait
//            usleep(100000);  // 1/10th second = 100,000 microseconds
            if (!fgets((char *)&value, sizeof(value), stdin)) {
                return 1;  // EOF (or error)
            }
            if (write_ait_map(0, value) < 0) {
                perror("write_ait_map() failed");
                return -1;  // failure
            }
        } else {
            if (write_ait_map(1, -1) < 0) {
                perror("write_ait_map() failed");
                return -1;  // failure
            }
            ait_rcvd(value);
        }
    }

    return 0;  // success
}
