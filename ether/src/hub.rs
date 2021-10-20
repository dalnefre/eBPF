use crate::actor::{self, Actor, Cap};
use crate::frame::Payload;
use crate::port::{PortEvent, PortState};
use crate::cell::CellEvent;

use pretty_hex::pretty_hex;

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

// Multi-Port Node/Hub
pub struct Hub {
    myself: Option<Cap<HubEvent>>,
    port_1: Cap<PortEvent>,
    port_2: Cap<PortEvent>,
    route: usize,
}
impl Hub {
    pub fn create(port_1: &Cap<PortEvent>, port_2: &Cap<PortEvent>) -> Cap<HubEvent> {
        let hub = actor::create(Hub {
            myself: None,
            port_1: port_1.clone(),
            port_2: port_2.clone(),
            route: 1,
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
            HubEvent::PortStatus(cust, state) => {
                println!(
                    "Hub::LinkStatus cust={:?} link_state={:?}, ait_balance={}",
                    cust, state.link_state, state.ait_balance
                );
            }
            HubEvent::PortToHubWrite(cust, payload) => {
                println!(
                    "Hub::PortToHubWrite cust={:?} payload={}",
                    cust,
                    pretty_hex(&payload.data)
                );
                if let Some(_myself) = &self.myself {}
            }
            HubEvent::PortToHubRead(cust) => {
                println!("Hub::PortToHubRead cust={:?}", cust);
                if let Some(myself) = &self.myself {
                    println!(
                        "Hub::myself={:?} port_1={:?} port_2={:?}",
                        myself, self.port_1, self.port_2
                    );
                }
            }
            HubEvent::CellToHubWrite(cust, payload) => {
                println!(
                    "Hub::CellToHubWrite cust={:?} payload={}",
                    cust,
                    pretty_hex(&payload.data)
                );
                if let Some(_myself) = &self.myself {}
            }
            HubEvent::CellToHubRead(cust) => {
                println!("Hub::CellToHubRead cust={:?}", cust);
                if let Some(myself) = &self.myself {
                    println!(
                        "Hub::myself={:?} port_1={:?} port_2={:?}",
                        myself, self.port_1, self.port_2
                    );
                }
            }
        }
    }
}
