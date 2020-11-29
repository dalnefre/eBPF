/*
 * ebpf_fcgi.c -- eBPF "map" FastCGI server
 */
#include <fcgi_stdio.h>
#include <stdlib.h>

#include <stddef.h>
#include <stdlib.h>
//#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <bpf/bpf.h>

static const char *ait_map_filename = "/sys/fs/bpf/xdp/globals/ait_map";
static int ait_map_fd = -1;  // default: map unavailable

int
init_ait_map()
{
    int rv;

    rv = bpf_obj_get(ait_map_filename);
    if (rv < 0) {
        perror("bpf_obj_get() failed");
        return -1;  // failure
    }
    ait_map_fd = rv;
    return 0;  // success
}

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

    if (ait_map_fd < 0) return -1;  // failure

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
http_header(char *content_type)
{
    if (content_type) {
        printf("Content-type: %s\r\n", content_type);
    }
    // HTTP header ends with a blank line
    printf("\r\n");
};

void
html_body(int req_count)
{
    printf("<!DOCTYPE html>\n");
    printf("<html>\n");
    printf("<head>\n");
    printf("<title>eBPF Map</title>\n");
    printf("</head>\n");
    printf("<body>\n");
    printf("<h1>eBPF Map</h1>\n");
    printf("<p>Request #%d</p>\n", req_count);
    printf("<h2>AIT Map Dump</h2>\n");
    printf("<pre>\n");
    if (dump_ait_map() < 0) {
        printf("<i>Map Unavailable</i>\n");
    }
    printf("</pre>\n");
    printf("</body>\n");
    printf("</html>\n");
};

int
main(void)
{
    int count = 0;

    init_ait_map();

    while(FCGI_Accept() >= 0) {
        http_header("text/html");
        html_body(++count);
    }

    return 0;
}
