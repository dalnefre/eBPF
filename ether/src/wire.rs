use crate::actor::{self, Actor, Cap};
/*
use crate::frame::{self, Frame};
*/
use crate::frame::Frame;
use crate::link::LinkEvent;

use crossbeam::crossbeam_channel::{Receiver, Sender};
use pretty_hex::pretty_hex;

#[derive(Debug, Clone)]
pub enum WireEvent {
    Listen(Cap<LinkEvent>),
    Frame(Frame),
}
impl WireEvent {
    pub fn new_listen(link: &Cap<LinkEvent>) -> WireEvent {
        WireEvent::Listen(link.clone())
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
            WireEvent::Listen(link) => {
                let rx = self.rx.clone(); // local copy moved into closure
                let link = link.clone(); // local copy moved into closure
                std::thread::spawn(move || {
                    while let Ok(frame) = rx.recv() {
                        //println!("Wire::inbound {}", pretty_hex(&frame.data));
                        link.send(LinkEvent::new_frame(&frame));
                    }
                });
            }
        }
    }
}

pub struct FaultyWire {
    tx: Sender<Frame>,
    rx: Receiver<Frame>,
    /*
    filter: [u8; frame::PAYLOAD_SIZE],
    */
    filter: Option<u16>,
}
impl FaultyWire {
    /*
    pub fn create(tx: &Sender<Frame>, rx: &Receiver<Frame>, filter: &str) -> Cap<WireEvent> {
    */
    pub fn create(tx: &Sender<Frame>, rx: &Receiver<Frame>, filter: u16) -> Cap<WireEvent> {
        /*
        assert!(filter.len() <= frame::PAYLOAD_SIZE);
        */
        let mut state = FaultyWire {
            tx: tx.clone(),
            rx: rx.clone(),
            /*
            filter: [0_u8; frame::PAYLOAD_SIZE],
            */
            filter: None,
        };
        /*
        state.filter[..filter.len()].copy_from_slice(&filter.as_bytes());
        */
        state.filter = Some(filter);
        actor::create(state)
    }
}
impl Actor for FaultyWire {
    type Event = WireEvent;

    fn on_event(&mut self, event: Self::Event) {
        match &event {
            WireEvent::Frame(frame) => {
                //println!("Wire::outbound {}", pretty_hex(&frame.data));
                /*
                if self.filter == frame.get_payload().data {
                */
                if let Some(filter) = self.filter {
                    if filter == frame.get_sequence() {
                        println!(
                            "Wire::outbound #{} ({},{}) FILTERED {}",
                            frame.get_sequence(),
                            frame.get_i_state(),
                            frame.get_u_state(),
                            pretty_hex(&frame.data)
                        );
                        /*
                        self.filter = [0_u8; frame::PAYLOAD_SIZE]; // reset filter
                        */
                        self.filter = None;
                        return;
                    }
                }
                // if not filtered, then send the frame...
                self.tx.send(frame.clone()).expect("Wire::send failed");
            }
            WireEvent::Listen(link) => {
                let rx = self.rx.clone(); // local copy moved into closure
                let link = link.clone(); // local copy moved into closure
                std::thread::spawn(move || {
                    while let Ok(frame) = rx.recv() {
                        //println!("Wire::inbound {}", pretty_hex(&frame.data));
                        link.send(LinkEvent::new_frame(&frame));
                    }
                });
            }
        }
    }
}
