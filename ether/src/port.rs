use crate::actor::{self, Actor, Cap};
use crate::frame::Payload;
use crate::hub::HubEvent;
use crate::link::{LinkEvent, LinkState};
use crate::pollster::PollsterEvent;

#[derive(Debug, Clone)]
pub enum PortEvent {
    Init(Cap<PortEvent>),                   // self-init on creation
    Start(Cap<HubEvent>),                   // start request
    Stop(Cap<HubEvent>),                    // stop request
    Status(PortStatus),                     // status report
    Poll(Cap<PollsterEvent>),               // poll for activity
    Activity(PortActivity),                 // activity report
    LinkToPortWrite(Payload),               // inbound token
    LinkToPortRead,                         // outbound-ready
    HubToPortWrite(Cap<HubEvent>, Payload), // outbound token
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
    pub fn new_status(status: &PortStatus) -> PortEvent {
        PortEvent::Status(status.clone())
    }
    pub fn new_poll(cust: &Cap<PollsterEvent>) -> PortEvent {
        PortEvent::Poll(cust.clone())
    }
    pub fn new_activity(activity: &PortActivity) -> PortEvent {
        PortEvent::Activity(activity.clone())
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
pub struct PortActivity {
    pub link_state: LinkState,
    pub ait_balance: isize,
    pub sequence: u16,
}
impl PortActivity {
    pub fn new(link_state: &LinkState, ait_balance: isize, sequence: u16) -> PortActivity {
        PortActivity {
            link_state: link_state.clone(),
            ait_balance,
            sequence,
        }
    }
}

#[derive(Debug, Clone)]
pub struct PortStatus {
    pub activity: PortActivity,
    pub inbound: Option<Payload>,
    pub outbound: Option<Payload>,
}
impl PortStatus {
    pub fn new(
        activity: &PortActivity,
        inbound: &Option<Payload>,
        outbound: &Option<Payload>,
    ) -> PortStatus {
        PortStatus {
            activity: activity.clone(),
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
                        // Port ready to receive
                        if self.reader.is_none() {
                            self.reader = Some(cust.clone());
                            self.link.send(LinkEvent::new_read(&myself));
                        }
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
            PortEvent::Status(status) => {
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::Status {:?}", myself, info);
                match &self.hub {
                    Some(hub) => {
                        hub.send(HubEvent::new_status(&myself, &status));
                        self.hub = None;
                        if status.activity.link_state == LinkState::Stop {
                            // on surplus, release inbound token
                            if status.activity.ait_balance > 0 {
                                if let Some(payload) = &status.inbound {
                                    if let Some(cust) = &self.reader {
                                        cust.send(HubEvent::new_port_to_hub_write(&myself, &payload));
                                    } else {
                                        println!("Port::Status no reader for inbound release");
                                    }
                                }
                            }
                            // clear pending reader/writer on Stop
                            self.reader = None;
                            self.writer = None;
                        }
                    }
                    None => {
                        println!("Port::Status no hub registered");
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
                    Some(_cust) => panic!("Only one Poll allowed"),
                }
            }
            PortEvent::Activity(activity) => {
                let myself = self.myself.as_ref().expect("Port::myself not set!");
                //println!("Port{}::Activity {:?}", myself, state);
                match &self.pollster {
                    Some(cust) => {
                        cust.send(PollsterEvent::new_port_status(&myself, &activity));
                        self.pollster = None;
                    }
                    None => {
                        println!("Port::Activity no Pollster registered");
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
                    // FIXME: we should not get overlapping reads, but we do on re-start...
                    //Some(_cust) => panic!("Only one Hub-to-Port reader allowed"),
                    Some(_cust) => {
                        println!("Port{}::HubToPortRead hub={} CONFLICT? reader={}", myself, cust, _cust);
                    },
                }
            }
        }
    }
}
