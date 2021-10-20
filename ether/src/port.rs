use crate::actor::{self, Actor, Cap};
use crate::frame::Payload;
use crate::hub::HubEvent;
use crate::link::{LinkEvent, LinkState};

//use pretty_hex::pretty_hex;

#[derive(Debug, Clone)]
pub enum PortEvent {
    Init(Cap<PortEvent>),
    LinkStatus(LinkState, isize),
    LinkToPortWrite(Payload),
    LinkToPortRead,
    HubToPortWrite(Cap<HubEvent>, Payload),
    HubToPortRead(Cap<HubEvent>),
}
impl PortEvent {
    pub fn new_init(port: &Cap<PortEvent>) -> PortEvent {
        PortEvent::Init(port.clone())
    }
    pub fn new_link_status(state: &LinkState, balance: &isize) -> PortEvent {
        PortEvent::LinkStatus(state.clone(), balance.clone())
    }
    pub fn new_link_to_port_write(payload: &Payload) -> PortEvent {
        PortEvent::LinkToPortWrite(payload.clone())
    }
    pub fn new_link_to_port_read() -> PortEvent {
        PortEvent::LinkToPortRead
    }
    pub fn new_hub_to_port_write(hub: &Cap<HubEvent>, payload: &Payload) -> PortEvent {
        PortEvent::HubToPortWrite(hub.clone(), payload.clone())
    }
    pub fn new_hub_to_port_read(hub: &Cap<HubEvent>) -> PortEvent {
        PortEvent::HubToPortRead(hub.clone())
    }
}

#[derive(Debug, Clone)]
pub struct PortState {
    pub link_state: LinkState,
    pub ait_balance: isize,
}

pub struct Port {
    myself: Option<Cap<PortEvent>>,
    link: Cap<LinkEvent>,
    reader: Option<Cap<HubEvent>>,
    writer: Option<Cap<HubEvent>>,
    outbound: Option<Payload>,
}
impl Port {
    pub fn create(
        link: &Cap<LinkEvent>,
    ) -> Cap<PortEvent> {
        let port = actor::create(Port {
            myself: None,
            link: link.clone(),
            reader: None,
            writer: None,
            outbound: None,
        });
        port.send(PortEvent::new_init(&port));
        port
    }
}
impl Actor for Port {
    type Event = PortEvent;

    fn on_event(&mut self, event: Self::Event) {
        match &event {
            PortEvent::Init(myself) => match &self.myself {
                None => self.myself = Some(myself.clone()),
                Some(_) => panic!("Port::myself already set"),
            },
            PortEvent::LinkStatus(state, balance) => {
                println!("Port::LinkStatus state={:?}, balance={}", state, balance);
            }
            PortEvent::LinkToPortWrite(payload) => {
                println!("Port::LinkToPortWrite");
                if let Some(myself) = &self.myself {
                    match &self.reader {
                        Some(hub) => {
                            hub.send(HubEvent::new_port_to_hub_write(&myself, &payload));
                            self.link.send(LinkEvent::new_read(myself)); // Ack Write
                        },
                        None => panic!("Reader (hub) not ready"),
                    }
                }
            }
            PortEvent::LinkToPortRead => {
                println!("Port::LinkToPortRead");
                if let Some(myself) = &self.myself {
                    match &self.writer {
                        Some(hub) => {
                            hub.send(HubEvent::new_port_to_hub_read(&myself));
                            if let Some(payload) = &self.outbound {
                                self.link.send(LinkEvent::new_write(&myself, &payload));
                                self.outbound = None;
                                self.writer = None;    
                            }
                        },
                        None => panic!("Writer (hub) not ready"),
                    }
                }
            }
            PortEvent::HubToPortWrite(cust, payload) => {
                println!("Port::HubToPortWrite cust={:?} payload={:?}", cust, payload);
                match &self.writer {
                    None => {
                        self.outbound = Some(payload.clone());
                        self.writer = Some(cust.clone());
                    }
                    Some(_cust) => panic!("Only one Hub-to-Port writer allowed"),
                }
            },
            PortEvent::HubToPortRead(cust) => {
                println!("Port::HubToPortRead cust={:?}", cust);
                match &self.reader {
                    None => {
                        self.reader = Some(cust.clone());
                    }
                    Some(_cust) => panic!("Only one Hub-to-Port reader allowed"),
                }
            },
        }
    }
}
