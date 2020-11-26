/*
 * ebpf_fcgi.c -- eBPF "map" FastCGI server
 */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>  // for getpwnam()
#include <sys/socket.h>
#include <sys/un.h>
#include "fcgi.h"

#define DEBUG(x) x /**/

#define SOCK_PATHNAME        "/run/ebpf_map.sock"
#define WEB_SERVER_USERNAME  "www-data"

//static char proto_buf[512];  // message-transfer buffer
static char proto_buf[4096];  // message-transfer buffer

static void
hexdump(FILE *f, void *data, size_t size)
{
    unsigned char *buffer = data;
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
                unsigned char b = buffer[j];
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

#if 0
read() got 640 octets:
0000:  01 01 00 01 00 08 00 00  00 01 00 00 00 00 00 00  |................|
0010:  01 04 00 01 02 55 03 00  0b 09 52 45 51 55 45 53  |.....U....REQUES|
0020:  54 5f 55 52 49 2f 65 62  70 66 5f 6d 61 70 0e 03  |T_URI/ebpf_map..|
0030:  52 45 51 55 45 53 54 5f  4d 45 54 48 4f 44 47 45  |REQUEST_METHODGE|
0040:  54 0c 00 43 4f 4e 54 45  4e 54 5f 54 59 50 45 0e  |T..CONTENT_TYPE.|
0050:  00 43 4f 4e 54 45 4e 54  5f 4c 45 4e 47 54 48 0f  |.CONTENT_LENGTH.|
0060:  16 53 43 52 49 50 54 5f  46 49 4c 45 4e 41 4d 45  |.SCRIPT_FILENAME|
0070:  2f 76 61 72 2f 77 77 77  2f 68 74 6d 6c 2f 65 62  |/var/www/html/eb|
0080:  70 66 5f 6d 61 70 0c 00  51 55 45 52 59 5f 53 54  |pf_map..QUERY_ST|
0090:  52 49 4e 47 09 0d 48 54  54 50 5f 48 4f 53 54 31  |RING..HTTP_HOST1|
00a0:  39 32 2e 31 36 38 2e 31  2e 31 31 36 0f 0a 48 54  |92.168.1.116..HT|
00b0:  54 50 5f 43 4f 4e 4e 45  43 54 49 4f 4e 6b 65 65  |TP_CONNECTIONkee|
00c0:  70 2d 61 6c 69 76 65 12  09 48 54 54 50 5f 43 41  |p-alive..HTTP_CA|
...
0190:  80 00 00 87 48 54 54 50  5f 41 43 43 45 50 54 74  |....HTTP_ACCEPTt|
01a0:  65 78 74 2f 68 74 6d 6c  2c 61 70 70 6c 69 63 61  |ext/html,applica|
01b0:  74 69 6f 6e 2f 78 68 74  6d 6c 2b 78 6d 6c 2c 61  |tion/xhtml+xml,a|
01c0:  70 70 6c 69 63 61 74 69  6f 6e 2f 78 6d 6c 3b 71  |pplication/xml;q|
01d0:  3d 30 2e 39 2c 69 6d 61  67 65 2f 61 76 69 66 2c  |=0.9,image/avif,|
01e0:  69 6d 61 67 65 2f 77 65  62 70 2c 69 6d 61 67 65  |image/webp,image|
01f0:  2f 61 70 6e 67 2c 2a 2f  2a 3b 71 3d 30 2e 38 2c  |/apng,*/*;q=0.8,| */
0200:  61 70 70 6c 69 63 61 74  69 6f 6e 2f 73 69 67 6e  |application/sign|
0210:  65 64 2d 65 78 63 68 61  6e 67 65 3b 76 3d 62 33  |ed-exchange;v=b3|
0220:  3b 71 3d 30 2e 39 14 0d  48 54 54 50 5f 41 43 43  |;q=0.9..HTTP_ACC|
0230:  45 50 54 5f 45 4e 43 4f  44 49 4e 47 67 7a 69 70  |EPT_ENCODINGgzip|
0240:  2c 20 64 65 66 6c 61 74  65 14 0e 48 54 54 50 5f  |, deflate..HTTP_|
0250:  41 43 43 45 50 54 5f 4c  41 4e 47 55 41 47 45 65  |ACCEPT_LANGUAGEe|
0260:  6e 2d 55 53 2c 65 6e 3b  71 3d 30 2e 39 00 00 00  |n-US,en;q=0.9...|
0270:  01 04 00 01 00 00 00 00  01 05 00 01 00 00 00 00  |................|
0280:  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
0290:  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
02a0:  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
...
0fe0:  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
0ff0:  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
#endif

typedef struct {
    char *name;
    char *value;
} name_value_t;

name_value_t config[] = {
    {
        .name = "FCGI_MAX_CONNS",
        .value = "1",
    },
    {
        .name = "FCGI_MAX_REQS",
        .value = "1",
    },
    {
        .name = "FCGI_MPXS_CONNS",
        .value = "0",
    },
};

int
get_value(FCGI_Header *req, int offset, FCGI_Header *rsp)
{
    int req_content_len = req->contentLengthB1 << 8 | req->contentLengthB0;
    //int req_padding_len = req->paddingLength;
    if (offset > req_content_len) return 0;  // end of content

    unsigned char *req_content = (void *)(req + 1);  // content follows header
    int name_len = req_content[offset++];
    if (name_len > 0x7F) return -1;  // name_len too large
    int value_len = req_content[offset++];
    if (value_len != 0) return -1;  // value_len must be zero for query
    char *req_name = (char *)req_content + offset;
    offset += name_len;
    //offset += value_len; -- value_len must be 0, so no increment needed.

    int rsp_content_len = rsp->contentLengthB1 << 8 | rsp->contentLengthB0;
    int rsp_padding_len = rsp->paddingLength;
    unsigned char *rsp_content = (void *)(rsp + 1);  // content follows header

    for (int i = 0; i < (sizeof(config) / sizeof(name_value_t)); ++i) {
        if ((name_len == strlen(config[i].name))
        &&  (strncmp(req_name, config[i].name, name_len) == 0)) {

            // matched config variable name, copy name/value pair
            value_len = strlen(config[i].value);
            if (2 + value_len > 0x7F) return -1;  // value_len too large
            if (rsp_padding_len < value_len) return -1;  // not enough room
            rsp_content[rsp_content_len++] = name_len;
            rsp_content[rsp_content_len++] = value_len;
            memcpy(rsp_content + rsp_content_len, req_name, name_len);
            rsp_content_len += name_len;
            memcpy(rsp_content + rsp_content_len, config[i].value, value_len);
            rsp_content_len += value_len;
            rsp_padding_len -= 2 + name_len + value_len;

            // update lengths in header (overwrite)
            rsp->contentLengthB0 = rsp_content_len;
            rsp->contentLengthB1 = rsp_content_len >> 8;
            rsp->paddingLength = rsp_padding_len;

            break;  // first-match wins!
        }
    }

    return offset;
}

int
record(void **data, void *end)
{
    int n;
    int rv = 0;  // success
    static int active_request_id = FCGI_NULL_REQUEST_ID;

    void *content = *data + FCGI_HEADER_LEN;
    if (content + FCGI_HEADER_LEN > end) return -1;  // too small
    FCGI_Header *hdr = (FCGI_Header *)*data;

/**
0000:  01 01 00 01 00 08 00 00  00 01 00 00 00 00 00 00  |................|
0010:  01 04 00 01 02 55 03 00  0b 09 52 45 51 55 45 53  |.....U....REQUES|
**/
    if (hdr->version != FCGI_VERSION_1) return -1;  // bad version

    int request_id = hdr->requestIdB1 << 8 | hdr->requestIdB0;
    int content_len = hdr->contentLengthB1 << 8 | hdr->contentLengthB0;
    int padding_len = hdr->paddingLength;

    *data += content_len + padding_len;  // advance to end of record

    switch (hdr->type) {

        case FCGI_BEGIN_REQUEST: {
            active_request_id = request_id;  // begin handling request
            break;
        }

        case FCGI_ABORT_REQUEST: {
            break;
        }

        case FCGI_END_REQUEST: {
            if (request_id != active_request_id) return -1;  // bad id
            active_request_id = FCGI_NULL_REQUEST_ID;  // no active request
            break;
        }

        case FCGI_PARAMS: {
            break;
        }

        case FCGI_STDIN: {
            break;
        }

        case FCGI_STDOUT: {
            break;
        }

        case FCGI_STDERR: {
            break;
        }

        case FCGI_DATA: {
            break;
        }

        case FCGI_GET_VALUES: {
            struct {
                FCGI_Header header;
                unsigned char body[64];
            } response = {
                .header = {
                    .version = FCGI_VERSION_1,
                    .type = FCGI_GET_VALUES_RESULT,
                    .requestIdB1 = 0,
                    .requestIdB0 = 0,
                    .contentLengthB1 = 0,
                    .contentLengthB0 = 0,
                    .paddingLength = 64,
                    .reserved = 0,
                },
            };
            memset(response.body, 0, sizeof(response.body));
            int offset = 0;
            for (;;) {
                n = get_value(hdr, offset, &response.header);
                if (n <= 0) break;
                offset += n;
            }
            rv = write(FCGI_LISTENSOCK_FILENO, &response, sizeof(response));
            if (rv < 0) {
                perror("write() failed");
                return -1;  // failure
            }
            break;
        }

        case FCGI_GET_VALUES_RESULT:  // invalid request (response type)
        case FCGI_UNKNOWN_TYPE:  // invalid request (response type)
        default: {  // unknown or invalid request type
            FCGI_UnknownTypeRecord response = {
                .header = {
                    .version = FCGI_VERSION_1,
                    .type = FCGI_UNKNOWN_TYPE,
                    .requestIdB1 = 0,
                    .requestIdB0 = 0,
                    .contentLengthB1 = 0,
                    .contentLengthB0 = 1,
                    .paddingLength = 7,
                    .reserved = 0,
                },
                .body = {
                    .type = hdr->type,
                    .reserved = { 0, 0, 0, 0, 0, 0, 0 },
                },
            };
            rv = write(FCGI_LISTENSOCK_FILENO, &response, sizeof(response));
            if (rv < 0) {
                perror("write() failed");
                return -1;  // failure
            }
            break;
        }

    }

    return rv;
}

int
connection(int fd)
{
    int n;
    int rv = 0;  // success

    n = read(fd, proto_buf, sizeof(proto_buf));
    if (n < 0) {
        perror("read() failed");
        return -1;  // failure
    }
    printf("read() got %d octets:\n", n);
    hexdump(stdout, proto_buf, sizeof(proto_buf));

    return rv;
}

int
server()
{
    int fd, rv;

    unlink(SOCK_PATHNAME);  // remove stale UNIX domain socket, if any

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket() failed");
        return -1;  // failure
    }
    if (fd != FCGI_LISTENSOCK_FILENO) {
        perror("fd != FCGI_LISTENSOCK_FILENO");
        return -1;  // failure
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATHNAME, sizeof(addr.sun_path) - 1);
    rv = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rv < 0) {
        perror("bind() failed");
        return -1;  // failure
    }

    struct passwd *pwd = getpwnam(WEB_SERVER_USERNAME);
    if (!pwd) {
        perror("getpwnam() failed");
        return -1;  // failure
    }
    rv = chown(SOCK_PATHNAME, pwd->pw_uid, -1);
    if (rv < 0) {
        perror("chown() failed");
        return -1;  // failure
    }

    rv = listen(FCGI_LISTENSOCK_FILENO, 4);
    if (rv < 0) {
        perror("listen() failed");
        return -1;  // failure
    }

    for (;;) {
        fd = accept(FCGI_LISTENSOCK_FILENO, NULL, NULL);
        if (fd < 0) {
            perror("accept() failed");
            continue;
        }

        rv = connection(fd);
        if (rv < 0) return rv;  // failure

        rv = close(fd);  // close client stream
    }

    rv = close(FCGI_LISTENSOCK_FILENO);  // close listener
    unlink(SOCK_PATHNAME);  // remove UNIX domain socket
    return rv;
}

int
main(int argc, char *argv[])
{
    int rv;

    rv = close(FCGI_LISTENSOCK_FILENO);  // close stdin for listener to re-use
    if (rv < 0) {
        perror("close() failed");
        return -1;  // failure
    }

    rv = server();

    return rv;
}
