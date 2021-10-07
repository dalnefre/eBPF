use crate::actor::{self, Actor, Cap};
use crate::frame::Payload;
use crate::link::LinkEvent;

//use pretty_hex::pretty_hex;
//use crossbeam::crossbeam_channel::unbounded as channel;
use crossbeam::crossbeam_channel::{Receiver, Sender};

#[derive(Debug, Clone)]
pub enum PortEvent {
    Init(Cap<PortEvent>),
    LinkToPortWrite(Payload),
    LinkToPortRead,
}
impl PortEvent {
    pub fn new_init(port: &Cap<PortEvent>) -> PortEvent {
        PortEvent::Init(port.clone())
    }
    pub fn new_link_to_port_write(payload: &Payload) -> PortEvent {
        PortEvent::LinkToPortWrite(payload.clone())
    }
    pub fn new_link_to_port_read() -> PortEvent {
        PortEvent::LinkToPortRead
    }
}

// Simulated Port for driving AIT link protocol tests
pub struct Port {
    myself: Option<Cap<PortEvent>>,
    link: Cap<LinkEvent>,
    tx: Sender<Payload>,
    rx: Receiver<Payload>,
}
impl Port {
    pub fn create(
        link: &Cap<LinkEvent>,
        tx: &Sender<Payload>,
        rx: &Receiver<Payload>,
    ) -> Cap<PortEvent> {
        let port = actor::create(Port {
            myself: None,
            link: link.clone(),
            tx: tx.clone(),
            rx: rx.clone(),
        });
        port.send(PortEvent::new_init(&port));
        port
    }
}
impl Actor for Port {
    type Event = PortEvent;

    fn on_event(&mut self, event: Self::Event) {
        match &event {
            PortEvent::Init(myself) => {
                match &self.myself {
                    None => {
                        self.myself = Some(myself.clone())
                    },
                    Some(_) => panic!("Port::port already set"),
                }
            }
            PortEvent::LinkToPortWrite(payload) => {
                //println!("Port::LinkToPortWrite");
                if let Some(myself) = &self.myself {
                    if self.tx.is_empty() { // if all prior data has been consumed, we are ready for more
                        self.tx.send(payload.clone()).expect("Port::inbound failed!");
                        self.link.send(LinkEvent::new_read(myself)); // Ack Write
                    } else {
                        // try again...
                        myself.send(event);
                    }
                }
            }
            PortEvent::LinkToPortRead => {
                //println!("Port::LinkToPortRead");
                if let Some(myself) = &self.myself {
                    match self.rx.try_recv() {
                        Ok(payload) => {
                            // send next payload
                            self.link.send(LinkEvent::new_write(myself, &payload));
                        }
                        Err(_) => {
                            // try again...
                            myself.send(event);
                        }
                    }
                }
            }
        }
    }
}
