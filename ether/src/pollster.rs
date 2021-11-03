use crate::actor::{self, Actor, Cap};
use crate::hub::HubEvent;
use crate::port::{PortEvent, PortState};

//use pretty_hex::pretty_hex;

#[derive(Debug, Clone)]
pub enum PollsterEvent {
    Init(Cap<PollsterEvent>),
    Poll(Cap<HubEvent>),
    PortStatus(Cap<PortEvent>, PortState),
}
impl PollsterEvent {
    pub fn new_init(pollster: &Cap<PollsterEvent>) -> PollsterEvent {
        PollsterEvent::Init(pollster.clone())
    }
    pub fn new_poll(hub: &Cap<HubEvent>) -> PollsterEvent {
        PollsterEvent::Poll(hub.clone())
    }
    pub fn new_port_status(port: &Cap<PortEvent>, state: &PortState) -> PollsterEvent {
        PollsterEvent::PortStatus(port.clone(), state.clone())
    }
}

// link failure detector based on polling for port activity
pub struct Pollster {
    myself: Option<Cap<PollsterEvent>>,
    _hub: Cap<HubEvent>,
    ports: Vec<Cap<PortEvent>>,
}
impl Pollster {
    pub fn create(
        hub: &Cap<HubEvent>, // FIXME: do we need to know this at creation?
        ports: &Vec<Cap<PortEvent>>,
    ) -> Cap<PollsterEvent> {
        let pollster = actor::create(Pollster {
            myself: None,
            _hub: hub.clone(),
            ports: ports.clone(),
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
            PollsterEvent::Poll(_hub) => {
                println!("Pollster::Poll");
                if let Some(myself) = &self.myself {
                    for port in &self.ports {
                        port.send(PortEvent::new_poll(&myself));
                    }
                }
            }
            PollsterEvent::PortStatus(port, state) => {
                let n = self.port_to_port_num(&port);
                println!(
                    "Pollster::LinkStatus[{}] link_state={:?}, ait_balance={}",
                    n, state.link_state, state.ait_balance
                );
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