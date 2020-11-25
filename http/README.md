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
