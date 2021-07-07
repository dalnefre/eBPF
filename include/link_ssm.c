/*
 * link_ssm.c -- Link Protocol Shared State Machine
 *
 * Implement Liveness and AIT protocols
 *
 * WARNING! THIS CODE IS MEANT TO BECOME #include'D INTO ANOTHER SOURCE FILE
 * In order to compile correctly, the host file must define several symbols:
 *	LOG_WARN, LOG_ERROR, LOG_INFO, LOG_DEBUG, LOG_TRACE,
 *	HEX_INFO, HEX_DEBUG, HEX_TRACE, MAC_TRACE, LOG_TEMP,
 *	XDP_ABORTED, XDP_DROP, XDP_PASS, XDP_TX, XDP_REDIRECT
 * the definitions in "../include/link.h" are also required.
 */

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

#define copy_payload(dst,src)  memcpy((dst), (src), MAX_PAYLOAD)
#define clear_payload(dst)     memset((dst), null, MAX_PAYLOAD)

static int
outbound_AIT(user_state_t *user, link_state_t *link)
{
/*
    if there is not AIT in progress already
    and there is outbound data to send
    copy the data into the link buffer
    and set AIT-in-progress flags
*/
    if ((GET_FLAG(user->user_flags, UF_FULL)
      && !GET_FLAG(link->link_flags, LF_BUSY))
    ||  GET_FLAG(link->link_flags, LF_SEND)) {
//    LOG_TEMP("outbound_AIT: user_flags=0x%x link_flags=0x%x\n",
//        user->user_flags, link->link_flags);
        if (GET_FLAG(link->link_flags, LF_BUSY)) {
            LOG_TEMP("outbound_AIT: resending (LF_BUSY)\n");
        } else {
            LOG_TEMP("outbound_AIT: setting LF_SEND + LF_BUSY\n");
            SET_FLAG(link->link_flags, LF_SEND);
            SET_FLAG(link->link_flags, LF_BUSY);
        }
        copy_payload(link->frame + ETH_HLEN + 2, user->outbound);
        link->len = MAX_PAYLOAD;
        LOG_INFO("outbound_AIT (%u octets)\n", link->len);
        HEX_INFO(user->outbound, link->len);
        return 1;  // send AIT
    }
    return 0;  // no AIT
}

static int
inbound_AIT(user_state_t *user, link_state_t *link, __u8 *payload)
{
/*
    if there is not AIT in progress already
    copy the data into the link buffer
    and set AIT-in-progress flags
*/
    LOG_INFO("inbound_AIT (%u octets)\n", link->len);
    if (!GET_FLAG(link->link_flags, LF_RECV)
    &&  (link->len > 0)) {
        LOG_TEMP("inbound_AIT: setting LF_RECV\n");
        SET_FLAG(link->link_flags, LF_RECV);
        copy_payload(link->frame + ETH_HLEN + 2, payload);
        return 1;  // success
    }
    link->len = 0;
    return 0;  // failure
}

static int
release_AIT(user_state_t *user, link_state_t *link)
{
/*
    if the client has room to accept the AIT
    copy the data from the link buffer
    and clear AIT-in-progress flags
*/
    if (GET_FLAG(link->link_flags, LF_RECV)
    &&  !GET_FLAG(user->user_flags, UF_BUSY)
    &&  !GET_FLAG(link->link_flags, LF_FULL)) {
        LOG_TEMP("release_AIT: setting LF_FULL\n");
        copy_payload(link->inbound, link->frame + ETH_HLEN + 2);
        SET_FLAG(link->link_flags, LF_FULL);
        LOG_INFO("release_AIT (%u octets)\n", link->len);
        HEX_INFO(link->inbound, link->len);
        return 1;  // AIT released
    }
    return 0;  // reject AIT
}

static int
clear_AIT(user_state_t *user, link_state_t *link)
{
/*
    acknowlege successful AIT
    and clear AIT-in-progress flags
*/
    if (GET_FLAG(link->link_flags, LF_SEND)) {
        LOG_TEMP("clear_AIT: setting !LF_SEND\n");
        CLR_FLAG(link->link_flags, LF_SEND);
        if (GET_FLAG(link->link_flags, LF_BUSY)
        &&  !GET_FLAG(user->user_flags, UF_FULL)) {
            LOG_TEMP("clear_AIT: setting !LF_BUSY\n");
            CLR_FLAG(link->link_flags, LF_BUSY);
            LOG_INFO("clear_AIT (%u octets)\n", link->len);
        } else {
            LOG_WARN("clear_AIT: outbound VALID still set!\n");
        }
    } else {
        LOG_WARN("clear_AIT: outbound SEND not set!\n");
    }
    link->len = 0;
    return 1;  // success
//    return 0;  // failure
}

