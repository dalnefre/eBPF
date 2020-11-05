/*
 * ait_user.c -- XDP userspace control program
 *
 * Implement atomic information transfer protocol in XDP
 */
#include <stdio.h>
#include <bpf/bpf.h>

static const char *ait_map_filename = "/sys/fs/bpf/xdp/globals/ait_map";

int
main(int argc, char *argv[])
{
    int rv, fd;
    __u32 key;
    __u64 value;

    rv = bpf_obj_get(ait_map_filename);
    if (rv < 0) {
        perror("bpf_obj_get() failed");
        return -1;
    }
    fd = rv;

    for (int i = 0; i < 4; ++i) {

        key = i;
        rv = bpf_map_lookup_elem(fd, &key, &value);
        if (rv < 0) {
            perror("bpf_map_lookup_elem() failed");
            return -1;
        }

        __u8 *bp = ((__u8 *) &value);
        printf("ait_map[%d] = %02x %02x %02x %02x %02x %02x %02x %02x (%lld)\n",
            key,
            bp[0], bp[1], bp[2], bp[3], bp[4], bp[5], bp[6], bp[7],
            value);

    }

    return 0;
}

#if 0

tc_l2_redirect_user.c
// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016 Facebook
 */
#include <linux/unistd.h>
#include <linux/bpf.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <bpf/bpf.h>

static void usage(void)
{
	printf("Usage: tc_l2_ipip_redirect [...]\n");
	printf("       -U <file>   Update an already pinned BPF array\n");
	printf("       -i <ifindex> Interface index\n");
	printf("       -h          Display this help\n");
}

int main(int argc, char **argv)
{
	const char *pinned_file = NULL;
	int ifindex = -1;
	int array_key = 0;
	int array_fd = -1;
	int ret = -1;
	int opt;

	while ((opt = getopt(argc, argv, "F:U:i:")) != -1) {
		switch (opt) {
		/* General args */
		case 'U':
			pinned_file = optarg;
			break;
		case 'i':
			ifindex = atoi(optarg);
			break;
		default:
			usage();
			goto out;
		}
	}

	if (ifindex < 0 || !pinned_file) {
		usage();
		goto out;
	}

	array_fd = bpf_obj_get(pinned_file);
	if (array_fd < 0) {
		fprintf(stderr, "bpf_obj_get(%s): %s(%d)\n",
			pinned_file, strerror(errno), errno);
		goto out;
	}

	/* bpf_tunnel_key.remote_ipv4 expects host byte orders */
	ret = bpf_map_update_elem(array_fd, &array_key, &ifindex, 0);
	if (ret) {
		perror("bpf_map_update_elem");
		goto out;
	}

out:
	if (array_fd != -1)
		close(array_fd);
	return ret;
}

#endif
