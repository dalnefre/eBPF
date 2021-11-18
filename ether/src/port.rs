use crate::actor::{self, Actor, Cap};
use crate::frame::Payload;
use crate::hub::HubEvent;
use crate::link::{LinkEvent, LinkState};
use crate::pollster::PollsterEvent;

#[derive(Debug, Clone)]
pub enum PortEvent {
    Init(Cap<PortEvent>),
    Start(Cap<HubEvent>),
    Stop(Cap<HubEvent>),
    Failover(FailoverInfo),
    Poll(Cap<PollsterEvent>),
    PollReply(PortState),
    LinkToPortWrite(Payload),               // inbound
    LinkToPortRead,                         // outbound-ready
    HubToPortWrite(Cap<HubEvent>, Payload), // outbound
    HubToPortRead(Cap<HubEvent>),           // inbound-credit
}
impl PortEvent {
    pub fn new_init(port: &Cap<PortEvent>) -> PortEvent {
        PortEvent::Init(port.clone())
    }
    pub fn new_start(cust: &Cap<HubEvent>) -> PortEvent {
        PortEvent::Start(cust.clone())
    }
    pub fn new_stop(cust: &Cap<HubEvent>) -> PortEvent {
        PortEvent::Stop(cust.clone())
    }
    pub fn new_failover(info: &FailoverInfo) -> PortEvent {
        PortEvent::Failover(info.clone())
    }
    pub fn new_poll(cust: &Cap<PollsterEvent>) -> PortEvent {
        PortEvent::Poll(cust.clone())
    }
    pub fn new_poll_reply(state: &PortState) -> PortEvent {
        PortEvent::PollReply(state.clone())
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
    pub sequence: u16,
}
impl PortState {
    pub fn new(link_state: &LinkState, ait_balance: isize, sequence: u16) -> PortState {
        PortState {
            link_state: link_state.clone(),
            ait_balance,
            sequence,
        }
    }
}

#[derive(Debug, Clone)]
pub struct FailoverInfo {
    pub port_state: PortState,
    //pub reader: Option<Cap<PortEvent>>,
    pub inbound: Option<Payload>,
    //pub writer: Option<Cap<PortEvent>>,
    pub outbound: Option<Payload>,
}
impl FailoverInfo {
    pub fn new(
        port_state: &PortState,
        inbound: &Option<Payload>,
        outbound: &Option<Payload>,
    ) -> FailoverInfo {
        FailoverInfo {
            port_state: port_state.clone(),
            inbound: inbound.clone(),
            outbound: outbound.clone(),
        }
    }
}

pub struct Port {
    myself: Option<Cap<PortEvent>>,
    link: Cap<LinkEvent>,
    hub: Option<Cap<HubEvent>>,
    pollster: Option<Cap<PollsterEvent>>,
    reader: Option<Cap<HubEvent>>,
    writer: Option<Cap<HubEvent>>,
}
impl Port {
    pub fn create(link: &Cap<LinkEvent>) -> Cap<PortEvent> {
        let port = actor::create(Port {
            myself: None,
            link: link.clone(),
            hub: None,
            pollster: None,
            reader: None,
            writer: None,
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
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::Start cust={}", myself, cust);
                match &self.hub {
                    None => {
                        self.hub = Some(cust.clone());
                        self.link.send(LinkEvent::new_start(&myself));
                        myself.send(PortEvent::new_hub_to_port_read(&cust)); // Port ready to receive
                    }
                    Some(_cust) => panic!("Only one start/stop allowed"),
                }
            }
            PortEvent::Stop(cust) => {
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::Stop cust={}", myself, cust);
                match &self.hub {
                    None => {
                        self.hub = Some(cust.clone());
                        self.link.send(LinkEvent::new_stop(&myself));
                    }
                    Some(_cust) => panic!("Only one start/stop allowed"),
                }
            }
            PortEvent::Failover(info) => {
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::Failover {:?}", myself, info);
                match &self.hub {
                    Some(cust) => {
                        cust.send(HubEvent::new_failover(&myself, &info));
                        self.hub = None;
                        if info.port_state.link_state == LinkState::Stop {
                            // clear pending reader/writer on Stop
                            self.reader = None;
                            self.writer = None;
                        }
                    }
                    None => {
                        println!("Port::Failover no hub registered");
                    }
                }
            }
            PortEvent::Poll(cust) => {
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::Poll cust={}", myself, cust);
                match &self.pollster {
                    None => {
                        self.pollster = Some(cust.clone());
                        self.link.send(LinkEvent::new_poll(&myself));
                    }
                    Some(_cust) => panic!("Only one poll allowed"),
                }
            }
            PortEvent::PollReply(state) => {
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::PollReply {:?}", myself, state);
                match &self.pollster {
                    Some(cust) => {
                        cust.send(PollsterEvent::new_port_status(&myself, &state));
                        self.pollster = None;
                    }
                    None => {
                        println!("Port::PollReply no Pollster registered");
                    },
                }
            }
            PortEvent::LinkToPortWrite(payload) => {
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::LinkToPortWrite link={}", myself, self.link);
                match &self.reader {
                    Some(hub) => {
                        hub.send(HubEvent::new_port_to_hub_write(&myself, &payload));
                        self.reader = None;
                    }
                    None => panic!("Reader (hub) not ready"),
                }
            }
            PortEvent::LinkToPortRead => {
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::LinkToPortRead link={}", myself, self.link);
                match &self.writer {
                    Some(hub) => {
                        hub.send(HubEvent::new_port_to_hub_read(&myself));
                        self.writer = None;
                    }
                    None => panic!("Writer (hub) not ready"),
                }
            }
            PortEvent::HubToPortWrite(cust, payload) => {
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::HubToPortWrite hub={}", myself, cust);
                match &self.writer {
                    None => {
                        self.writer = Some(cust.clone());
                        self.link.send(LinkEvent::new_write(&myself, &payload));
                    }
                    Some(_cust) => panic!("Only one Hub-to-Port writer allowed"),
                }
            }
            PortEvent::HubToPortRead(cust) => {
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::HubToPortRead hub={}", myself, cust);
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
