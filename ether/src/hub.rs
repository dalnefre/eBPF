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
    Nowhere,
    Cell,
    Port(usize),
}

struct CellBuf {
    reader: Option<Cap<CellEvent>>, // inbound
    writer: Option<Cap<CellEvent>>, // outbound
    payload: Option<Payload>, // outbound
    send_to: Vec<Route>, // outbound
}

struct PortBuf {
    port: Cap<PortEvent>, // persistent
    reader: Option<Cap<PortEvent>>, // outbound
    writer: Option<Cap<PortEvent>>, // inbound
    payload: Option<Payload>, // inbound
    send_to: Vec<Route>, // inbound
}

// Multi-Port Hub (Node)
pub struct Hub {
    myself: Option<Cap<HubEvent>>,
    cell_buf: CellBuf,
    port_bufs: Vec<PortBuf>,
}
impl Hub {
    pub fn create(ports: &[Cap<PortEvent>]) -> Cap<HubEvent> {
        let cell_buf = CellBuf {
            reader: None,
            writer: None,
            payload: None,
            send_to: Vec::with_capacity(MAX_PORTS),
        };
        let port_bufs: Vec<_> = ports
            .iter()
            .map(|port| PortBuf {
                port: port.clone(),
                reader: Some(port.clone()),
                writer: None,
                payload: None,
                send_to: Vec::with_capacity(MAX_PORTS),
            })
            .collect();
        let hub = actor::create(Hub {
            myself: None,
            cell_buf,
            port_bufs,
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
                match &self.port_bufs[n].writer {
                    None => {
                        self.port_bufs[n].writer = Some(cust.clone());
                        self.port_bufs[n].payload = Some(payload.clone());
                        self.find_routes(Route::Port(n), &payload);
                        self.try_everyone();
                    }
                    Some(_cust) => panic!("Only one Port-to-Hub writer allowed"),
                }
            }
            HubEvent::PortToHubRead(cust) => {
                println!("Hub::PortToHubRead");
                let n = self.port_to_port_num(&cust);
                match &self.port_bufs[n].reader {
                    None => {
                        self.port_bufs[n].reader = Some(cust.clone());
                        self.try_everyone();
                    }
                    Some(_cust) => panic!("Only one Port-to-Hub reader allowed"),
                }
            }
            HubEvent::CellToHubWrite(cust, payload) => {
                println!("Hub::CellToHubWrite");
                match &self.cell_buf.writer {
                    None => {
                        self.cell_buf.writer = Some(cust.clone());
                        self.cell_buf.payload = Some(payload.clone());
                        self.find_routes(Route::Cell, &payload);
                        self.try_everyone();
                    }
                    Some(_cust) => panic!("Only one Cell-to-Hub writer allowed"),
                }
            }
            HubEvent::CellToHubRead(cust) => {
                println!("Hub::CellToHubRead");
                match &self.cell_buf.reader {
                    None => {
                        self.cell_buf.reader = Some(cust.clone());
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
            Route::Nowhere => panic!("Route from Nowhere?!"),
            Route::Cell => {
                let routes = &mut self.cell_buf.send_to;
                assert!(routes.is_empty()); // there shouldn't be any left-over routes
                routes.push(Route::Port(0)); // all Cell tokens route to Port(0)
            },
            Route::Port(n) => {
                let routes = &mut self.port_bufs[n].send_to;
                assert!(routes.is_empty()); // there shouldn't be any left-over routes
                routes.push(Route::Cell); // all Port(_) tokens route to Cell
            },
        }
    }
    fn try_everyone(&mut self) {
        if let Some(myself) = &self.myself {
            // try sending from Cell
            if let Some(cell) = &self.cell_buf.writer {
                if let Some(payload) = &self.cell_buf.payload {
                    if !self.try_sending(Route::Cell, &payload) {
                        cell.send(CellEvent::new_hub_to_cell_read()); // ack writer
                        self.cell_buf.writer = None;
                        self.cell_buf.payload = None;
                    }
                }
            }
            // try sending from each Port
            let mut n: usize = 0; // current port number
            while n < MAX_PORTS {
                if let Some(port) = &self.port_bufs[n].writer {
                    if let Some(payload) = &self.port_bufs[n].payload {
                        if !self.try_sending(Route::Port(n), &payload) {
                            port.send(PortEvent::new_hub_to_port_read(&myself)); // ack writer
                            self.port_bufs[n].writer = None;
                            self.port_bufs[n].payload = None;
                        }
                    }
                }
                n += 1; // next port
            }    
        }
    }
    fn try_sending(&mut self, from: Route, payload: &Payload) -> bool {
        let routes = match from {
            Route::Nowhere => panic!("Route from Nowhere?!"),
            Route::Cell => &mut self.cell_buf.send_to,
            Route::Port(n) => &mut self.port_bufs[n].send_to,
        };
        if routes.is_empty() {
            return false; // no more routes
        }
        if let Some(myself) = &self.myself {
            let mut i: usize = 0; // current route index
            while i < routes.len() {
                match routes[i] {
                    Route::Nowhere => panic!("Route to Nowhere?!"),
                    Route::Cell => {
                        if let Some(cell) = &self.cell_buf.reader {
                            cell.send(CellEvent::new_hub_to_cell_write(&payload));
                            self.cell_buf.reader = None;
                            routes.remove(i);
                        } else {
                            i += 1; // route not ready
                        }
                    },
                    Route::Port(to) => {
                        if let Some(port) = &self.port_bufs[to].reader {
                            port.send(PortEvent::new_hub_to_port_write(&myself, &payload));
                            self.port_bufs[to].reader = None;
                            routes.remove(i);
                        } else {
                            i += 1; // route not ready
                        }        
                    },
                }
            }    
        }
        true // waiting for acks
    }
    fn port_to_port_num(&mut self, port: &Cap<PortEvent>) -> usize {
        self.port_bufs
            .iter()
            .enumerate()
            .find(|(_port_num, port_buf)| &port_buf.port == port)
            .expect("unknown Port")
            .0
    }
}
