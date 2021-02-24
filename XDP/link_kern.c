/*
 * link_kern.c -- XDP in-kernel eBPF filter
 *
 * Implement Liveness and AIT protocols in XDP
 */
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"
#include "link.h"
#include "code.h"

#define PERMISSIVE   0  // allow non-protocol frames to pass through
#define LOG_LEVEL    1  // log level (0=none, 1=info, 2=debug, 3=trace)

#ifndef __inline
#define __inline  inline __attribute__((always_inline))
#endif

#define memcpy(dst,src,len)  __builtin_memcpy(dst, src, len);
#define memset(dst,val,len)  __builtin_memset(dst, val, len);


/* always print warnings and errors */
#define LOG_WARN(fmt, ...)  LOG_PRINT(0, (fmt), ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  LOG_PRINT(0, (fmt), ##__VA_ARGS__)

#define LOG_PRINT(level, fmt, ...)  bpf_printk((fmt), ##__VA_ARGS__)
#define MAC_PRINT(level, tag, mac)  /* FIXME: NOT IMPLEMENTED */
#define HEX_DUMP(level, buf, len)   /* FIXME: NOT IMPLEMENTED */

#if (LOG_LEVEL < 1)
#define LOG_INFO(fmt, ...)  /* REMOVED */
#define HEX_INFO(buf, len)  /* REMOVED */
#else
#define LOG_INFO(fmt, ...)  LOG_PRINT(1, (fmt), ##__VA_ARGS__)
#define HEX_INFO(buf, len)  HEX_DUMP(1, (buf), (len))
#endif

#if (LOG_LEVEL < 2)
#define LOG_DEBUG(fmt, ...)  /* REMOVED */
#define HEX_DEBUG(buf, len)  /* REMOVED */
#else
#define LOG_DEBUG(fmt, ...)  LOG_PRINT(2, (fmt), ##__VA_ARGS__)
#define HEX_DEBUG(buf, len)  HEX_DUMP(2, (buf), (len))
#endif

#if (LOG_LEVEL < 3)
#define LOG_TRACE(fmt, ...)  /* REMOVED */
#define MAC_TRACE(tag, mac)  /* REMOVED */
#define HEX_TRACE(buf, len)  /* REMOVED */
#else
#define LOG_TRACE(fmt, ...)  LOG_PRINT(3, (fmt), ##__VA_ARGS__)
#define MAC_TRACE(tag, mac)  MAC_PRINT(3, (tag), (mac))
#define HEX_TRACE(buf, len)  HEX_DUMP(3, (buf), (len))
#endif


#include <iproute2/bpf_elf.h>

struct bpf_elf_map link_map SEC("maps") = {
    .type       = BPF_MAP_TYPE_ARRAY,
    .size_key   = sizeof(__u32),
    .size_value = sizeof(link_state_t),
    .pinning    = PIN_GLOBAL_NS,
    .max_elem   = 16,
};

link_state_t *
get_link_state(__u32 if_index)
{
    return bpf_map_lookup_elem(&link_map, &if_index);
}

static __inline int
cmp_mac_addr(void *dst, void *src)
{
    __u8 *d = dst;
    __u8 *s = src;
    int dir;

    dir = ((int)d[5] - (int)s[5]);
    if (dir) return dir;
    dir = ((int)d[4] - (int)s[4]);
    if (dir) return dir;
    dir = ((int)d[3] - (int)s[3]);
    if (dir) return dir;
    dir = ((int)d[2] - (int)s[2]);
    if (dir) return dir;
    dir = ((int)d[1] - (int)s[1]);
    if (dir) return dir;
    dir = ((int)d[0] - (int)s[0]);
    return dir;
}

static __inline int
mac_is_bcast(void *mac)
{
    __u8 *b = mac;

    return ((b[0] & b[1] & b[2] & b[3] & b[4] & b[5]) == 0xFF);
}

