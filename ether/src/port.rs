use crate::actor::{self, Actor, Cap};
use crate::frame::Payload;
use crate::hub::HubEvent;
use crate::link::{LinkEvent, LinkState};
use crate::pollster::PollsterEvent;

#[derive(Debug, Clone)]
pub enum PortEvent {
    Init(Cap<PortEvent>),
    Start(Cap<PollsterEvent>),
    Poll(Cap<PollsterEvent>),
    Stop(Cap<PollsterEvent>),
    LinkStatus(LinkState, isize),
    Failover(FailoverInfo),
    LinkToPortWrite(Payload),               // inbound
    LinkToPortRead,                         // outbound-ready
    HubToPortWrite(Cap<HubEvent>, Payload), // outbound
    HubToPortRead(Cap<HubEvent>),           // inbound-credit
}
impl PortEvent {
    pub fn new_init(port: &Cap<PortEvent>) -> PortEvent {
        PortEvent::Init(port.clone())
    }
    pub fn new_start(cust: &Cap<PollsterEvent>) -> PortEvent {
        PortEvent::Start(cust.clone())
    }
    pub fn new_poll(cust: &Cap<PollsterEvent>) -> PortEvent {
        PortEvent::Poll(cust.clone())
    }
    pub fn new_stop(cust: &Cap<PollsterEvent>) -> PortEvent {
        PortEvent::Stop(cust.clone())
    }
    pub fn new_link_status(state: &LinkState, balance: &isize) -> PortEvent {
        PortEvent::LinkStatus(state.clone(), balance.clone())
    }
    pub fn new_failover(info: &FailoverInfo) -> PortEvent {
        PortEvent::Failover(info.clone())
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
impl PortState {
    pub fn new(link_state: &LinkState, ait_balance: isize) -> PortState {
        PortState {
            link_state: link_state.clone(),
            ait_balance
        }
    }
}

#[derive(Debug, Clone)]
pub struct FailoverInfo {
    pub link_state: LinkState,
    pub ait_balance: isize,
    //pub reader: Option<Cap<PortEvent>>,
    pub inbound: Option<Payload>,
    //pub writer: Option<Cap<PortEvent>>,
    pub outbound: Option<Payload>,
}
impl FailoverInfo {
    pub fn new(
        link_state: &LinkState,
        ait_balance: isize,
        inbound: &Option<Payload>,
        outbound: &Option<Payload>,
    ) -> FailoverInfo {
        FailoverInfo {
            link_state: link_state.clone(),
            ait_balance,
            inbound: inbound.clone(),
            outbound: outbound.clone(),
        }
    }
}

pub struct Port {
    myself: Option<Cap<PortEvent>>,
    link: Cap<LinkEvent>,
    reader: Option<Cap<HubEvent>>,
    writer: Option<Cap<HubEvent>>,
    pollster: Option<Cap<PollsterEvent>>,
}
impl Port {
    pub fn create(link: &Cap<LinkEvent>) -> Cap<PortEvent> {
        let port = actor::create(Port {
            myself: None,
            link: link.clone(),
            reader: None,
            writer: None,
            pollster: None,
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
                None => {
                    self.myself = Some(myself.clone());
                }
                Some(_) => panic!("Port::myself already set"),
            },
            PortEvent::Start(cust) => {
                if let Some(myself) = &self.myself {
                    println!("Port{}::Start", myself);
                    match &self.pollster {
                        None => {
                            self.pollster = Some(cust.clone());
                            self.link.send(LinkEvent::new_start(&myself));
                        }
                        Some(_cust) => panic!("Only one Pollster allowed"),
                    }
                }
            }
            PortEvent::Poll(cust) => {
                if let Some(myself) = &self.myself {
                    println!("Port{}::Poll", myself);
                    match &self.pollster {
                        None => {
                            self.pollster = Some(cust.clone());
                            self.link.send(LinkEvent::new_poll(&myself));
                        }
                        Some(_cust) => panic!("Only one Pollster allowed"),
                    }
                }
            }
            PortEvent::Stop(cust) => {
                if let Some(myself) = &self.myself {
                    println!("Port{}::Stop", myself);
                    match &self.pollster {
                        None => {
                            self.pollster = Some(cust.clone());
                            self.link.send(LinkEvent::new_stop(&myself));
                        }
                        Some(_cust) => panic!("Only one Pollster allowed"),
                    }
                }
            }
            PortEvent::LinkStatus(state, balance) => {
                if let Some(myself) = &self.myself {
                    println!("Port{}::LinkStatus state={:?}, balance={}", myself, state, balance);
                    match &self.pollster {
                        Some(cust) => {
                            let state = PortState::new(&state, *balance);
                            cust.send(PollsterEvent::new_port_status(&myself, &state));
                            self.pollster = None;
                        }
                        None => {
                            println!("Port::LinkStatus no Pollster registered");
                        },
                    }
                }
            }
            PortEvent::Failover(info) => {
                if let Some(myself) = &self.myself {
                    println!("Port{}::Failover info={:?}", myself, info);
                }
            }
            PortEvent::LinkToPortWrite(payload) => {
                if let Some(myself) = &self.myself {
                    println!("Port{}::LinkToPortWrite", myself);
                    match &self.reader {
                        Some(hub) => {
                            hub.send(HubEvent::new_port_to_hub_write(&myself, &payload));
                            self.reader = None;
                        }
                        None => panic!("Reader (hub) not ready"),
                    }
                }
            }
            PortEvent::LinkToPortRead => {
                if let Some(myself) = &self.myself {
                    println!("Port{}::LinkToPortRead", myself);
                    match &self.writer {
                        Some(hub) => {
                            hub.send(HubEvent::new_port_to_hub_read(&myself));
                            self.writer = None;
                        }
                        None => panic!("Writer (hub) not ready"),
                    }
                }
            }
            PortEvent::HubToPortWrite(cust, payload) => {
                if let Some(myself) = &self.myself {
                    println!("Port{}::HubToPortWrite cust={}", myself, cust);
                    match &self.writer {
                        None => {
                            self.writer = Some(cust.clone());
                            self.link.send(LinkEvent::new_write(&myself, &payload));
                        }
                        Some(_cust) => panic!("Only one Hub-to-Port writer allowed"),
                    }
                }
            }
            PortEvent::HubToPortRead(cust) => {
                if let Some(myself) = &self.myself {
                    println!("Port{}::HubToPortRead cust={}", myself, cust);
                    match &self.reader {
                        None => {
                            self.reader = Some(cust.clone());
                            self.link.send(LinkEvent::new_read(&myself));
                        }
                        Some(_cust) => panic!("Only one Hub-to-Port reader allowed"),
                    }
                }
            }
        }
    }
}
