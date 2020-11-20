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
