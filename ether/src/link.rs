use crate::actor::{self, Actor, Cap};
use crate::frame::{self, Frame, Payload};
use crate::port::{PortEvent, PortState, FailoverInfo};
use crate::wire::WireEvent;
use rand::Rng;

#[derive(Debug, Clone)]
pub enum LinkEvent {
    Frame(Frame),                   // inbound frame received
    Start(Cap<PortEvent>),          // start link activity
    Poll(Cap<PortEvent>),           // link status check
    Stop(Cap<PortEvent>),           // stop link activity
    Read(Cap<PortEvent>),           // reader ready (port-to-link)
    Write(Cap<PortEvent>, Payload), // writer full (port-to-link)
}
impl LinkEvent {
    pub fn new_frame(frame: &Frame) -> LinkEvent {
        LinkEvent::Frame(frame.clone())
    }
    pub fn new_start(port: &Cap<PortEvent>) -> LinkEvent {
        LinkEvent::Start(port.clone())
    }
    pub fn new_poll(port: &Cap<PortEvent>) -> LinkEvent {
        LinkEvent::Poll(port.clone())
    }
    pub fn new_stop(port: &Cap<PortEvent>) -> LinkEvent {
        LinkEvent::Stop(port.clone())
    }
    pub fn new_read(port: &Cap<PortEvent>) -> LinkEvent {
        LinkEvent::Read(port.clone())
    }
    pub fn new_write(port: &Cap<PortEvent>, payload: &Payload) -> LinkEvent {
        LinkEvent::Write(port.clone(), payload.clone())
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum LinkState {
    Stop, // link is disabled
    Init, // ready to become entangled
    Run,  // entangled, but quiet
    Live, // entangled with recent activity
}

pub struct Link {
    wire: Cap<WireEvent>,
    nonce: u32,
    sequence: u16,
    state: LinkState,
    balance: isize,
    reader: Option<Cap<PortEvent>>,
    inbound: Option<Payload>,
    writer: Option<Cap<PortEvent>>,
    outbound: Option<Payload>,
}
impl Link {
    pub fn create(wire: &Cap<WireEvent>, nonce: u32) -> Cap<LinkEvent> {
        actor::create(Link {
            wire: wire.clone(),
            nonce,
            sequence: 0,
            state: LinkState::Stop,
            balance: 0,
            reader: None,
            inbound: None,
            writer: None,
            outbound: None,
        })
    }
}
impl Actor for Link {
    type Event = LinkEvent;

    fn on_event(&mut self, event: Self::Event) {
        match &event {
            LinkEvent::Frame(frame) => {
                if self.state == LinkState::Stop {
                    return; // EARLY EXIT WHEN LINK IS STOPPED.
                } else if frame.is_reset() {
                    self.sequence = 0; // reset sequence number
                    self.state = LinkState::Init;
                    let nonce = frame.get_nonce();
                    println!("Link::nonce={}, frame.nonce={}", self.nonce, nonce);
                    if self.nonce < nonce {
                        println!("waiting...");
                    } else if self.nonce > nonce {
                        println!("entangle...");
                        let seq = frame.get_sequence() + 1; // next sequence number
                        let reply = Frame::new_entangled(seq, frame::TICK, frame::TICK);
                        self.wire.send(WireEvent::new_frame(&reply));
                    } else {
                        println!("collision...");
                        self.nonce = rand::thread_rng().gen();
                        let reply = Frame::new_reset(self.nonce);
                        self.wire.send(WireEvent::new_frame(&reply));
                    }
                } else if frame.is_entangled() {
                    //assert_eq!(self.sequence + 1, frame.get_sequence);
                    self.sequence = frame.get_sequence();
                    self.state = LinkState::Live;
                    let i_state = frame.get_i_state();
                    //println!("entangled i={}", i_state);
                    match i_state {
                        frame::TICK => {
                            //println!("TICK rcvd."); // liveness recv'd
                            if self.balance == 1 {
                                // receive completed
                                println!("TICK w/ surplus");
                                if let Some(reader) = &self.reader {
                                    if let Some(payload) = &self.inbound {
                                        reader.send(
                                            // release payload
                                            PortEvent::new_link_to_port_write(&payload),
                                        );
                                        self.reader = None; // reader satisfied
                                        self.inbound = None; // clear inbound
                                        self.balance = 0; // clear balance
                                    }
                                }
                            }
                            //assert_eq!(self.balance, 0); // at this point, the balance should always be 0
                            match &self.outbound {
                                None => {
                                    let seq = frame.get_sequence() + 1; // next sequence number
                                    let reply = Frame::new_entangled(
                                        seq,
                                        frame::TICK, // liveness
                                        i_state,
                                    );
                                    self.wire.send(WireEvent::new_frame(&reply));
                                    self.balance = 0; // clear balance
                                }
                                Some(payload) => {
                                    let seq = frame.get_sequence() + 1; // next sequence number
                                    let mut reply = Frame::new_entangled(
                                        seq,
                                        frame::TECK, // begin AIT
                                        i_state,
                                    );
                                    reply.set_payload(&payload);
                                    self.wire.send(WireEvent::new_frame(&reply));
                                    self.balance = -1; // deficit balance
                                }
                            }
                        }
                        frame::TECK => {
                            let payload = frame.get_payload();
                            println!("TECK rcvd."); // begin AIT recv'd
                            match &self.reader {
                                Some(_cust) => {
                                    // reader ready
                                    self.inbound = Some(payload);
                                    let seq = frame.get_sequence() + 1; // next sequence number
                                    let reply = Frame::new_entangled(
                                        seq,
                                        frame::TACK, // Ack AIT
                                        i_state,
                                    );
                                    self.wire.send(WireEvent::new_frame(&reply));
                                    self.balance = 1; // surplus balance
                                }
                                None => {
                                    // no reader ready
                                    let seq = frame.get_sequence() + 1; // next sequence number
                                    let mut reply = Frame::new_entangled(
                                        seq,
                                        frame::RTECK, // reject AIT
                                        i_state,
                                    );
                                    reply.set_payload(&payload);
                                    self.wire.send(WireEvent::new_frame(&reply));
                                    //self.balance = 0; // balance unchanged
                                }
                            }
                        }
                        frame::TACK => {
                            println!("TACK rcvd."); // Ack AIT recv'd
                            assert_eq!(self.balance, -1); // deficit expected
                            println!("TACK w/ deficit");
                            if let Some(writer) = &self.writer {
                                writer.send(PortEvent::new_link_to_port_read()); // acknowlege write
                                self.writer = None; // writer satisfied
                                self.outbound = None; // clear outbound
                                self.balance = 0; // clear balance
                                let seq = frame.get_sequence() + 1; // next sequence number
                                let reply = Frame::new_entangled(
                                    seq,
                                    frame::TICK, // liveness (Ack Ack)
                                    i_state,
                                );
                                self.wire.send(WireEvent::new_frame(&reply));
                            }
                        }
                        frame::RTECK => {
                            println!("RTECK rcvd."); // Reject AIT recv'd
                            let seq = frame.get_sequence() + 1; // next sequence number
                            let reply = Frame::new_entangled(
                                seq,
                                frame::TICK, // liveness
                                i_state,
                            );
                            self.wire.send(WireEvent::new_frame(&reply));
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
            LinkEvent::Start(cust) => {
                let init = Frame::new_reset(self.nonce);
                self.wire.send(WireEvent::new_frame(&init)); // send init/reset
                self.state = LinkState::Init;
                let state = PortState::new(&self.state, self.balance, self.sequence);
                let info = FailoverInfo::new(
                    &state,
                    &self.inbound,
                    &self.outbound,
                );
                cust.send(PortEvent::new_failover(&info));
            }
            LinkEvent::Stop(cust) => {
                // FIXME: possible race if the Link gets activity (from the Wire)
                //        before the Port tells the Link to Stop.
                self.state = LinkState::Stop;
                let state = PortState::new(&self.state, self.balance, self.sequence);
                let info = FailoverInfo::new(
                    &state,
                    &self.inbound,
                    &self.outbound,
                );
                cust.send(PortEvent::new_failover(&info));
                // reset link state after reporting fail-over info
                self.balance = 0;
                self.reader = None;
                self.inbound = None;
                self.writer = None;
                self.outbound = None;
                // FIXME: what should we do about self.reader and self.writer?
            }
            LinkEvent::Poll(cust) => {
                let state = PortState::new(&self.state, self.balance, self.sequence);
                cust.send(PortEvent::new_poll_reply(&state));
                if self.state == LinkState::Live {
                    self.state = LinkState::Run; // clear Live status
                }
            }
            LinkEvent::Read(cust) => match &self.reader {
                None => {
                    self.reader = Some(cust.clone());
                }
                Some(_cust) => panic!("Only one Link-to-Port reader allowed"),
            },
            LinkEvent::Write(cust, payload) => match &self.writer {
                None => {
                    self.outbound = Some(payload.clone());
                    self.writer = Some(cust.clone());
                }
                Some(_cust) => panic!("Only one Port-to-Link writer allowed"),
            },
        }
    }
}
