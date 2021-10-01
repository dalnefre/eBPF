use crate::actor::{Actor, Cap};
use crate::frame::Frame;
use crate::link::LinkEvent;
use crossbeam::crossbeam_channel::{Receiver, Sender};
//use pretty_hex::pretty_hex;

#[derive(Debug, Clone)]
pub enum WireEvent {
    Poll(Cap<LinkEvent>, Cap<WireEvent>),
    Frame([u8; 60]),
}

pub struct Wire {
    tx: Sender<[u8; 60]>,
    rx: Receiver<[u8; 60]>,
}
impl Wire {
    pub fn new(tx: Sender<[u8; 60]>, rx: Receiver<[u8; 60]>) -> Wire {
        Wire { tx, rx }
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
            WireEvent::Poll(link, wire) => {
                // FIXME: this polling strategy is only needed
                // until we can inject events directly
                match self.rx.try_recv() {
                    Ok(data) => {
                        //println!("Wire::inbound {}", pretty_hex(&data));
                        let frame = Frame::new(&data[..]).expect("bad frame size");
                        link.send(LinkEvent::Frame(frame));
                    }
                    _ => {
                        // FIXME: we should actually check for errors
                        // _OTHER THAN_ not data available
                    }
                }
                //wire.send(event); // keep polling
                //wire.send(event.clone()); // keep polling
                wire.send(WireEvent::Poll(link.clone(), wire.clone())); // keep polling
            }
        }
    }
}