#define ob_valid(link)         GET_FLAG(link->user_flags, UF_VALD)
#define ob_set_full(link)      SET_FLAG(link->link_flags, LF_FULL)
#define ob_clr_full(link)      CLR_FLAG(link->link_flags, LF_FULL)
#define copy_payload(dst,src)  memcpy((dst), (src), MAX_PAYLOAD)
#define clear_payload(dst)     memset((dst), null, MAX_PAYLOAD)
#define ib_full(link)          GET_FLAG(link->user_flags, UF_FULL)
#define ib_set_valid(link)     SET_FLAG(link->link_flags, LF_VALD)
#define ib_clr_valid(link)     CLR_FLAG(link->link_flags, LF_VALD)

static int
outbound_AIT(link_state_t *link)
{
/*
    if there is not AIT in progress already
    and there is outbound data to send
    copy the data into the link buffer
    and set AIT-in-progress flags
*/
    if (!GET_FLAG(link->link_flags, LF_SEND)
    &&  ob_valid(link)) {
        SET_FLAG(link->link_flags, LF_SEND);
        ob_set_full(link);
        copy_payload(link->frame + ETH_HLEN + 2, link->outbound);
        link->len = MAX_PAYLOAD;
        LOG_INFO("outbound_AIT (%u octets)\n", link->len);
        HEX_INFO(link->outbound, link->len);
        return 1;  // send AIT
    }
    return 0;  // no AIT
}

static int
clear_AIT(link_state_t *link)
{
/*
    acknowlege successful AIT
    and clear AIT-in-progress flags
*/
    CLR_FLAG(link->link_flags, LF_SEND);
    if (ob_valid(link)) {
        LOG_WARN("clear_AIT: outbound VALID still set!\n");
    }
    LOG_INFO("clear_AIT (%u octets)\n", link->len);
    link->len = 0;
    return 1;  // success
//    return 0;  // failure
}

static int
inbound_AIT(link_state_t *link, __u8 *payload)
{
/*
    if there is not AIT in progress already
    copy the data into the link buffer
    and set AIT-in-progress flags
*/
    LOG_INFO("inbound_AIT (%u octets)\n", link->len);
    if (!GET_FLAG(link->link_flags, LF_RECV)
    &&  (link->len > 0)) {
        SET_FLAG(link->link_flags, LF_RECV);
        copy_payload(link->frame + ETH_HLEN + 2, payload);
        return 1;  // success
    }
    link->len = 0;
    return 0;  // failure
}

static int
release_AIT(link_state_t *link)
{
/*
    if the client has room to accept the AIT
    copy the data from the link buffer
    and clear AIT-in-progress flags
*/
    if (!ib_full(link)) {
        CLR_FLAG(link->link_flags, LF_RECV);
        copy_payload(link->inbound, link->frame + ETH_HLEN + 2);
        ib_set_valid(link);
        LOG_INFO("release_AIT (%u octets)\n", link->len);
        HEX_INFO(link->inbound, link->len);
        return 1;  // AIT released
    }
    return 0;  // reject AIT
}

