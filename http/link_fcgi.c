/*
 * link_fcgi.c -- link "map" FastCGI server
 */
#include <fcgi_stdio.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <inttypes.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <bpf/bpf.h>

#include "../include/code.h"
#include "../include/link.h"

#ifndef IF_NAME
#define IF_NAME "eth0"
#endif

static char hostname[32];
static char if_name[] = IF_NAME;
static int if_index = -1;
static int if_sock = -1;

static const char *link_map_filename = "/sys/fs/bpf/xdp/globals/link_map";
static int link_map_fd = -1;  // default: map unavailable

int
init_link_map()
{
    int rv;

    rv = bpf_obj_get(link_map_filename);
    if (rv < 0) {
        perror("bpf_obj_get() failed");
        return -1;  // failure
    }
    link_map_fd = rv;
    return 0;  // success
}

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

int
init_host_if()
{
    int rv;

    rv = gethostname(hostname, sizeof(hostname));
    if (rv < 0) return rv;  // failure
    hostname[sizeof(hostname) - 1] = '\0';  // ensure NUL termination

    rv = if_nametoindex(if_name);
    if (rv < 0) return rv;  // failure
    if_index = rv;

    rv = socket(AF_PACKET, SOCK_RAW, ETH_P_DALE);
    if (rv < 0) return rv;  // failure
    if_sock = rv;

    return 0;  // success
}

int
get_link_status()
{
    int rv;

    struct ifreq ifr;
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
#if 0
    rv = ioctl(if_sock, SIOCGIFFLAGS, &if_req);
    if (rv < 0) return rv;  // failure getting link status
    return (ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING);
#else
    struct ethtool_value ethval = {
        .cmd = ETHTOOL_GLINK,
    };
    ifr.ifr_data = ((char *)&ethval);
    rv = ioctl(if_sock, SIOCETHTOOL, &ifr);
    if (rv < 0) return rv;  // failure getting link status
    return !!ethval.data;
#endif
}

static __u8 proto_init[64] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // dst_mac = broadcast
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // src_mac = broadcast
    0xDa, 0x1e,                          // protocol ethertype
    0x80,                                // state = (0,0)
    0x80,                                // payload length = 0
};

int
send_init_msg()
{
    int rv;

    struct sockaddr_storage address;
    memset(&address, 0, sizeof(address));

    struct sockaddr_ll *sll = (struct sockaddr_ll *)&address;
    sll->sll_family = AF_PACKET;
    sll->sll_protocol = htons(ETH_P_DALE);
    sll->sll_ifindex = if_index;

    socklen_t addr_len = sizeof(*sll);
    struct sockaddr *addr = (struct sockaddr *)sll;
    rv = sendto(if_sock, proto_init, ETH_ZLEN, 0, addr, addr_len);
    if (rv < 0) return rv;  // failure sending message

    return 0;  // success
}