static int
on_frame_recv(__u8 *data, __u8 *end, user_state_t *user, link_state_t *link)
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
    if ((i < Got_AIT) && (u < Got_AIT)) {
        LOG_TRACE("  (%u,%u) <--\n", i, u);
    } else {
        LOG_DEBUG("  (%u,%u) <--\n", i, u);
    }
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

    // update async flags
    if (!GET_FLAG(link->link_flags, LF_SEND)
    &&  GET_FLAG(link->link_flags, LF_BUSY)
    &&  !GET_FLAG(user->user_flags, UF_FULL)) {
        LOG_TEMP("on_frame_recv: setting !LF_BUSY\n");
        CLR_FLAG(link->link_flags, LF_BUSY);
        LOG_TRACE("outbound BUSY cleared.\n");
    }
    if (GET_FLAG(user->user_flags, UF_BUSY)
    &&  GET_FLAG(link->link_flags, LF_RECV)
    &&  GET_FLAG(link->link_flags, LF_FULL)) {
        LOG_TEMP("on_frame_recv: setting !LF_FULL + !LF_RECV\n");
        CLR_FLAG(link->link_flags, LF_FULL);
        CLR_FLAG(link->link_flags, LF_RECV);
        LOG_TRACE("inbound FULL + RECV cleared.\n");
    }

    // protocol state machine
    switch (proto) {
        case PROTO(Init, Init) : {
            if (len != 0) {
                LOG_WARN("Unexpected payload (len=%d)\n", len);
                return XDP_DROP;  // unexpected payload
            }
            link->seq = 0;
            LOG_TEMP("on_frame_recv: clearing LF_* + UF_*\n");
            link->link_flags = 0;
//            user->user_flags = 0;  // FIXME: can't write to user_state?
            if (mac_is_bcast(dst)) {
                LOG_INFO("Init: dst mac is bcast\n");
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
            if (outbound_AIT(user, link)) {
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
            if (outbound_AIT(user, link)) {
                link->u = Got_AIT;
            } else {
                link->u = Ping;
            }
            break;
        }
        case PROTO(Ping, Got_AIT) : {
            link->len = len;
            LOG_TEMP("on_frame_recv: (Ping, Got_AIT) len=%d\n", len);
            if (inbound_AIT(user, link, data + ETH_HLEN + 2)) {
                link->u = Ack_AIT;
            } else {
                link->u = Ping;
            }
            break;
        }
        case PROTO(Got_AIT, Ping) : {  // reverse
            link->u = Pong;  // give the other end a chance to send
            break;
        }
        case PROTO(Pong, Got_AIT) : {
            link->len = len;
            LOG_TEMP("on_frame_recv: (Pong, Got_AIT) len=%d\n", len);
            if (inbound_AIT(user, link, data + ETH_HLEN + 2)) {
                link->u = Ack_AIT;
            } else {
                link->u = Pong;
            }
            break;
        }
        case PROTO(Got_AIT, Pong) : {  // reverse
            link->u = Ping;  // give the other end a chance to send
            break;
        }
        case PROTO(Got_AIT, Ack_AIT) : {
            link->len = len;
            LOG_TEMP("on_frame_recv: (Got_AIT, Ack_AIT) len=%d\n", len);
            link->u = Ack_Ack;
            break;
        }
        case PROTO(Ack_AIT, Got_AIT) : {  // reverse
            LOG_TEMP("on_frame_recv: clearing LF_RECV (rev Got_AIT)\n");
            CLR_FLAG(link->link_flags, LF_RECV);
            // FIXME: consider sending AIT, if we have data to send
            if (GET_FLAG(link->link_flags, LF_ID_B)) {
                link->u = Ping;
            } else {
                link->u = Pong;
            }
            break;
        }
        case PROTO(Ack_AIT, Ack_Ack) : {
            link->len = len;
            LOG_TEMP("on_frame_recv: (Ack_AIT, Ack_Ack) len=%d\n", len);
            if (release_AIT(user, link)) {
                link->u = Proceed;
            } else {
                LOG_TEMP("on_frame_recv: release failed, reversing!\n");
                link->u = Ack_AIT;  // reverse
            }
            break;
        }
        case PROTO(Ack_Ack, Ack_AIT) : {  // reverse
            LOG_TEMP("on_frame_recv: reverse Ack_AIT\n");
            link->u = Got_AIT;
            break;
        }
        case PROTO(Ack_Ack, Proceed) : {
            link->len = len;
            LOG_TEMP("on_frame_recv: (Ack_Ack, Proceed) len=%d\n", len);
            clear_AIT(user, link);
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

    // construct reply frame
    link->seq += 1;
    link->frame[ETH_HLEN + 0] = PROTO(link->i, link->u);
    link->frame[ETH_HLEN + 1] = INT2SMOL(link->len);
    if (link->len == 0) {
        clear_payload(link->frame + ETH_HLEN + 2);
    }
    if ((link->i < Got_AIT) && (link->u < Got_AIT)) {
        LOG_TRACE("  (%u,%u) #%u -->\n", link->i, link->u, link->seq);
    } else {
        LOG_DEBUG("  (%u,%u) #%u -->\n", link->i, link->u, link->seq);
    }

    return XDP_TX;  // send updated frame out on same interface
}
