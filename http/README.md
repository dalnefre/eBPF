# HTTP User Interface

A browser-based GUI for demonstration.

## NGINX Web Server

Install [NGINX on RPi](https://www.raspberrypi.org/documentation/remote-access/web-server/nginx.md)

```
$ sudo apt update
$ sudo apt install nginx
$ sudo /etc/init.d/nginx start
```

Allow `pi` group to edit web content.
Web root is `/var/www/html`

```
$ cd /var
$ sudo chgrp -R pi www
$ sudo chmod -R g+w www
```

## FastCGI Interface

We're using [FastCGI](https://fastcgi-archives.github.io/)
to communicate between the webserver and our eBPF map server.
For NGINX, we use the [ngx_http_fastcgi_module](http://nginx.org/en/docs/http/ngx_http_fastcgi_module.html).

### NGINX FastCGI Configuration

NGINX configuration files are in the `/etc/nginx/` directory.
Copy the `default` configuration to `ebpfdemo` and make it the _enabled_ site.

```
$ cd /etc/nginx
$ sudo cp sites-available/default sites-available/ebpfdemo
$ sudo ln -s /etc/nginx/sites-available/ebpfdemo sites-enabled/ebpfdemo
$ sudo rm sites-enabled/default
```

Configuration parameters:

```
server {
        ...

        # pass requests to eBPF "map" FastCGI server
        location /ebpf_map {
                fastcgi_pass unix:/run/ebpf_map.sock;
                fastcgi_param REQUEST_URI     $request_uri;
                fastcgi_param REQUEST_METHOD  $request_method;
                fastcgi_param CONTENT_TYPE    $content_type;
                fastcgi_param CONTENT_LENGTH  $content_length;
                fastcgi_param SCRIPT_FILENAME $request_filename;
                fastcgi_param QUERY_STRING    $query_string;
        }

        ...
}
```

After making configuration changes,
tell NGINX to reload its configuration files.
```
$ sudo nginx -s reload
```

## FastCGI Developer's Kit

Most of the examples for building a FastCGI server
use the [FastCGI Developer's Kit](https://github.com/FastCGI-Archives/fcgi2),
which provides `libfcgi`,
a shim that intercepts most of `stdio.h`
and implements the FastCGI protocol for us.

```
$ sudo apt install gcc make m4 autoconf automake libtool
$ cd ~/dev
$ git clone https://github.com/FastCGI-Archives/fcgi2.git
$ cd fcgi2
$ ./autogen.sh
$ ./configure
$ make
$ sudo make install
```

Confirm that `libfcgi` is available as a shared library.
```
$ ldconfig -p | grep fcgi
```
If nothing is shown,
add `/usr/local/lib` to the paths searched for shared libraries.
```
$ sudo ldconfig /usr/local/lib
```

Once installed,
we should be able to build
[`examples/tiny-fcgi.c`](https://fastcgi-archives.github.io/FastCGI_Developers_Kit_FastCGI.html),
shown here:
```
#include "fcgi_stdio.h"
#include <stdlib.h>

int main(void)
{
    int count = 0;
    while(FCGI_Accept() >= 0)
        printf("Content-type: text/html\r\n"
               "\r\n"

               "<title>FastCGI Hello!</title>"
               "<h1>FastCGI Hello!</h1>"
               "Request number %d running on host <i>%s</i>\n",
                ++count, getenv("SERVER_NAME"));
}
```
This program must be compiled and linked with `libfcgi`:
```
$ cc -O2 -Wall -lfcgi -o tiny-fcgi tiny-fcgi.c
```

There is a similar sample program called `hello_fcgi`
that should be automatically built by `make`.
If it built successfully,
you can use `cgi-fcgi` to start the application server:
```
$ sudo cgi-fcgi -start -connect /run/hello_fcgi.sock ./hello_fcgi
```

Make the UNIX Domain socket available to the web server (repeat after each application server restart):
```
$ sudo chown www-data /run/hello_fcgi.sock
```

Of course, you'll also have to modify the NGINX configuration
to connect to `/run/hello_fgci.sock`:
```
        ...
                fastcgi_pass unix:/run/hello_fcgi.sock;
        ...
```
And, tell NGINX to reload this configuration:
```
$ sudo nginx -s reload
```

Now you should be able to visit [`http://localhost/ebpf_map`](http://localhost/ebpf_map)
in a browser running on the same machine.

If the page does not display properly, check the NGINX error log:
```
$ tail /var/log/nginx/error.log
```

In order to install a new version of the application server,
you'll first have to `kill` the current one:
```
$ ps -ef | grep fcgi
root     16056     1  0 12:14 pts/0    00:00:00 ./hello_fcgi
pi       16132 10015  0 12:39 pts/0    00:00:00 grep --color=auto fcgi
$ sudo kill 16056
```
