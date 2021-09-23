//use std::convert::TryInto;
//use std::borrow::BorrowMut;
use std::{cell::RefCell, ops::DerefMut};

//use pretty_hex::pretty_hex;
use rand::Rng;
//use crossbeam::crossbeam_channel::unbounded as channel;
use crossbeam::crossbeam_channel::{Receiver, Sender};

// Simulated Port for driving AIT link protocol tests
#[derive(Debug, Clone)]
pub struct Port {
    tx: Sender<[u8; 44]>,
    rx: Receiver<[u8; 44]>,
    data: RefCell<Option<[u8; 44]>>,
}
impl Port {
    pub fn new(tx: Sender<[u8; 44]>, rx: Receiver<[u8; 44]>) -> Port {
        Port { tx, rx, data: RefCell::new(None) }
    }

    pub fn inbound_ready(&self) -> bool {
        self.tx.is_empty() // if all prior data has been consumed, we are ready for more
    }

    pub fn inbound(&self, data: [u8; 44]) -> Result<(), Error> {
        match self.tx.send(data) {
            Ok(_) => Ok(()),
            Err(e) => {
                println!("Port::in ERROR! {}", e);
                Err("Port send failed")
            },
        }
    }

    pub fn outbound(&self) -> Result<[u8; 44], Error> {
        let mut ref_data = self.data.borrow_mut();
        let opt_data = ref_data.deref_mut();
        match opt_data {
            Some(data) => {
                Ok(data.clone())
            },
            None => {
                match self.rx.try_recv() {
                    Ok(data) => {
                        let _ = opt_data.insert(data.clone()); // data is Copy, clone() would be implicit
                        Ok(data)
                    },
                    Err(_) => Err("Port recv failed"),  // FIXME: distinguish "empty" from "error"
                }
            },
        }
    }

    pub fn ack_outbound(&self) {
        self.data.replace(None);
    }
}

use crate::reactor::*;
extern crate alloc;
//use alloc::boxed::Box;
use alloc::rc::Rc;
use crate::frame::{self, Frame};

pub struct LinkBeh {
    port: Port,
    wire: Rc<Actor>,
    nonce: u32,
    balance: isize,
}
impl LinkBeh {
    pub fn new(port: Port, wire: &Rc<Actor>, nonce: u32, balance: isize) -> Box<dyn Behavior> {
        Box::new(LinkBeh {
            port,
            wire: Rc::clone(&wire),
            nonce,
            balance,
        })
    }
}
impl Behavior for LinkBeh {
    fn react(&self, event: Event) -> Result<Effect, Error> {
        let mut effect = Effect::new();
        match event.message {
            Message::Frame(data) => {
                // Frame received from the wire
                match Frame::new(&data[..]) {
                    Ok(frame) => {
                        if frame.is_reset() {
                            let nonce = frame.get_tree_id();
                            if self.nonce < nonce {
                                println!("waiting...");
                                Ok(effect)
                            } else if self.nonce > nonce {
                                println!("entangle...");
                                let mut reply = Frame::default();
                                reply.set_i_state(frame::TICK); // initiate liveness
                                reply.set_tree_id(self.nonce);
                                effect.send(&self.wire, Message::Frame(reply.data));
                                Ok(effect)
                            } else {
                                println!("collision...");
                                let nonce: u32 = rand::thread_rng().gen();
                                effect.update(LinkBeh::new(self.port.clone(), &self.wire, nonce, 0))?;
                                let mut reply = Frame::default();
                                reply.set_reset(); // send reset/init
                                reply.set_tree_id(self.nonce);
                                effect.send(&self.wire, Message::Frame(reply.data));
                                Ok(effect)
                            }
                        } else if frame.is_entangled() {
                            let i_state = frame.get_i_state();
                            //println!("entangled i={}...", i_state);
                            if i_state == frame::TICK {
                                //println!("TICK rcvd."); // liveness recv'd
                                let mut reply = Frame::default();
                                reply.set_u_state(i_state);
                                match self.port.outbound() {
                                    Ok(payload) => {
                                        reply.set_i_state(frame::TECK); // send begin AIT
                                        reply.set_tree_id(self.nonce); // FIXME: ait destination address?
                                        reply.set_payload(payload);
                                        effect.update(LinkBeh::new(self.port.clone(), &self.wire, self.nonce, -1))?;
                                    },
                                    Err(_) => {
                                        reply.set_i_state(frame::TICK); // send liveness
                                        reply.set_tree_id(self.nonce);
                                        effect.update(LinkBeh::new(self.port.clone(), &self.wire, self.nonce, 0))?;
                                    },
                                }
                                effect.send(&self.wire, Message::Frame(reply.data));
                                Ok(effect)
                            } else if i_state == frame::TECK {
                                println!("TECK rcvd."); // begin AIT recv'd
                                let mut reply = Frame::default();
                                reply.set_u_state(i_state);
                                if self.port.inbound_ready() {
                                    let _nonce = frame.get_tree_id(); // FIXME: ait destination address?
                                    let payload = frame.get_payload();
                                    self.port.inbound(payload)?;
                                    reply.set_i_state(frame::TACK); // send Ack AIT
                                    effect.update(LinkBeh::new(self.port.clone(), &self.wire, self.nonce, 1))?;
                                } else {
                                    reply.set_i_state(frame::RTECK); // send Reject AIT
                                    reply.set_tree_id(self.nonce);
                                    effect.update(LinkBeh::new(self.port.clone(), &self.wire, self.nonce, 0))?;
                                }
                                effect.send(&self.wire, Message::Frame(reply.data));
                                Ok(effect)
                            } else if i_state == frame::TACK {
                                println!("TACK rcvd."); // Ack AIT recv'd
                                self.port.ack_outbound();
                                let mut reply = Frame::default();
                                reply.set_u_state(i_state);
                                reply.set_i_state(frame::TICK); // send liveness
                                reply.set_tree_id(self.nonce);
                                effect.update(LinkBeh::new(self.port.clone(), &self.wire, self.nonce, 0))?;
                                effect.send(&self.wire, Message::Frame(reply.data));
                                Ok(effect)
                            } else if i_state == frame::RTECK {
                                println!("RTECK rcvd."); // Reject AIT recv'd
                                let mut reply = Frame::default();
                                reply.set_u_state(i_state);
                                reply.set_i_state(frame::TICK); // send liveness
                                reply.set_tree_id(self.nonce);
                                effect.update(LinkBeh::new(self.port.clone(), &self.wire, self.nonce, self.balance))?;
                                effect.send(&self.wire, Message::Frame(reply.data));
                                Ok(effect)
                            } else {
                                println!("bad state.");
                                Err("bad protocol state")
                            }
                        } else {
                            println!("bad frame.");
                            Err("bad frame format")
                        }
                    }
                    Err(_) => Err("bad frame data"),
                }
            }
            _ => Err("unknown message"),
            //_ => Err(format!("unknown message {:?}", event.message)),
        }
        //Ok(effect)
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
