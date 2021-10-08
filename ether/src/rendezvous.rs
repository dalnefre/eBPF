/*
 * Synchronous Rendezvous actor
 */
use crate::actor::{self, Actor, Cap};
use crate::frame::Payload;

#[derive(Debug, Clone)]
pub enum RendezvousEvent {
    Init(Cap<RendezvousEvent>),
    Read(Cap<RendezvousEvent>),
    Write(Cap<RendezvousEvent>, Payload),
}
impl RendezvousEvent {
    pub fn new_init(myself: &Cap<RendezvousEvent>) -> RendezvousEvent {
        RendezvousEvent::Init(myself.clone())
    }
    pub fn new_read(reader: &Cap<RendezvousEvent>) -> RendezvousEvent {
        RendezvousEvent::Read(reader.clone())
    }
    pub fn new_write(writer: &Cap<RendezvousEvent>, payload: &Payload) -> RendezvousEvent {
        RendezvousEvent::Write(writer.clone(), payload.clone())
    }
}

pub struct Rendezvous {
    myself: Option<Cap<RendezvousEvent>>,
    reader: Option<Cap<RendezvousEvent>>,
    writer: Option<Cap<RendezvousEvent>>,
    payload: Option<Payload>,
}
impl Rendezvous {
    pub fn create() -> Cap<RendezvousEvent> {
        let myself = actor::create(Rendezvous {
            myself: None,
            reader: None,
            writer: None,
            payload: None,
        });
        myself.send(RendezvousEvent::new_init(&myself));
        myself
    }
}

impl Actor for Rendezvous {
    type Event = RendezvousEvent;

    fn on_event(&mut self, event: Self::Event) {
        match &event {
            RendezvousEvent::Init(myself) => match &self.myself {
                None => {
                    self.myself = Some(myself.clone());
                }
                Some(_) => panic!("Rendezvous::myself already set"),
            },
            RendezvousEvent::Read(reader) => match &self.writer {
                Some(writer) => {
                    if let Some(myself) = &self.myself {
                        if let Some(payload) = &self.payload {
                            reader.send(RendezvousEvent::new_write(&myself, &payload));
                            writer.send(RendezvousEvent::new_read(&myself));
                            self.payload = None;
                            self.writer = None;
                        }
                    }
                }
                None => match &self.reader {
                    None => {
                        self.reader = Some(reader.clone());
                    }
                    Some(_) => panic!("Rendezvous::reader already set"),
                },
            },
            RendezvousEvent::Write(writer, payload) => match &self.reader {
                Some(reader) => {
                    if let Some(myself) = &self.myself {
                        reader.send(RendezvousEvent::new_write(&myself, &payload));
                        writer.send(RendezvousEvent::new_read(&myself));
                        self.reader = None;
                    }
                }
                None => match &self.writer {
                    None => {
                        self.writer = Some(writer.clone());
                        self.payload = Some(payload.clone());
                    }
                    Some(_) => panic!("Rendezvous::reader already set"),
                },
            },
        }
    }
}
