use crate::actor::{Actor, Cap};
//use crate::reactor::{Actor, Behavior, Effect, Error, Event, Message};
//extern crate alloc;
//use alloc::boxed::Box;
//use alloc::rc::Rc;
use crossbeam::crossbeam_channel::{Receiver, Sender};
//use pretty_hex::pretty_hex;
use crate::link::LinkEvent;

#[derive(Debug, Clone)]
pub enum WireEvent {
    Poll(Cap<WireEvent>),
    Frame([u8; 60]),
}

pub struct Wire {
    link: Cap<LinkEvent>,
    tx: Sender<[u8; 60]>,
    rx: Receiver<[u8; 60]>,
}
impl Wire {
    pub fn new(
        link: Cap<LinkEvent>,
        tx: Sender<[u8; 60]>,
        rx: Receiver<[u8; 60]>,
    ) -> Wire {
        Wire { link, tx, rx }
    }
}
impl Actor for Wire {
    type Event = WireEvent;

    fn on_event(&mut self, event: Self::Event) {
        match event {
            WireEvent::Frame(data) => {
                //println!("Wire::outbound {}", pretty_hex(&data));
                self.tx.send(data).expect("Wire::send failed");
            }
            WireEvent::Poll(myself) => {
                // FIXME: this polling strategy is only needed
                // until we can inject events directly
                match self.rx.try_recv() {
                    Ok(data) => {
                        //println!("Wire::inbound {}", pretty_hex(&data));
                        self.link.send(LinkEvent::Frame(data));
                    }
                    _ => {
                        // FIXME: we should actually check for errors
                        // _OTHER THAN_ not data available
                    }
                }
                //myself.send(event); // keep polling
                //myself.send(event.clone()); // keep polling
                myself.send(WireEvent::Poll(myself.clone())); // keep polling
            }
        }
    }
}
