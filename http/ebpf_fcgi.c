/*
 * ebpf_fcgi.c -- eBPF "map" FastCGI server
 */
#include <fcgi_stdio.h>
#include <stddef.h>
#include <stdlib.h>
//#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
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

static char *
ait_map_label(int key)
{
    switch (key) {
        case 0: return "outbound";
        case 1: return "inbound";
        case 2: return "reserved";
        case 3: return "counter";
        default: return "???";
    }
}

int
html_ait_map()
{
    int rv = 0;  // success
    __u32 key;
    __u64 value;

    if (ait_map_fd < 0) return -1;  // failure

    printf("<table>\n");
    printf("<tr>"
           "<th>#</th>"
           "<th>Label</th>"
           "<th>Value</th>"
           "<th>Octets</th>"
           "</tr>\n");
    for (key = 0; key < 4; ++key) {

        if (read_ait_map(key, &value) < 0) {
            perror("read_ait_map() failed");
            rv = -1;  // failure
            break;
        }

        __u8 *bp = (__u8 *)&value;
        printf("<tr>");
        //printf("<td>" PRId32 "</td>", key);
        printf("<td>%d</td>", (int)key);
        printf("<td>%s</td>", ait_map_label(key));
        //printf("<td>" PRId64 "</td>", value);
        printf("<td>%lld</td>", value);
        printf("<td><tt>%02x %02x %02x %02x %02x %02x %02x %02x</tt></td>",
            bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7]);
        printf("</tr>\n");

    }
    printf("</table>\n");

    return rv;
}

char *
fcgi_param(char *name)
{
    char *value = getenv(name);
    if (!value) {
        return "*NULL*";
    }
    return value;
};

int
html_params()
{
    static char *name[] = {
        "REQUEST_URI",
        "REQUEST_METHOD",
        "CONTENT_TYPE",
        "CONTENT_LENGTH",
        "DOCUMENT_URI",
        "SCRIPT_NAME",
        "SCRIPT_FILENAME",
        "QUERY_STRING",
        "QUERY_PARAMS",
        "SERVER_NAME",
        NULL
    };
    int rv = 0;  // success

    printf("<table>\n");
    printf("<tr><th>Name</th><th>Value</th></tr>\n");
    for (int i = 0; name[i]; ++i) {
        char *key = name[i];
        char *value = getenv(key);

        printf("<tr>");
        printf("<td>%s</td>", key);
        if (value) {
            printf("<td><tt>%s</tt></td>", value);
        } else {
            printf("<td><i>null</i></td>");
        }
        printf("</tr>\n");

    }
    printf("</table>\n");

    return rv;
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
html_content(int req_count)
{
    printf("<!DOCTYPE html>\n");
    printf("<html>\n");

    printf("<head>\n");
    printf("<title>eBPF Map</title>\n");
    printf("<link "
           "rel=\"stylesheet\" "
           "type=\"text/css\" "
           "href=\"/style.css\" "
           "/>\n");
    printf("</head>\n");

    printf("<body>\n");
    printf("<h1>eBPF Map</h1>\n");

    printf("<p>Request #%d</p>\n", req_count);

    printf("<h2>AIT Map Dump</h2>\n");
    if (html_ait_map() < 0) {
        printf("<i>Map Unavailable</i>\n");
    }

    printf("<h2>FastCGI Params</h2>\n");
    if (html_params() < 0) {
        printf("<i>Params Unavailable</i>\n");
    }

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
        html_content(++count);
    }

    return 0;
}
