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
    Activity(Cap<PortEvent>, PortActivity),
}
impl PollsterEvent {
    pub fn new_init(pollster: &Cap<PollsterEvent>) -> PollsterEvent {
        PollsterEvent::Init(pollster.clone())
    }
    pub fn new_poll(hub: &Cap<HubEvent>) -> PollsterEvent {
        PollsterEvent::Poll(hub.clone())
    }
    pub fn new_port_activity(port: &Cap<PortEvent>, activity: &PortActivity) -> PollsterEvent {
        PollsterEvent::Activity(port.clone(), activity.clone())
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
                //println!("Pollster::Poll hub={}", hub);
                let myself = self.myself.as_ref().expect("Pollster::myself not set!");
                if self.hub.is_none() {
                    self.hub = Some(hub.clone());
                    self.pending = self.ports.len();
                    for port in &self.ports {
                        port.send(PortEvent::new_poll(&myself));
                    }
                } else {
                    println!("Pollster::Poll already polling...");
                }
            }
            PollsterEvent::Activity(port, activity) => {
                let _n = self.port_to_port_num(&port);
                //println!("Pollster::Activity[{}] port={} {:?}", _n, port, activity);
                if let Some(poll) = self.poll.get_mut(port) {
                    if activity.link_state == LinkState::Live {
                        poll.idle = 0;
                    } else {
                        poll.idle += 1;
                    }
                    //println!("Pollster::poll[{}].idle = {}", _n, poll.idle);
                }
                assert!(self.pending > 0);
                self.pending -= 1;
                if self.pending == 0 {
                    if let Some(hub) = &self.hub {
                        println!("Pollster::Activity hub={} pending={}", hub, self.pending);
                        self.poll
                            .iter_mut()
                            .filter(|(_port, poll)| poll.idle > 3)
                            .for_each(|(port, poll)| {
                                println!("Pollster::Activity hub={} port={} DECLARED DEAD", hub, port);
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
