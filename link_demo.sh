#
# on reboot, use this script to rebuild and run the link demo configuration
#

cd proto
make
cd ..

cd XDP
make
sudo ip -force link set dev eth0 xdp obj ait_kern.o
cd ..

cd http
make && sudo make install
sudo cgi-fcgi -start -connect /run/ebpf_map.sock ./ebpf_fcgi
sudo chown www-data /run/ebpf_map.sock
cd ..

echo link demo configuation completed.