int
html_link_state()
{
    int rv = 0;  // success

    if (link_map_fd < 0) return -1;  // failure

    // get link_state structure for this interface
    link_state_t link_state;
    link_state_t *link = &link_state;
    rv = read_link_map(if_index, link);
    if (rv < 0) return rv;  // failure

    printf("<table>\n");
    printf("<tr>"
           "<th>Field</th>"
           "<th>Value</th>"
           "</tr>\n");

    printf("<tr><td>%s</td><td><pre>", "outbound");
    hexdump(stdout, link->outbound, 44);
    printf("</pre></td></tr>\n");

    printf("<tr><td>%s</td><td><tt>", "user_flags");
    __u32 uf = link->user_flags;
    printf("0x%08x (%c%c%c%c)",
        uf,
        '-',
        (GET_FLAG(uf, UF_STOP) ? 'R' : 's'),
        (GET_FLAG(uf, UF_VALD) ? 'V' : '-'),
        (GET_FLAG(uf, UF_FULL) ? 'F' : 'e'));
    printf("</tt></td></tr>\n");

    printf("<tr><td>%s</td><td><pre>", "inbound");
    hexdump(stdout, link->inbound, 44);
    printf("</pre></td></tr>\n");

    printf("<tr><td>%s</td><td><tt>", "link_flags");
    __u32 lf = link->link_flags;
    printf("0x%08x (%c%c%c%c%c%c%c%c)",
        lf,
        '-',
        (GET_FLAG(lf, LF_RECV) ? 'R' : '-'),
        (GET_FLAG(lf, LF_SEND) ? 'S' : '-'),
        (GET_FLAG(lf, LF_VALD) ? 'V' : '-'),
        (GET_FLAG(lf, LF_FULL) ? 'F' : 'e'),
        (GET_FLAG(lf, LF_ENTL) ? '&' : '-'),
        (GET_FLAG(lf, LF_ID_B) ? 'B' : '-'),
        (GET_FLAG(lf, LF_ID_A) ? 'A' : '-'));
    printf("</tt></td></tr>\n");

    printf("<tr><td>%s</td><td><pre>", "frame");
    hexdump(stdout, link->frame, 64);
    printf("</pre></td></tr>\n");

    printf("<tr><td>%s</td><td>", "i,u");
    printf("(%o,%o)", link->i, link->u);
    printf("</td></tr>\n");

    printf("<tr><td>%s</td><td>", "len");
    printf("%u", link->len);
    printf("</td></tr>\n");

    printf("<tr><td>%s</td><td><tt>", "seq");
    printf("0x%08x", link->seq);
    printf("</tt></td></tr>\n");

    printf("</table>\n");

    return rv;
}

int
json_unescaped(int c)
{
    // Per RFC 8259, non-ASCII characters >= 0x7F need not be escaped,
    // however "Any character may be escaped.", so we choose to.
    return (c >= 0x20) && (c < 0x7F)  // printable ASCII
        && (c != '"') && (c != '\\');  // exceptions
}

int
json_string(char *s, int n)
{
    int rv = 0;

    rv += printf("\"");
    for (int i = 0; i < n; ++i) {
        int c = s[i];
        if (json_unescaped(c)) {
            rv += printf("%c", c);
        } else if (c == '\t') {
            rv += printf("\\t");
        } else if (c == '\r') {
            rv += printf("\\r");
        } else if (c == '\n') {
            rv += printf("\\n");
        } else {
            rv += printf("\\u%04X", c);
        }
    }
    rv += printf("\"");

    return rv;
}

static __u32 pkt_count = -1;  // packet counter value

int
json_link_state(link_state_t *link)
{
    __u8 *bp;

    printf("{");
    printf("\n");

    printf("  \"outbound\":");
    json_string((char *)link->outbound, MAX_PAYLOAD);
    printf(",");
    printf("\n");
 
    printf("  \"user_flags\":");
    __u32 uf = link->user_flags;
    printf("{");
    printf("\"STOP\":%s", (GET_FLAG(uf, UF_STOP) ? "true" : "false"));
    printf(",");
    printf("\"VALD\":%s", (GET_FLAG(uf, UF_VALD) ? "true" : "false"));
    printf(",");
    printf("\"FULL\":%s", (GET_FLAG(uf, UF_FULL) ? "true" : "false"));
    printf("}");
    printf(",");
    printf("\n");

    printf("  \"inbound\":");
    json_string((char *)link->inbound, MAX_PAYLOAD);
    printf(",");
    printf("\n");

    printf("  \"link_flags\":");
    __u32 lf = link->link_flags;
    printf("{");
    printf("\"RECV\":%s", (GET_FLAG(lf, LF_RECV) ? "true" : "false"));
    printf(",");
    printf("\"SEND\":%s", (GET_FLAG(lf, LF_SEND) ? "true" : "false"));
    printf(",");
    printf("\"VALD\":%s", (GET_FLAG(lf, LF_VALD) ? "true" : "false"));
    printf(",");
    printf("\"FULL\":%s", (GET_FLAG(lf, LF_FULL) ? "true" : "false"));
    printf(",");
    printf("\"ENTL\":%s", (GET_FLAG(lf, LF_ENTL) ? "true" : "false"));
    printf(",");
    printf("\"ID_B\":%s", (GET_FLAG(lf, LF_ID_B) ? "true" : "false"));
    printf(",");
    printf("\"ID_A\":%s", (GET_FLAG(lf, LF_ID_A) ? "true" : "false"));
    printf("}");
    printf(",");
    printf("\n");

    printf("  \"frame\":");
    bp = link->frame;
    printf("[");
    printf("%u,%u,%u,%u,%u,%u,%u,%u",
        bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7]);
    bp += 8;
    printf(",");
    printf("%u,%u,%u,%u,%u,%u,%u,%u",
        bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7]);
    bp += 8;
    printf(",");
    printf("%u,%u,%u,%u,%u,%u,%u,%u",
        bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7]);
    bp += 8;
    printf(",");
    printf("%u,%u,%u,%u,%u,%u,%u,%u",
        bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7]);
    printf("]");
    printf(",");
    printf("\n");
 
    printf("  \"i\":%u,\n", link->i);
    printf("  \"u\":%u,\n", link->u);
    printf("  \"len\":%u,\n", link->len);

    printf("  \"seq\":%u\n", link->seq);
    printf("}");
