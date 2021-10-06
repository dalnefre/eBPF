use crate::actor::{self, Actor, Cap};
use crate::frame::Payload;
use crate::link::LinkEvent;
//use std::convert::TryInto;
//use std::borrow::BorrowMut;
use std::{cell::RefCell, ops::DerefMut};

//use pretty_hex::pretty_hex;
//use crossbeam::crossbeam_channel::unbounded as channel;
use crossbeam::crossbeam_channel::{Receiver, Sender};

#[derive(Debug, Clone)]
pub enum PortEvent {
    Init(Cap<PortEvent>),
    Inbound(Payload),
    AckWrite,
}
impl PortEvent {
    pub fn new_init(port: &Cap<PortEvent>) -> PortEvent {
        PortEvent::Init(port.clone())
    }
    pub fn new_inbound(payload: &Payload) -> PortEvent {
        PortEvent::Inbound(payload.clone())
    }
    pub fn new_ack_write() -> PortEvent {
        PortEvent::AckWrite
    }
}

// Simulated Port for driving AIT link protocol tests
#[derive(Debug, Clone)]
pub struct Port {
    port: Option<Cap<PortEvent>>,
    link: Cap<LinkEvent>,
    tx: Sender<[u8; 44]>,
    rx: Receiver<[u8; 44]>,
    data: RefCell<Option<Payload>>,
}
impl Port {
    pub fn create(
        link: &Cap<LinkEvent>,
        tx: &Sender<[u8; 44]>,
        rx: &Receiver<[u8; 44]>,
    ) -> Cap<PortEvent> {
        let port = actor::create(Port {
            port: None,
            link: link.clone(),
            tx: tx.clone(),
            rx: rx.clone(),
            data: RefCell::new(None),
        });
        port.send(PortEvent::new_init(&port));
        port
    }

    pub fn inbound_ready(&self) -> bool {
        self.tx.is_empty() // if all prior data has been consumed, we are ready for more
    }

    pub fn inbound(&self, payload: &Payload) {
        self.tx.send(payload.data).expect("Port::inbound failed!");
    }

    pub fn outbound(&self) -> Option<Payload> {
        let mut ref_data = self.data.borrow_mut();
        let opt_data = ref_data.deref_mut();
        match opt_data {
            Some(payload) => Some(payload.clone()),
            None => {
                match self.rx.try_recv() {
                    Ok(data) => {
                        let payload = Payload::new(&data);
                        let _ = opt_data.insert(payload.clone());
                        Some(payload)
                    }
                    Err(_) => None, // FIXME: distinguish "empty" from "error"
                }
            }
        }
    }

    pub fn ack_outbound(&self) {
        self.data.replace(None);
    }
}
impl Actor for Port {
    type Event = PortEvent;

    fn on_event(&mut self, event: Self::Event) {
        match &event {
            PortEvent::Init(port) => {
                //self.port = Some(port.clone()); // FIXME: should fail if not None (set once only)
                match &self.port {
                    None => {
                        self.port = Some(port.clone())
                    },
                    Some(_cust) => panic!("Port::port already set"),
                }
            }
            PortEvent::Inbound(payload) => {
                //println!("Port::Inbound");
                if let Some(cust) = &self.port {
                    if self.inbound_ready() {
                        self.inbound(&payload);
                        self.link.send(LinkEvent::new_read(cust)); // Ack Write
                    } else {
                        // try again...
                        cust.send(event);
                    }
                }
            }
            PortEvent::AckWrite => {
                //println!("Port::AckWrite");
                if let Some(cust) = &self.port {
                    match self.outbound() {
                        Some(payload) => {
                            // send next payload
                            self.link.send(LinkEvent::new_write(cust, &payload));
                            self.ack_outbound(); // do this immediately after send, since Link buffers
                        }
                        None => {
                            // try again...
                            cust.send(event);
                        }
                    }
                }
            }
        }
    }
}
