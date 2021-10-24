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

const MAX_PORTS: usize = 3;

enum Route {
    Cell,
    Port(usize),
}

struct CellIn {
    reader: Option<Cap<CellEvent>>, // inbound
}

struct CellOut {
    writer: Option<Cap<CellEvent>>, // outbound
    payload: Option<Payload>, // outbound
    send_to: Vec<Route>, // outbound
}

struct PortIn { // inbound from port
    writer: Option<Cap<PortEvent>>, // inbound
    payload: Option<Payload>, // inbound
    send_to: Vec<Route>, // inbound
}

struct PortOut { // outbound to port
    reader: Option<Cap<PortEvent>>, // outbound
}

// Multi-Port Hub (Node)
pub struct Hub {
    myself: Option<Cap<HubEvent>>,
    ports: Vec<Cap<PortEvent>>,
    cell_in: CellIn,
    cell_out: CellOut,
    port_in: Vec<PortIn>,
    port_out: Vec<PortOut>,
}
impl Hub {
    pub fn create(ports: &[Cap<PortEvent>]) -> Cap<HubEvent> {
        let ports: Vec<_> = ports
            .iter()
            .map(|port| port.clone() )
            .collect();
        let cell_in = CellIn {
            reader: None,
        };
        let cell_out = CellOut {
            writer: None,
            payload: None,
            send_to: Vec::with_capacity(MAX_PORTS),
        };
        let port_in: Vec<_> = ports
            .iter()
            .map(|_port| PortIn {
                writer: None,
                payload: None,
                send_to: Vec::with_capacity(MAX_PORTS),
            })
            .collect();
        let port_out: Vec<_> = ports
            .iter()
            .map(|port| PortOut {
                reader: Some(port.clone()),
            })
            .collect();
        let hub = actor::create(Hub {
            myself: None,
            ports,
            cell_in,
            cell_out,
            port_in,
            port_out,
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
            HubEvent::PortToHubWrite(cust, payload) => {
                println!("Hub::PortToHubWrite");
                let n = self.port_to_port_num(&cust);
                let port_in = &mut self.port_in[n];
                match &port_in.writer {
                    None => {
                        port_in.writer = Some(cust.clone());
                        port_in.payload = Some(payload.clone());
                        self.find_routes(Route::Port(n), &payload);
                        self.try_everyone();
                    }
                    Some(_cust) => panic!("Only one Port-to-Hub writer allowed"),
                }
            }
            HubEvent::PortToHubRead(cust) => {
                println!("Hub::PortToHubRead");
                let n = self.port_to_port_num(&cust);
                let port_out = &mut self.port_out[n];
                match &port_out.reader {
                    None => {
                        port_out.reader = Some(cust.clone());
                        self.try_everyone();
                    }
                    Some(_cust) => panic!("Only one Port-to-Hub reader allowed"),
                }
            }
            HubEvent::CellToHubWrite(cust, payload) => {
                println!("Hub::CellToHubWrite");
                match &self.cell_out.writer {
                    None => {
                        self.cell_out.writer = Some(cust.clone());
                        self.cell_out.payload = Some(payload.clone());
                        self.find_routes(Route::Cell, &payload);
                        self.try_everyone();
                    }
                    Some(_cust) => panic!("Only one Cell-to-Hub writer allowed"),
                }
            }
            HubEvent::CellToHubRead(cust) => {
                println!("Hub::CellToHubRead");
                match &self.cell_in.reader {
                    None => {
                        self.cell_in.reader = Some(cust.clone());
                        self.try_everyone();
                    }
                    Some(_cust) => panic!("Only one Cell-to-Hub reader allowed"),
                }
            }
        }
    }
}
impl Hub {
    fn find_routes(&mut self, from: Route, payload: &Payload) {
        // FIXME: this is a completely bogus "routing table" lookup!
        // The TreeId in the Payload should determine the routes, excluding `from`.
        let _tree_id = &payload.id;
        match from {
            Route::Cell => {
                let routes = &mut self.cell_out.send_to;
                assert!(routes.is_empty()); // there shouldn't be any left-over routes
                routes.push(Route::Port(0)); // all Cell tokens route to Port(0)
            },
            Route::Port(n) => {
                let routes = &mut self.port_in[n].send_to;
                assert!(routes.is_empty()); // there shouldn't be any left-over routes
                routes.push(Route::Cell); // all Port(_) tokens route to Cell
            },
        }
    }
    fn try_everyone(&mut self) {
        if let Some(myself) = &self.myself {
            // try sending from Cell
            let cell_out = &mut self.cell_out;
            if let Some(cell) = &cell_out.writer {
                if let Some(payload) = &cell_out.payload {
                    let routes = &mut cell_out.send_to;
                    if !routes.is_empty() {
                        let mut i: usize = 0; // current route index
                        while i < routes.len() {
                            match routes[i] {
                                Route::Cell => panic!("Can't route Cell to itself"),
                                Route::Port(to) => {
                                    let port_out = &mut self.port_out[to];
                                    if let Some(port) = &port_out.reader {
                                        port.send(PortEvent::new_hub_to_port_write(&myself, &payload));
                                        port_out.reader = None;
                                        routes.remove(i);
                                    } else {
                                        i += 1; // route not ready
                                    }
                                },
                            }
                        }
                    } else {
                        // no more routes
                        cell.send(CellEvent::new_hub_to_cell_read()); // ack writer
                        cell_out.writer = None;
                        cell_out.payload = None;
                    }
                }
            }
            // try sending from each Port
            let mut from: usize = 0; // current port number
            while from < MAX_PORTS {
                let port_in = &mut self.port_in[from];
                if let Some(port) = &port_in.writer {
                    if let Some(payload) = &port_in.payload {
                        let routes = &mut port_in.send_to;
                        if !routes.is_empty() {
                            let mut i: usize = 0; // current route index
                            while i < routes.len() {
                                match routes[i] {
                                    Route::Cell => {
                                        let cell_in = &mut self.cell_in;
                                        if let Some(cell) = &cell_in.reader {
                                            cell.send(CellEvent::new_hub_to_cell_write(&payload));
                                            cell_in.reader = None;
                                            routes.remove(i);
                                        } else {
                                            i += 1; // route not ready
                                        }
                                    },
                                    Route::Port(to) => {
                                        let port_out = &mut self.port_out[to];
                                        if let Some(port) = &port_out.reader {
                                            port.send(PortEvent::new_hub_to_port_write(&myself, &payload));
                                            port_out.reader = None;
                                            routes.remove(i);
                                        } else {
                                            i += 1; // route not ready
                                        }
                                    },
                                }
                            }
                        } else {
                            // no more routes
                            port.send(PortEvent::new_hub_to_port_read(&myself)); // ack writer
                            port_in.writer = None;
                            port_in.payload = None;
                        }
                    }
                }
                from += 1; // next port
            }
        }
    }
    fn port_to_port_num(&mut self, port: &Cap<PortEvent>) -> usize {
        self.ports
            .iter()
            .enumerate()
            .find(|(_port_num, port_cap)| *port_cap == port)
            .expect("unknown Port")
            .0
    }
}
