#include <stdio.h>
#include <ifaddrs.h>
#include <netpacket/packet.h>

int main (int argc, const char * argv[])
{
    struct ifaddrs *ifaddr=NULL;
    struct ifaddrs *ifa = NULL;
    int i = 0;

    if (getifaddrs(&ifaddr) == -1)
    {
         perror("getifaddrs");
    }
    else
    {
         for ( ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
         {
             if ( (ifa->ifa_addr) && (ifa->ifa_addr->sa_family == AF_PACKET) )
             {
                  struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;
                  printf("%-8s ", ifa->ifa_name);
                  for (i=0; i <s->sll_halen; i++)
                  {
                      printf("%02x%c", (s->sll_addr[i]), (i+1!=s->sll_halen)?':':'\n');
                  }
             }
         }
         freeifaddrs(ifaddr);
    }
    return 0;
}
