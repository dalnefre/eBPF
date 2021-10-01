use crate::actor::{self, Cap};
use crate::frame::{self, Frame};
use crate::wire::WireEvent;
use rand::Rng;

#[derive(Debug, Clone)]
pub enum LinkEvent {
    Frame(Frame),
}

pub struct Link {
    wire: Cap<WireEvent>,
    nonce: u32,
    balance: isize,
    inbound: Option<[u8; 44]>,
    outbound: Option<[u8; 44]>,
}
impl Link {
    pub fn new(wire: Cap<WireEvent>, nonce: u32) -> Link {
        Link {
            wire,
            nonce,
            balance: 0,
            inbound: None,
            outbound: None,
        }
    }
}
impl actor::Actor for Link {
    type Event = LinkEvent;

    fn on_event(&mut self, event: Self::Event) {
        match event {
            LinkEvent::Frame(frame) => {
                if frame.is_reset() {
                    let nonce = frame.get_tree_id();
                    if self.nonce < nonce {
                        println!("waiting...");
                    } else if self.nonce > nonce {
                        println!("entangle...");
                        let reply = Frame::entangled(self.nonce, frame::TICK, frame::TICK);
                        self.wire.send(WireEvent::Frame(reply.data));
                    } else {
                        println!("collision...");
                        self.nonce = rand::thread_rng().gen();
                        let reply = Frame::reset(self.nonce);
                        self.wire.send(WireEvent::Frame(reply.data));
                    }
                } else if frame.is_entangled() {
                    let i_state = frame.get_i_state();
                    //println!("entangled i={}...", i_state);
                    match i_state {
                        frame::TICK => {
                            //println!("TICK rcvd."); // liveness recv'd
                            match self.outbound {
                                None => {
                                    let reply = Frame::entangled(
                                        self.nonce,
                                        frame::TICK, // liveness
                                        i_state,
                                    );
                                    self.wire.send(WireEvent::Frame(reply.data));
                                    self.balance = 0; // clear balance (if any)
                                }
                                Some(payload) => {
                                    let mut reply = Frame::entangled(
                                        self.nonce,
                                        frame::TECK, // begin AIT
                                        i_state,
                                    );
                                    reply.set_payload(payload);
                                    self.wire.send(WireEvent::Frame(reply.data));
                                    self.balance = -1; // deficit balance
                                    self.outbound = None; // clear outbound
                                }
                            }
                        }
                        frame::TECK => {
                            println!("TECK rcvd."); // begin AIT recv'd
                            self.inbound = Some(frame.get_payload());
                            let reply = Frame::entangled(
                                self.nonce,
                                frame::RTECK, // reject AIT
                                i_state,
                            );
                            self.wire.send(WireEvent::Frame(reply.data));
                        }
                        frame::TACK => {
                            println!("TACK rcvd."); // Ack AIT recv'd
                            todo!("handle TACK");
                        }
                        frame::RTECK => {
                            println!("RTECK rcvd."); // Reject AIT recv'd
                            let reply = Frame::entangled(
                                self.nonce,
                                frame::TICK, // liveness
                                i_state,
                            );
                            self.wire.send(WireEvent::Frame(reply.data));
                            self.balance = 0; // clear deficit
                        }
                        _ => {
                            panic!("bad protocol state");
                        }
                    }
                } else {
                    panic!("bad frame format");
                }
            }
        }
    }
}

/*** Reference Implementation in Humus

# The _link_ is modeled as two separate endpoints,
# one in each node, connected by a _wire_.
# Each endpoint has:
#   * a nonce (for symmetry breaking)
#   * a liveness flag
#   * an AIT buffer (reader, writer, out_pkt)
#   * an information balance counter
LET link_beh(wire, nonce, live, ait, xfer) = \msg.[
    CASE msg OF
    (cust, #poll) : [
        SEND (SELF, nonce, live, ait, xfer) TO cust
        BECOME link_beh(wire, nonce, FALSE, ait, xfer)
    ]
    ($wire, #TICK) : [
        CASE ait OF
        (_, ?, _) : [  # entangled liveness
            SEND (SELF, #TICK) TO wire
            BECOME link_beh(wire, nonce, TRUE, ait, 0)
        ]
        (_, _, out_pkt) : [  # initiate AIT
            SEND (SELF, #TECK, out_pkt) TO wire
            BECOME link_beh(wire, nonce, TRUE, ait, -1)
        ]
        END
    ]
    ($wire, #TECK, in_pkt) : [
        CASE ait OF
        (?, _, _) : [  # no reader, reject AIT
            SEND (#LINK, SELF, #REVERSE, msg, ait) TO println
            SEND (SELF, #~TECK, in_pkt) TO wire
            BECOME link_beh(wire, nonce, TRUE, ait, xfer)
        ]
        (r, w, out_pkt) : [  # deliver AIT received
            SEND (SELF, #TACK, in_pkt) TO wire
            SEND (SELF, #write, in_pkt) TO r
            BECOME link_beh(wire, nonce, TRUE, (?, w, out_pkt), 1)
        ]
        END
    ]
    ($wire, #TACK, _) : [
        LET (r, w, _) = $ait
        SEND (SELF, #TICK) TO wire
        SEND (SELF, #read) TO w
        BECOME link_beh(wire, nonce, TRUE, (r, ?, ?), 0)
    ]
    ($wire, #~TECK, _) : [
        SEND (SELF, #TICK) TO wire
        BECOME link_beh(wire, nonce, TRUE, ait, 0)
    ]
    (cust, #read) : [
        CASE ait OF
        (?, w, out_pkt) : [
            BECOME link_beh(wire, nonce, live, (cust, w, out_pkt), xfer)
        ]
        _ : [ THROW (#Unexpected, NOW, SELF, msg, live, ait) ]
        END
    ]
    (cust, #write, out_pkt) : [
        CASE ait OF
        (r, ?, _) : [
            BECOME link_beh(wire, nonce, live, (r, cust, out_pkt), xfer)
        ]
        _ : [ THROW (#Unexpected, NOW, SELF, msg, live, ait) ]
        END
    ]
    ($wire, #INIT, nonce') : [
        CASE compare(nonce, nonce') OF
        1 : [  # entangle link
            SEND (NOW, SELF, #entangle) TO println
            SEND (SELF, #TICK) TO wire
        ]
        -1 : [  # ignore (wait for other endpoint to entangle)
            SEND (NOW, SELF, #waiting) TO println
        ]
        _ : [  # error! re-key...
            SEND (NOW, SELF, #re-key) TO println
            SEND (SELF, nonce_limit) TO random
        ]
        END
    ]
    (_, _) : [ THROW (#Unexpected, NOW, SELF, msg) ]
    nonce' : [
        BECOME link_beh(wire, nonce', live, ait, xfer)
        SEND (SELF, #INIT, nonce') TO wire
    ]
    END
]

***/
