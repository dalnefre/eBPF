use crate::actor::{self, Actor, Cap};
use crate::cell::CellEvent;
use crate::frame::Payload;
use crate::port::{PortEvent, PortState};

#[derive(Debug, Clone)]
pub enum HubEvent {
    Init(Cap<HubEvent>),
    PortStatus(Cap<PortEvent>, PortState),
    PortToHubWrite(Cap<PortEvent>, Payload),
    PortToHubRead(Cap<PortEvent>),
    CellToHubWrite(Cap<CellEvent>, Payload),
    CellToHubRead(Cap<CellEvent>),
}
impl HubEvent {
    pub fn new_init(hub: &Cap<HubEvent>) -> HubEvent {
        HubEvent::Init(hub.clone())
    }
    pub fn new_port_status(port: &Cap<PortEvent>, state: &PortState) -> HubEvent {
        HubEvent::PortStatus(port.clone(), state.clone())
    }
    pub fn new_port_to_hub_write(port: &Cap<PortEvent>, payload: &Payload) -> HubEvent {
        HubEvent::PortToHubWrite(port.clone(), payload.clone())
    }
    pub fn new_port_to_hub_read(port: &Cap<PortEvent>) -> HubEvent {
        HubEvent::PortToHubRead(port.clone())
    }
    pub fn new_cell_to_hub_write(cell: &Cap<CellEvent>, payload: &Payload) -> HubEvent {
        HubEvent::CellToHubWrite(cell.clone(), payload.clone())
    }
    pub fn new_cell_to_hub_read(cell: &Cap<CellEvent>) -> HubEvent {
        HubEvent::CellToHubRead(cell.clone())
    }
}

// Single-Port Hub (Node)
pub struct Hub {
    myself: Option<Cap<HubEvent>>,
    port: Cap<PortEvent>,
    reader: Option<Cap<CellEvent>>,
    writer: Option<Cap<CellEvent>>,
}
impl Hub {
    pub fn create(port: &Cap<PortEvent>) -> Cap<HubEvent> {
        let hub = actor::create(Hub {
            myself: None,
            port: port.clone(),
            reader: None,
            writer: None,
        });
        hub.send(HubEvent::new_init(&hub));
        hub
    }
}
impl Actor for Hub {
    type Event = HubEvent;

    fn on_event(&mut self, event: Self::Event) {
        match &event {
            HubEvent::Init(myself) => match &self.myself {
                None => self.myself = Some(myself.clone()),
                Some(_) => panic!("Hub::myself already set"),
            },
            HubEvent::PortStatus(_cust, state) => {
                println!(
                    "Hub::LinkStatus link_state={:?}, ait_balance={}",
                    state.link_state, state.ait_balance
                );
            }
            HubEvent::PortToHubWrite(_cust, payload) => {
                println!("Hub::PortToHubWrite");
                match &self.reader {
                    Some(cell) => {
                        cell.send(CellEvent::new_hub_to_cell_write(&payload));
                        self.reader = None;
                    }
                    None => panic!("Cell-to-Hub reader not ready"),
                }
            }
            HubEvent::PortToHubRead(_cust) => {
                println!("Hub::PortToHubRead");
                match &self.writer {
                    Some(cell) => {
                        cell.send(CellEvent::new_hub_to_cell_read());
                        self.writer = None;
                    }
                    None => panic!("Cell-to-Hub writer not ready"),
                }
            }
            HubEvent::CellToHubWrite(cust, payload) => {
                if let Some(myself) = &self.myself {
                    println!("Hub::CellToHubWrite");
                    match &self.writer {
                        None => {
                            self.writer = Some(cust.clone());
                            self.port
                                .send(PortEvent::new_hub_to_port_write(&myself, &payload));
                        }
                        Some(_cust) => panic!("Only one Cell-to-Hub writer allowed"),
                    }
                }
            }
            HubEvent::CellToHubRead(cust) => {
                if let Some(myself) = &self.myself {
                    println!("Hub::CellToHubRead");
                    match &self.reader {
                        None => {
                            self.reader = Some(cust.clone());
                            self.port.send(PortEvent::new_hub_to_port_read(&myself));
                        }
                        Some(_cust) => panic!("Only one Cell-to-Hub reader allowed"),
                    }
                }
            }
        }
    }
}