static int
on_frame_recv(__u8 *data, __u8 *end, link_state_t *link)
{
    protocol_t i;
    protocol_t u;

    // parse protocol state
    __u8 proto = data[ETH_HLEN + 0];
    if ((proto & 0300) != 0200) {
        LOG_WARN("Bad format (proto=0%o)\n", proto);
        return XDP_DROP;  // bad format
    }
    PARSE_PROTO(i, u, proto);
    LOG_DEBUG("  (%u,%u) <--\n", i, u);
    link->i = u;

    // parse payload length
    __u8 len = SMOL2INT(data[ETH_HLEN + 1]);
    if (len > MAX_PAYLOAD) {
        LOG_WARN("Bad format (len=%u > %u)\n", len, MAX_PAYLOAD);
        return XDP_DROP;  // bad format
    }
    __u8 *dst = data;
    __u8 *src = data + ETH_ALEN;
    MAC_TRACE("dst = ", dst);
    MAC_TRACE("src = ", src);
    LOG_TRACE("len = %d\n", len);
    link->len = 0;

    // protocol state machine
    switch (proto) {
        case PROTO(Init, Init) : {
            if (len != 0) {
                LOG_WARN("Unexpected payload (len=%d)\n", len);
                return XDP_DROP;  // unexpected payload
            }
            link->seq = 0;
            link->link_flags = 0;
            link->user_flags = 0;
            if (mac_is_bcast(dst)) {
                LOG_DEBUG("dst mac is bcast\n");
                link->u = Init;
            } else {
                int dir = cmp_mac_addr(dst, src);
                LOG_TRACE("cmp(dst, src) = %d\n", dir);
                if (dir < 0) {
                    if (GET_FLAG(link->link_flags, LF_ENTL)) {
                        LOG_INFO("Drop overlapped Init!\n");
                        return XDP_DROP;  // drop overlapped init
                    }
                    SET_FLAG(link->link_flags, LF_ENTL | LF_ID_B);
                    LOG_DEBUG("ENTL set on send\n");
                    LOG_INFO("Bob sending initial Ping\n");
                    link->u = Ping;
                } else if (dir > 0) {
                    LOG_INFO("Alice breaking symmetry\n");
                    link->u = Init;  // Alice breaking symmetry
                } else {
                    LOG_ERROR("Identical srs/dst mac\n");
                    return XDP_DROP;  // identical src/dst mac
                }
            }
            MAC_TRACE("eth_remote = ", src);
            memcpy(link->frame, src, ETH_ALEN);
            break;
        }
        case PROTO(Init, Ping) : {
            if (cmp_mac_addr(dst, src) < 0) {
                LOG_ERROR("Bob received Ping!\n");
                return XDP_DROP;  // wrong role for ping
            }
            if (GET_FLAG(link->link_flags, LF_ENTL)) {
                LOG_INFO("Drop overlapped Ping!\n");
                return XDP_DROP;  // drop overlapped ping
            }
            SET_FLAG(link->link_flags, LF_ENTL | LF_ID_A);
            LOG_DEBUG("ENTL set on recv\n");
            LOG_INFO("Alice sending initial Pong\n");
            link->u = Pong;
            break;
        }
        case PROTO(Proceed, Ping) : /* FALL-THRU */
        case PROTO(Pong, Ping) : {
            if (cmp_mac_addr(link->frame, src) != 0) {
                MAC_TRACE("expect = ", link->frame);
                MAC_TRACE("actual = ", src);
                LOG_ERROR("Unexpected peer address!\n");
                return XDP_DROP;  // wrong peer mac
            }
            if (!GET_FLAG(link->link_flags, LF_ID_A)) {
                LOG_INFO("Ping is for Alice!\n");
                return XDP_DROP;  // wrong role for ping
            }
            if (outbound_AIT(link)) {
                link->u = Got_AIT;
            } else {
                link->u = Pong;
            }
            break;
        }
        case PROTO(Proceed, Pong) : /* FALL-THRU */
        case PROTO(Ping, Pong) : {
            if (cmp_mac_addr(link->frame, src) != 0) {
                MAC_TRACE("expect = ", link->frame);
                MAC_TRACE("actual = ", src);
                LOG_ERROR("Unexpected peer address!\n");
                return XDP_DROP;  // wrong peer mac
            }
            if (!GET_FLAG(link->link_flags, LF_ID_B)) {
                LOG_INFO("Pong is for Bob!\n");
                return XDP_DROP;  // wrong role for pong
            }
            if (outbound_AIT(link)) {
                link->u = Got_AIT;
            } else {
                link->u = Ping;
            }
            break;
        }
        case PROTO(Ping, Got_AIT) : {
            link->len = len;
            if (inbound_AIT(link, data + ETH_HLEN + 2)) {
                link->u = Ack_AIT;
            } else {
                link->u = Ping;
            }
            break;
        }
        case PROTO(Got_AIT, Ping) : {  // reverse
            CLR_FLAG(link->link_flags, LF_SEND);
            link->u = Pong;  // give the other end a chance to send
            break;
        }
        case PROTO(Pong, Got_AIT) : {
            link->len = len;
            if (inbound_AIT(link, data + ETH_HLEN + 2)) {
                link->u = Ack_AIT;
            } else {
                link->u = Pong;
            }
            break;
        }
        case PROTO(Got_AIT, Pong) : {  // reverse
            CLR_FLAG(link->link_flags, LF_SEND);
            link->u = Ping;  // give the other end a chance to send
            break;
        }
        case PROTO(Got_AIT, Ack_AIT) : {
            link->len = len;
            link->u = Ack_Ack;
            break;
        }
        case PROTO(Ack_AIT, Got_AIT) : {  // reverse
            CLR_FLAG(link->link_flags, LF_RECV);
            if (GET_FLAG(link->link_flags, LF_ID_B)) {
                link->u = Ping;
            } else {
                link->u = Pong;
            }
            break;
        }
        case PROTO(Ack_AIT, Ack_Ack) : {
            link->len = len;
            if (release_AIT(link)) {
                link->u = Proceed;
            } else {
                link->u = Ack_AIT;  // reverse
            }
            break;
        }
        case PROTO(Ack_Ack, Ack_AIT) : {  // reverse
            link->u = Got_AIT;
            break;
        }
        case PROTO(Ack_Ack, Proceed) : {
            link->len = len;
            clear_AIT(link);
            if (GET_FLAG(link->link_flags, LF_ID_B)) {
                link->u = Ping;
            } else {
                link->u = Pong;
            }
            break;
        }
        default: {
            LOG_ERROR("Bad state (%u,%u)\n", i, u);
            return XDP_DROP;  // bad state
        }
    }

    // update flags
    if (!GET_FLAG(link->link_flags, LF_SEND)
    &&  !ob_valid(link)) {
        ob_clr_full(link);
        LOG_TRACE("outbound FULL cleared.\n");
    }
    if (ib_full(link)) {
        ib_clr_valid(link);
        LOG_TRACE("inbound VALD cleared.\n");
    }

    // construct reply frame
    link->seq += 1;
    link->frame[ETH_HLEN + 0] = PROTO(link->i, link->u);
    link->frame[ETH_HLEN + 1] = INT2SMOL(link->len);
    if (link->len == 0) {
        clear_payload(link->frame + ETH_HLEN + 2);
    }
    LOG_DEBUG("  (%u,%u) #%u -->\n", link->i, link->u, link->seq);

    return XDP_TX;  // send updated frame out on same interface
}

