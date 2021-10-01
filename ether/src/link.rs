use crate::actor::{Actor, Cap};
use crate::frame::{self, Frame};
use crate::port::PortEvent;
use crate::wire::WireEvent;
use rand::Rng;

#[derive(Debug, Clone)]
pub enum LinkEvent {
    Frame(Frame),
    Read(Cap<PortEvent>),
    Write(Cap<PortEvent>, [u8; 44]),
}

pub struct Link {
    wire: Cap<WireEvent>,
    nonce: u32,
    balance: isize,
    reader: Option<Cap<PortEvent>>,
    inbound: Option<[u8; 44]>,
    writer: Option<Cap<PortEvent>>,
    outbound: Option<[u8; 44]>,
}
impl Link {
    pub fn new(wire: Cap<WireEvent>, nonce: u32) -> Link {
        Link {
            wire,
            nonce,
            balance: 0,
            reader: None,
            inbound: None,
            writer: None,
            outbound: None,
        }
    }
}
impl Actor for Link {
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
                    //println!("entangled i={}", i_state);
                    match i_state {
                        frame::TICK => {
                            println!("TICK rcvd."); // liveness recv'd
                            if self.balance == -1 { // send completed
                                println!("TICK deficit");
                                let writer = self.writer.clone().expect("Link::writer not set!");
                                writer.send(PortEvent::AckWrite()); // acknowlege write
                                self.writer = None; // writer satisfied
                                self.outbound = None; // clear outbound
                                self.balance = 0; // clear balance
                            } else if self.balance == 1 { // receive completed
                                println!("TICK surplus");
                                let reader = self.reader.clone().expect("Link::reader not set!");
                                let payload = self.inbound.clone().expect("Link::inbound not set!");
                                reader.send(PortEvent::Inbound(payload));  // release payload
                                self.reader = None; // reader satisfied
                                self.inbound = None; // clear inbound
                                self.balance = 0; // clear balance
                            } else { // no AIT
                                //self.balance = 0; // balance already clear?
                            }
                            assert_eq!(self.balance, 0); // at this point, the balance should always be 0
                            match self.outbound {
                                None => {
                                    let reply = Frame::entangled(
                                        self.nonce,
                                        frame::TICK, // liveness
                                        i_state,
                                    );
                                    self.wire.send(WireEvent::Frame(reply.data));
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
                                }
                            }
                        }
                        frame::TECK => {
                            println!("TECK rcvd."); // begin AIT recv'd
                            match &self.reader {
                                Some(_cust) => { // reader ready
                                    self.inbound = Some(frame.get_payload());
                                    let reply = Frame::entangled(
                                        self.nonce,
                                        frame::TACK, // Ack AIT
                                        i_state,
                                    );
                                    self.wire.send(WireEvent::Frame(reply.data));
                                    self.balance = 1; // surplus balance
                                },
                                None => { // no reader ready
                                    let reply = Frame::entangled(
                                        self.nonce,
                                        frame::RTECK, // reject AIT
                                        i_state,
                                    );
                                    self.wire.send(WireEvent::Frame(reply.data));
                                    //self.balance = 0; // balance already clear?
                                    assert_eq!(self.balance, 0);
                                },
                            }
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
            },
            LinkEvent::Read(cust) => {
                match &self.reader {
                    None => { self.reader = Some(cust) },
                    Some(_cust) => panic!("Only one Link-to-Port reader allowed"),
                }
            },
            LinkEvent::Write(cust, payload) => {
                match &self.writer {
                    None => { self.writer = Some(cust) },
                    Some(_cust) => panic!("Only one Port-to-Link writer allowed"),
                }
                self.outbound = Some(payload);
            },
        }
    }
}
