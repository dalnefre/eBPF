use crate::actor::{self, Actor, Cap};
use crate::hub::HubEvent;
use crate::link::LinkState;
use crate::port::{PortActivity, PortEvent};

use std::collections::HashMap;
//use pretty_hex::pretty_hex;

#[derive(Debug, Clone)]
pub enum PollsterEvent {
    Init(Cap<PollsterEvent>),
    Poll(Cap<HubEvent>),
    PortStatus(Cap<PortEvent>, PortActivity),
}
impl PollsterEvent {
    pub fn new_init(pollster: &Cap<PollsterEvent>) -> PollsterEvent {
        PollsterEvent::Init(pollster.clone())
    }
    pub fn new_poll(hub: &Cap<HubEvent>) -> PollsterEvent {
        PollsterEvent::Poll(hub.clone())
    }
    pub fn new_port_status(port: &Cap<PortEvent>, state: &PortActivity) -> PollsterEvent {
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
    pub fn create(ports: &Vec<Cap<PortEvent>>) -> Cap<PollsterEvent> {
        let mut poll: HashMap<Cap<PortEvent>, PortPoll> = HashMap::new();
        for port in ports {
            poll.insert(port.clone(), PortPoll { idle: 0 });
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
                }
                Some(_) => panic!("Pollster::myself already set"),
            },
            PollsterEvent::Poll(hub) => {
                println!("Pollster::Poll hub={}", hub);
                //let myself = &self.myself.expect("NO SELF!?"); // an alternative to avoid nesting...
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
            PollsterEvent::PortStatus(port, state) => {
                let n = self.port_to_port_num(&port);
                println!(
                    "Pollster::LinkStatus[{}] port={}, link_state={:?}, ait_balance={}",
                    n, port, state.link_state, state.ait_balance
                );
                if let Some(poll) = self.poll.get_mut(port) {
                    if state.link_state == LinkState::Live {
                        poll.idle = 0;
                    } else {
                        poll.idle += 1;
                    }
                    println!("Pollster::poll[{}].idle = {}", n, poll.idle);
                }
                assert!(self.pending > 0);
                self.pending -= 1;
                if self.pending == 0 {
                    if let Some(hub) = &self.hub {
                        self.poll
                            .iter_mut()
                            .filter(|(_port, poll)| poll.idle > 3)
                            .for_each(|(port, poll)| {
                                // attempt to stop the dead port
                                port.send(PortEvent::new_stop(&hub));
                                // attempt to re-start the dead port
                                //port.send(PortEvent::new_start(hub));
                                poll.idle = 0; // reset idle counter
                            });
                    }
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
