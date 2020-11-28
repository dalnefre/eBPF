#include <fcgi_stdio.h>
#include <stdlib.h>

int main(void)
{
    int count = 0;
    while(FCGI_Accept() >= 0) {
        printf("Content-type: text/html\r\n");
        printf("\r\n");
        printf("Hello world!<br>\r\n");
        printf("Request number %d.", count++);
    }
    exit(0);
}