//    printf("\n");

    return 0;  // success
}

static const char hex[] = "0123456789ABCDEF";

int
hex_to_octets(char *dbuf, int dlen, char *sbuf, int slen)
{
    int i = 0, j = 0;
    char *s;
    int c;

    while (i+1 < slen) {
        if (j >= dlen) {
            return -1;  // fail: dbuf overflow
        }

        s = strchr(hex, toupper(sbuf[i++]));
        if (!(s && *s)) {
            return -1;  // fail: non-hex digit
        }
        c = (s - hex);

        s = strchr(hex, toupper(sbuf[i++]));
        if (!(s && *s)) {
            return -1;  // fail: non-hex digit
        }
        c = (c << 4) | (s - hex);

        dbuf[j++] = c;
    }
    return j;  // success: # of characters written to dbuf
}

int
uri_unreserved(int c)
{
    // Per RFC 3986, '~' is "unreserved" in query strings,
    // but not in "application/x-www-form-urlencoded" content.
    return ((c >= '0') && (c <= '9'))
        || ((c >= 'a') && (c <= 'z'))
        || ((c >= 'A') && (c <= 'Z'))
        || (c == '-') || (c == '_') || (c == '.');
}

int
uri_to_utf8(char *dbuf, int dlen, char *sbuf, int slen)
{
    int i = 0, j = 0;
    char *s;

    while (i < slen) {
        if (j >= dlen) {
            return -1;  // fail: dbuf overflow
        }
        int c = sbuf[i++];
        if (c == '%') {
            if (i + 2 > slen) {
                return -1;  // fail: sbuf underflow
            }

            s = strchr(hex, toupper(sbuf[i++]));
            if (!(s && *s)) {
                return -1;  // fail: non-hex digit
            }
            c = (s - hex);

            s = strchr(hex, toupper(sbuf[i++]));
            if (!(s && *s)) {
                return -1;  // fail: non-hex digit
            }
            c = (c << 4) | (s - hex);
        } else if (c == '+') {
            c = ' ';  // backward compatibility
        }
        dbuf[j++] = c;
    }
    return j;  // success: # of characters written to dbuf
}

int
utf8_to_uri(char *dbuf, int dlen, char *sbuf, int slen)
{
    int i, j = 0;

    for (i = 0; i < slen; ++i) {
        int c = sbuf[i];
        if (uri_unreserved(c)) {
            if (j >= dlen) {
                return -1;  // fail: dbuf overflow
            }
            dbuf[j++] = c;
        } else {
            if (j + 2 >= dlen) {
                return -1;  // fail: dbuf overflow
            }
            dbuf[j++] = '%';
            dbuf[j++] = hex[(c >> 4) & 0xF];
            dbuf[j++] = hex[c & 0xF];
        }
    }
    return j;  // success: # of characters written to dbuf
}

