use crate::actor::{self, Actor, Cap};
use crate::link::LinkEvent;
//use std::convert::TryInto;
//use std::borrow::BorrowMut;
use std::{cell::RefCell, ops::DerefMut};

//use pretty_hex::pretty_hex;
//use crossbeam::crossbeam_channel::unbounded as channel;
use crate::reactor::Error;
use crossbeam::crossbeam_channel::{Receiver, Sender};

#[derive(Debug, Clone)]
pub enum PortEvent {
    Init(Cap<PortEvent>),
    Inbound([u8; 44]),
    AckWrite(),
}

// Simulated Port for driving AIT link protocol tests
#[derive(Debug, Clone)]
pub struct Port {
    port: Option<Cap<PortEvent>>,
    link: Cap<LinkEvent>,
    tx: Sender<[u8; 44]>,
    rx: Receiver<[u8; 44]>,
    data: RefCell<Option<[u8; 44]>>,
}
impl Port {
    pub fn create(link: Cap<LinkEvent>, tx: Sender<[u8; 44]>, rx: Receiver<[u8; 44]>) -> Cap<PortEvent> {
        let port = actor::create(
            Port {
                port: None,
                link,
                tx,
                rx,
                data: RefCell::new(None),
            }
        );
        port.send(PortEvent::Init(port.clone()));
        port
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
            }
        }
    }

    pub fn outbound(&self) -> Result<[u8; 44], Error> {
        let mut ref_data = self.data.borrow_mut();
        let opt_data = ref_data.deref_mut();
        match opt_data {
            Some(data) => Ok(data.clone()),
            None => {
                match self.rx.try_recv() {
                    Ok(data) => {
                        let _ = opt_data.insert(data.clone()); // data is Copy, clone() would be implicit
                        Ok(data)
                    }
                    Err(_) => Err("Port recv failed"), // FIXME: distinguish "empty" from "error"
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
        match event {
            PortEvent::Init(port) => {
                self.port = Some(port); // FIXME: should fail if not None (set once only)
            }
            PortEvent::Inbound(payload) => {
                self.inbound(payload).expect("Port::inbound failed");
                let cust = self.port.clone().expect("Port::port not set!");
                self.link.send(LinkEvent::Read(cust));  // Ack Write
            },
            PortEvent::AckWrite() => {
                println!("Port::AckWrite");
                // FIXME: what do we need to do here?
            }
        }
    }
}
