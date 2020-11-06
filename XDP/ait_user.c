/*
 * ait_user.c -- XDP userspace control program
 *
 * Implement atomic information transfer protocol in XDP
 */
#include <stdio.h>
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
            return -1;
        }

        __u8 *bp = (__u8 *)&value;
        printf("ait_map[%d] = %02x %02x %02x %02x %02x %02x %02x %02x (%lld)\n",
            key,
            bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7],
            value);

    }

    return 0;
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

int
main(int argc, char *argv[])
{
    int rv;
    __u64 value;

    rv = bpf_obj_get(ait_map_filename);
    if (rv < 0) {
        perror("bpf_obj_get() failed");
        return -1;  // failure
    }
    ait_map_fd = rv;

    if (dump_ait_map() < 0) return -1;  // failure

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
//            usleep(100000);  // 1/10th second = 100,000 microsecond
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
