/*
 * ebpf_fcgi.c -- eBPF "map" FastCGI server
 */
#if TEST_MAIN
#include <stdio.h>
#else
#include <fcgi_stdio.h>
#endif
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
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

int
json_ait_map()
{
    int rv = 0;  // success
    __u32 key;
    __u64 value;

    if (ait_map_fd < 0) return -1;  // failure

    printf("[");
    for (key = 0; key < 4; ++key) {

        if (read_ait_map(key, &value) < 0) {
            perror("read_ait_map() failed");
            rv = -1;  // failure
            break;
        }
        __u8 *bp = (__u8 *)&value;

        if (key > 0) {
            printf(",");
        }
        printf("\n");
        printf("{");

        printf("\"n\":");
        //printf(PRId64, value);
        printf("%lld", value);
        printf(",");

        printf("\"s\":");
        json_string((char *)&value, sizeof(value));
        printf(",");

        printf("\"b\":");
        printf("[%d,%d,%d,%d,%d,%d,%d,%d]",
            bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7]);
        printf("}");

    }
    printf("\n");
    printf("]");

    // clear inbound AIT, if any
    if (write_ait_map(1, -1) < 0) {
        rv = -1;
    }

    return rv;
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

static const char hex[] = "0123456789ABCDEF";

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
};

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
};

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
        "ait",
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
json_query(char *query_string)
{
    int rv = 0;  // success
    __u64 ait;
    char value[256];

    // check for outbound AIT in query string
    char *q = query_string;
    int n = get_uri_param(value, sizeof(value) - 1, &q, "ait");
    if (n < 0) {
        return 0;  // success (no outbound AIT)
    }

    // check for space in outbound AIT register
    if (read_ait_map(0, &ait) < 0) return -1;  // failure
    if (ait != -1) {
        return 0;  // success (outbound not empty)
    }

    // we have outbound AIT to write
    if (n > 8) {
        n = 8;  // truncate to 64 bits
    }
    value[n] = '\0';  // add NUL termination
    ait = 0;
    memcpy(&ait, value, n);

    // write outbound AIT
    rv = write_ait_map(0, ait);
    if (rv >= 0) {
        printf("\"sent\":");
        json_string(value, n);
        printf(",");
    }

    return rv;
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
};

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
    printf("<h1>eBPF Map</h1>\n");

    printf("<p>Request #%d</p>\n", req_num);

    printf("<h2>AIT Map Dump</h2>\n");
    if (html_ait_map() < 0) {
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
};

void
json_content(int req_num)
{
    printf("{");

    printf("\"req_num\":%d", req_num);
    printf(",");

    json_query(getenv("QUERY_STRING"));

    printf("\"ait_map\":");
    if (json_ait_map() < 0) {
        printf(",");
        printf("\"error\":\"%s\"", "Map Unavailable");
    }

    printf("}\n");
};

#ifndef TEST_MAIN
int
main(void)
{
    char buf[256];
    int count = 0;

    init_ait_map();

    while(FCGI_Accept() >= 0) {
        ++count;
        char *path_info = getenv("PATH_INFO");
        if (path_info) {
            int n = uri_to_utf8(buf, sizeof(buf), path_info, strlen(path_info));

            if ((n == 18) && (strncmp(buf, "/ebpf_map/ait.html", n) == 0)) {
                http_header("text/html");
                html_content(count);
                continue;  // next request...
            }

            if ((n == 18) && (strncmp(buf, "/ebpf_map/ait.json", n) == 0)) {
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
#else /* TEST_MAIN */
#include <assert.h>

int
main(void)
{
    char buf[64];
    char *s;
    int n;
    char *expect = "~Bad = clear (already)?";

    s = "%7eBad+%3d+clear+%28already%29%3F";
    n = uri_to_utf8(buf, sizeof(buf), s, strlen(s));
    printf("n = %d\n", n);
    assert(n == strlen(expect));
    printf("uri_to_utf8: \"%.*s\"\n", n, buf);
    assert(strncmp(buf, expect, n) == 0);

    s = expect;
    n = utf8_to_uri(buf, sizeof(buf), s, strlen(s));
    printf("n = %d\n", n);
    assert(n > strlen(s));
    printf("utf8_to_uri: \"%.*s\"\n", n, buf);
    n = uri_to_utf8(buf, sizeof(buf), buf, n);
    printf("n = %d\n", n);
    assert(n == strlen(expect));
    assert(strncmp(buf, expect, n) == 0);

    n = html_query("fmt=json&ait=Hello%2C+World!");
    assert(n >= 0);

    return 0;
}
#endif /* TEST_MAIN */
