#include <stdlib.h>
#include <stdio.h>
#include <sys/sysinfo.h>

int
main(int argc, char *argv[])
{
    printf("This system has "
            "%d processors configured and "
            "%d processors available.\n",
            get_nprocs_conf(), get_nprocs());
    exit(EXIT_SUCCESS);
}
