#
# on reboot, use this script to rebuild and run the link demo configuration
#

IF_NAME=${1:-eth0}

cd proto
make
cd ..

cd XDP
make
sudo ip -force link set dev ${IF_NAME} xdp obj ait_kern.o
cd ..

cd http
make && sudo make install
sudo pkill ebpf_fcgi
sudo cgi-fcgi -start -connect /run/ebpf_map.sock ./ebpf_fcgi
sudo chown www-data /run/ebpf_map.sock
cd ..

echo link demo configuation completed.
