#
# on reboot, use this script to rebuild and run the link state demo
#

IF_NAME=${1:-eth0}

cd proto
make
cd ..

cd XDP
make
sudo ip -force link set dev ${IF_NAME} xdp obj link_kern.o
cd ..

cd http
make && sudo make install
sudo pkill link_fcgi
sudo cgi-fcgi -start -connect /run/link_map.sock ./link_fcgi
sudo chown www-data /run/link_map.sock
cd ..

echo link state configuation completed.