int
get_uri_param(char *buf, int len, char **query_string, char *key)
{
    // NOTE: key must consist of only "unreserved" characters!
    while (query_string && *query_string) {
        char *p = *query_string;
        char *q = key;

        // try to match key
        while (*p && (*p != '=')) {
            q = (q && (*q == *p)) ? q + 1 : NULL;
            if (!uri_unreserved(*p++)) {
                return -1;  // reserved character in query key
            }
        }
        // iff key matched, *q == '\0'.

        if (!*p++) return -1;  // no value!
        char *r = p;
        // parse value
        while (*p && (*p != '&') && (*p != ';')) {
            ++p;
        }

        // update query_string
        *query_string = (*p ? p + 1 : NULL);

        // if key matched, return translated value
        if (q && (*q == '\0')) {
            return uri_to_utf8(buf, len, r, (p - r));  // translate!
        }
    }
    return -1;  // key not found
}

int
html_query(char *query_string)
{
    static char *name[] = {
        "fmt",
        "id",
        NULL
    };
    int rv = 0;  // success
    char value[256];

    printf("<table>\n");
    printf("<tr><th>Name</th><th>Value</th></tr>\n");
    for (int i = 0; name[i]; ++i) {
        char *key = name[i];

        printf("<tr>");
        printf("<td>%s</td>", key);

        char *q = query_string;
        int n = get_uri_param(value, sizeof(value), &q, key);
        if (n < 0) {
            printf("<td><i>null</i></td>");
        } else {
            // FIXME: should sanitize value for HTML output
            printf("<td>\"%.*s\"</td>", n, value);
        }

        printf("</tr>\n");

    }
    printf("</table>\n");

    return rv;
}

int
json_query(link_state_t *link, char *query_string)
{
    char value[256];
    char *q;
    int n;

    // echo query string
    printf("\"query\":");
    json_string(query_string, strlen(query_string));
    printf(",");

    // check for inbound FULL flag
    q = query_string;
    n = get_uri_param(value, sizeof(value) - 1, &q, "FULL");
    if ((n == 4) && (strncmp(value, "true", 4) == 0)) {
        SET_FLAG(link->user_flags, UF_FULL);
    }
    if ((n == 5) && (strncmp(value, "false", 5) == 0)) {
        CLR_FLAG(link->user_flags, UF_FULL);
    }

    // check for outbound VALD flag
    q = query_string;
    n = get_uri_param(value, sizeof(value) - 1, &q, "VALD");
    if ((n == 4) && (strncmp(value, "true", 4) == 0)) {
        memset((char *)link->outbound, null, MAX_PAYLOAD);
        q = query_string;
        n = get_uri_param(value, sizeof(value) - 1, &q, "DATA");
        if (n < 0) return -1;  // failure!
        n = hex_to_octets((char *)link->outbound, MAX_PAYLOAD, value, n);
        if (n < 0) return -1;  // failure!
        SET_FLAG(link->user_flags, UF_VALD);
    }
    if ((n == 5) && (strncmp(value, "false", 5) == 0)) {
        CLR_FLAG(link->user_flags, UF_VALD);
        memset((char *)link->outbound, null, MAX_PAYLOAD);
    }

    return 0;  // success
}

int
html_params()
{
    static char *name[] = {
        "REQUEST_SCHEME",
        "REQUEST_URI",
        "REQUEST_METHOD",
        "CONTENT_TYPE",
        "CONTENT_LENGTH",
        "PATH_INFO",
        "QUERY_STRING",
        "SERVER_NAME",
        "SCRIPT_FILENAME",
        "HTTP_ACCEPT",
        "HTTP_ACCEPT_CHARSET",
        "HTTP_ACCEPT_ENCODING",
        "HTTP_ACCEPT_LANGUAGE",
        "HTTP_CONNECTION",
        "HTTP_USER_AGENT",
        "HTTP_HOST",
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
}

void
html_content(int req_num)
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
    printf("<h1>Link #%d Map</h1>\n", if_index);

    printf("<p>Request #%d</p>\n", req_num);

    printf("<h2>Link State</h2>\n");
    if (html_link_state() < 0) {
        printf("<i>Map Unavailable</i>\n");
    }

    printf("<h2>Query Params</h2>\n");
    if (html_query(getenv("QUERY_STRING")) < 0) {
        printf("<i>Params Unavailable</i>\n");
    }

    printf("<h2>FastCGI Params</h2>\n");
    if (html_params() < 0) {
        printf("<i>Params Unavailable</i>\n");
    }

    printf("</body>\n");
    printf("</html>\n");
}

