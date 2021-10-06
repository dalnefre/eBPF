use crate::actor::{self, Actor, Cap};
use crate::frame::Frame;
use crate::link::LinkEvent;
use crossbeam::crossbeam_channel::{Receiver, Sender};
//use pretty_hex::pretty_hex;

#[derive(Debug, Clone)]
pub enum WireEvent {
    Poll(Cap<LinkEvent>, Cap<WireEvent>),
    Frame(Frame),
}
impl WireEvent {
    pub fn new_poll(link: &Cap<LinkEvent>, wire: &Cap<WireEvent>) -> WireEvent {
        WireEvent::Poll(link.clone(), wire.clone())
    }
    pub fn new_frame(frame: &Frame) -> WireEvent {
        WireEvent::Frame(frame.clone())
    }
}

pub struct Wire {
    tx: Sender<Frame>,
    rx: Receiver<Frame>,
}
impl Wire {
    pub fn create(tx: &Sender<Frame>, rx: &Receiver<Frame>) -> Cap<WireEvent> {
        actor::create(Wire {
            tx: tx.clone(),
            rx: rx.clone(),
        })
    }
}
impl Actor for Wire {
    type Event = WireEvent;

    fn on_event(&mut self, event: Self::Event) {
        match &event {
            WireEvent::Frame(frame) => {
                //println!("Wire::outbound {}", pretty_hex(&frame.data));
                self.tx.send(frame.clone()).expect("Wire::send failed");
            }
            WireEvent::Poll(link, wire) => {
                // FIXME: this polling strategy is only needed
                // until we can inject events directly
                match self.rx.try_recv() {
                    Ok(frame) => {
                        //println!("Wire::inbound {}", pretty_hex(&frame.data));
                        link.send(LinkEvent::new_frame(&frame));
                    }
                    _ => {
                        // FIXME: we should actually check for errors
                        // _OTHER THAN_ not data available
                    }
                }
                //wire.send(event); // keep polling
                wire.send(event.clone()); // keep polling
                //wire.send(WireEvent::Poll(link.clone(), wire.clone())); // keep polling
                //wire.send(WireEvent::new_poll(&link, &wire)); // keep polling
            }
        }
    }
}
