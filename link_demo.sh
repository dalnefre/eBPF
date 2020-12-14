#
# on reboot, use this script to set up the link demo configuration
#

cd XDP
sudo ip -force link set dev eth0 xdp obj ait_kern.o
cd ..

cd http
sudo cgi-fcgi -start -connect /run/ebpf_map.sock ./ebpf_fcgi
sudo chown www-data /run/ebpf_map.sock
cd ..

echo link demo configuation completed.