int
json_info(int old, int new)
{
    int rv;
    char *status;

    printf(",");
    printf("\"old\":%d", old);

    printf(",");
    printf("\"new\":%d", new);

    if (old != new) {  // packet count advancing
        status = "UP";
    } else {
        rv = get_link_status();
        if (rv < 0) {  // failure getting link status
            status = "ERROR";
        } else if (rv == 0) {  // link is down
            status = "DOWN";
        } else {
            // link is up, try to kick-start it...
            rv = send_init_msg();
            if (rv < 0) {  // failure sending init message
                status = "DEAD";
            } else {
                status = "INIT";
            }
        }
    }
    printf(",");
    printf("\"link\":");
    json_string(status, strlen(status));

    return 0;  // success
}

void
json_content(int req_num)
{
    printf("{");

    // write hostname
    printf("\"host\":");
    json_string(hostname, strlen(hostname));
    printf(",");

    // write request number
    printf("\"req_num\":%d", req_num);
    printf(",");

    int old = (__s32)pkt_count;  // save old packet count

    // get link_state structure for this interface
    link_state_t link_state;
    link_state_t *link = &link_state;
    if ((link_map_fd < 0)
    ||  (read_link_map(if_index, link) < 0)) {
        printf(",");
        printf("\"error\":\"%s\"", "Failed Reading Link Map");
    } else {

        json_query(link, getenv("QUERY_STRING"));

        printf("\"link_state\":");
        json_link_state(link);
        pkt_count = link->seq;  // update packet count

    }

    // update link_state structure
    if (write_link_map(if_index, link) < 0) {
        printf(",");
        printf("\"error\":\"%s\"", "Failed Writing Link Map");
    } else {
        int new = (__s32)pkt_count;  // get new packet count
        json_info(old, new);
    }

    printf("}\n");
}

int
init_src_mac()
{
    int rv;

    // get hardware (mac) address from interface
    struct ifreq ifr;
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
    rv = ioctl(if_sock, SIOCGIFHWADDR, &ifr);
    if (rv < 0) return rv;  // failure

    // copy src_mac into init message
    memcpy(proto_init + ETH_ALEN, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

    // get link_state structure for this interface
    link_state_t link_state;
    link_state_t *link = &link_state;
    rv = read_link_map(if_index, link);
    if (rv < 0) return rv;  // failure

    // copy src_mac and eth_proto into link_map
    struct ethhdr *eth = (struct ethhdr *)link->frame;
    eth->h_proto = htons(ETH_P_DALE);
    memcpy(eth->h_source, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    rv = write_link_map(if_index, link);
    if (rv < 0) return rv;  // failure

    return 0;  // success
}

int
main(void)
{
    char buf[256];
    int count = 0;

    init_link_map();
    init_host_if();
    init_src_mac();

    while(FCGI_Accept() >= 0) {
        ++count;
        char *path_info = getenv("PATH_INFO");
        if (path_info) {
            int n = uri_to_utf8(buf, sizeof(buf), path_info, strlen(path_info));

            if ((n == 19) && (strncmp(buf, "/link_map/link.html", n) == 0)) {
                http_header("text/html");
                html_content(count);
                continue;  // next request...
            }

            if ((n == 19) && (strncmp(buf, "/link_map/link.json", n) == 0)) {
                http_header("application/json");
                json_content(count);
                continue;  // next request...
            }

        }

        http_header("text/plain");
        printf("Bad Request.\r\n");
    }

    return 0;
}
