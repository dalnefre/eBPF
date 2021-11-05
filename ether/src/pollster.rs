use crate::actor::{self, Actor, Cap};
use crate::hub::HubEvent;
use crate::link::LinkState;
use crate::port::{PortEvent, PortState};

use std::collections::HashMap;
//use pretty_hex::pretty_hex;

#[derive(Debug, Clone)]
pub enum PollsterEvent {
    Init(Cap<PollsterEvent>),
    Start(Cap<HubEvent>),
    Poll(Cap<HubEvent>),
    Stop(Cap<HubEvent>),
    PortStatus(Cap<PortEvent>, PortState),
}
impl PollsterEvent {
    pub fn new_init(pollster: &Cap<PollsterEvent>) -> PollsterEvent {
        PollsterEvent::Init(pollster.clone())
    }
    pub fn new_start(hub: &Cap<HubEvent>) -> PollsterEvent {
        PollsterEvent::Start(hub.clone())
    }
    pub fn new_poll(hub: &Cap<HubEvent>) -> PollsterEvent {
        PollsterEvent::Poll(hub.clone())
    }
    pub fn new_stop(hub: &Cap<HubEvent>) -> PollsterEvent {
        PollsterEvent::Stop(hub.clone())
    }
    pub fn new_port_status(port: &Cap<PortEvent>, state: &PortState) -> PollsterEvent {
        PollsterEvent::PortStatus(port.clone(), state.clone())
    }
}

// polling information for each port
struct PortPoll {
    idle: usize, // idle state counter
}

// link failure detector based on polling for port activity
pub struct Pollster {
    myself: Option<Cap<PollsterEvent>>,
    hub: Option<Cap<HubEvent>>,
    ports: Vec<Cap<PortEvent>>,
    pending: usize,
    poll: HashMap<Cap<PortEvent>, PortPoll>,
}
impl Pollster {
    pub fn create(
        ports: &Vec<Cap<PortEvent>>,
    ) -> Cap<PollsterEvent> {
        let mut poll: HashMap<Cap<PortEvent>, PortPoll> = HashMap::new();
        for port in ports {
            poll.insert(
                port.clone(),
                PortPoll {
                    idle: 0
                }
            );
        }
        let pollster = actor::create(Pollster {
            myself: None,
            hub: None,
            ports: ports.clone(),
            pending: 0,
            poll,
        });
        pollster.send(PollsterEvent::new_init(&pollster));
        pollster
    }
}
impl Actor for Pollster {
    type Event = PollsterEvent;

    fn on_event(&mut self, event: Self::Event) {
        match &event {
            PollsterEvent::Init(myself) => match &self.myself {
                None => {
                    self.myself = Some(myself.clone()); // set self-reference
                },
                Some(_) => panic!("Pollster::myself already set"),
            },
            PollsterEvent::Start(hub) => {
                println!("Pollster::Start");
                if let Some(myself) = &self.myself {
                    if self.hub.is_none() {
                        self.hub = Some(hub.clone());
                        self.pending = self.ports.len();
                        for port in &self.ports {
                            port.send(PortEvent::new_start(&myself));
                        }
                    } else {
                        println!("pollster already polling...");
                    }
                }
            }
            PollsterEvent::Poll(hub) => {
                println!("Pollster::Poll");
                if let Some(myself) = &self.myself {
                    if self.hub.is_none() {
                        self.hub = Some(hub.clone());
                        self.pending = self.ports.len();
                        for port in &self.ports {
                            port.send(PortEvent::new_poll(&myself));
                        }
                    } else {
                        println!("pollster already polling...");
                    }
                }
            }
            PollsterEvent::Stop(hub) => {
                println!("Pollster::Stop");
                if let Some(myself) = &self.myself {
                    if self.hub.is_none() {
                        self.hub = Some(hub.clone());
                        self.pending = self.ports.len();
                        for port in &self.ports {
                            port.send(PortEvent::new_stop(&myself));
                        }
                    } else {
                        println!("pollster already polling...");
                    }
                }
            }
            PollsterEvent::PortStatus(port, state) => {
                let n = self.port_to_port_num(&port);
                println!(
                    "Pollster::LinkStatus[{}] link_state={:?}, ait_balance={}",
                    n, state.link_state, state.ait_balance
                );
                if let Some(poll) = self.poll.get_mut(port) {
                    if state.link_state == LinkState::Live {
                        poll.idle =  0;
                    } else {
                        poll.idle += 1;
                    }
                    println!("Pollster::poll[{}].idle = {}", n, poll.idle);
                }
                assert!(self.pending > 0);
                self.pending -= 1;
                if self.pending == 0 {
                    self.hub = None;
                }
            }
        }
    }
}
impl Pollster {
    fn port_to_port_num(&mut self, port: &Cap<PortEvent>) -> usize {
        self.ports
            .iter()
            .enumerate()
            .find(|(_port_num, port_cap)| *port_cap == port)
            .expect("unknown Port")
            .0
    }
}