//use std::io::{Error, ErrorKind};

use crate::reactor::{Actor, Behavior, Effect, Error, Event, Message};
extern crate alloc;
//use alloc::boxed::Box;
use alloc::rc::Rc;
use crossbeam::crossbeam_channel::{Receiver, Sender};
//use pretty_hex::pretty_hex;

pub struct WireBeh {
    link: Rc<Actor>,
    tx: Sender<[u8; 60]>,
    rx: Receiver<[u8; 60]>,
}

impl WireBeh {
    pub fn new(
        link: &Rc<Actor>,
        tx: Sender<[u8; 60]>,
        rx: Receiver<[u8; 60]>,
    ) -> Box<dyn Behavior> {
        Box::new(WireBeh {
            link: Rc::clone(&link),
            tx,
            rx,
        })
    }
}

impl Behavior for WireBeh {
    fn react(&self, event: Event) -> Result<Effect, Error> {
        let mut effect = Effect::new();
        match event.message {
            Message::Frame(data) => {
                //println!("Wire::outbound {}", pretty_hex(&data));
                match self.tx.send(data) {
                    Ok(_) => Ok(effect),
                    _ => Err("send failed"),
                }
            }
            Message::Empty => {
                // FIXME: this polling strategy is only needed
                // until we can inject events directly into ReActor
                match self.rx.try_recv() {
                    Ok(data) => {
                        //println!("Wire::inbound {}", pretty_hex(&data));
                        effect.send(&self.link, Message::Frame(data));
                        effect.send(&event.target, Message::Empty); // keep polling
                        Ok(effect)
                    }
                    _ => {
                        // FIXME: we should actually check for errors
                        // _OTHER THAN_ not data available
                        effect.send(&event.target, Message::Empty); // keep polling
                        Ok(effect)
                    }
                }
            }
            _ => Err("unknown message"),
        }
    }
}
