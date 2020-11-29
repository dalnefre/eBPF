/*
 * ebpf_fcgi.c -- eBPF "map" FastCGI server
 */
#include <fcgi_stdio.h>
#include <stdlib.h>

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
    printf("</body>\n");
    printf("</html>\n");
};

int
main(void)
{
    int count = 0;

    while(FCGI_Accept() >= 0) {
        http_header("text/html");
        html_body(count++);
    }

    return 0;
}