SEC("prog") int
xdp_filter(struct xdp_md *ctx)
{
    __u32 data_len = ctx->data_end - ctx->data;
    void *end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    __u32 if_index = ctx->ingress_ifindex;

    if (data + ETH_ZLEN > end) {
        LOG_ERROR("frame too small. expect=%u, actual=%u\n",
            ETH_ZLEN, data_len);
        return XDP_DROP;  // frame too small
    }
    struct ethhdr *eth = data;
    __u16 eth_proto = bpf_ntohs(eth->h_proto);
    if (eth_proto != ETH_P_DALE) {
#if PERMISSIVE
        return XDP_PASS;  // pass frame on to networking stack
#else
        LOG_WARN("wrong protocol. expect=0x%x, actual=0x%x\n",
            ETH_P_DALE, eth_proto);
        return XDP_DROP;  // wrong protocol
#endif
    }

    link_state_t *link = get_link_state(if_index);
    if (!link) {
        LOG_ERROR("failed loading if=%u link_state\n", if_index);
        return XDP_DROP;  // BPF Map failure
    }

    int rc = on_frame_recv(data, end, link);
    LOG_TRACE("recv: proto=0x%x len=%u rc=%d\n", eth_proto, data_len, rc);

    if (rc == XDP_TX) {
        memcpy(data, link->frame, ETH_ZLEN);  // copy frame to i/o buffer
    }

    return rc;
}

char __license[] SEC("license") = "GPL";

