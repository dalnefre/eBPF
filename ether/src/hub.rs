use std::collections::VecDeque;

use crate::actor::{self, Actor, Cap};
use crate::cell::CellEvent;
use crate::frame::{self, Payload, TreeId};
use crate::link::LinkState;
use crate::pollster::{Pollster, PollsterEvent};
use crate::port::{PortEvent, PortStatus};

#[derive(Debug, Clone)]
pub enum HubEvent {
    Init(Cap<HubEvent>),
    Status(Cap<PortEvent>, PortStatus),
    PortToHubWrite(Cap<PortEvent>, Payload),
    PortToHubRead(Cap<PortEvent>),
    CellToHubWrite(Cap<CellEvent>, Payload),
    CellToHubRead(Cap<CellEvent>),
}
impl HubEvent {
    pub fn new_init(hub: &Cap<HubEvent>) -> HubEvent {
        HubEvent::Init(hub.clone())
    }
    pub fn new_status(port: &Cap<PortEvent>, status: &PortStatus) -> HubEvent {
        HubEvent::Status(port.clone(), status.clone())
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
    // Inbound to Cell
    reader: Option<Cap<CellEvent>>,
}

struct CellOut {
    // Outbound from Cell
    writer: Option<Cap<CellEvent>>,
    payload: Option<Payload>,
    send_to: Vec<Route>,
}

struct PortIn {
    // Inbound from port
    writer: Option<Cap<PortEvent>>, // inbound writer
    payload: Option<Payload>, // inbound payload
    send_to: Vec<Route>, // routes for inbound delivery
}

struct PortOut {
    // Outbound to port
    reader: Option<Cap<PortEvent>>, // outbound read credit
    ctrl_msgs: VecDeque<Payload>, // outbound contol messages
}

struct Failover {
    // Failover recovery information
    payload_r: Option<Payload>, // payload from early FAILOVER_R
    status_r: Option<PortStatus>, // status to satisfy FAILOVER_R
    status_d: Option<PortStatus>, // status to satisfy FAILOVER_D
}

// Multi-Port Hub (Node)
pub struct Hub {
    myself: Option<Cap<HubEvent>>,
    ports: Vec<Cap<PortEvent>>,
    cell_in: CellIn,
    cell_out: CellOut,
    port_in: Vec<PortIn>,
    port_out: Vec<PortOut>,
    failover: Vec<Failover>,
    route_port: usize, // outbound port # for all trees
}
impl Hub {
    pub fn create(port_set: &[Cap<PortEvent>]) -> Cap<HubEvent> {
        let ports: Vec<_> = port_set.iter().map(|port| port.clone()).collect();
        let cell_in = CellIn { reader: None };
        let cell_out = CellOut {
            writer: None,
            payload: None,
            send_to: Vec::with_capacity(MAX_PORTS),
        };
        let port_in: Vec<_> = port_set
            .iter()
            .map(|_port| PortIn {
                writer: None,
                payload: None,
                send_to: Vec::with_capacity(MAX_PORTS),
            })
            .collect();
        let port_out: Vec<_> = port_set
            .iter()
            .map(|port| PortOut {
                reader: Some(port.clone()),
                ctrl_msgs: VecDeque::with_capacity(MAX_PORTS),
            })
            .collect();
        let failover: Vec<_> = port_set
            .iter()
            .map(|_port| Failover {
                payload_r: None,
                status_r: None,
                status_d: None,
            })
            .collect();
        assert_eq!(ports.len(), port_in.len());
        assert_eq!(ports.len(), port_out.len());
        assert_eq!(ports.len(), failover.len());
        let hub = actor::create(Hub {
            myself: None,
            ports: ports.clone(),
            cell_in,
            cell_out,
            port_in,
            port_out,
            failover,
            route_port: 0,
        });
        hub.send(HubEvent::new_init(&hub));
        for port in port_set {
            port.send(PortEvent::new_init(&port, &hub)); // connect Port to Hub
            port.send(PortEvent::new_start(&hub)); // attempt to start Port
            //port.send(PortEvent::new_hub_to_port_read(&hub)); // Port ready to receive
        }
        let pollster = Pollster::create(&ports); // create link-failure detector
        // periodically poll ports for activity
        let cust = hub.clone(); // local copy moved into closure
        std::thread::spawn(move || loop {
            std::thread::sleep(core::time::Duration::from_millis(500));
            pollster.send(PollsterEvent::new_poll(&cust));
        });
        // return Hub capability
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
            HubEvent::Status(cust, status) => {
                let myself = self.myself.as_ref().expect("Hub::myself not set!");
                let n = self.port_to_port_num(&cust);
                println!("Hub{}::Status[{}] port={} {:?}", myself, n, cust, status);
                let activity = &status.activity;
                if activity.link_state == LinkState::Stop {
                    println!("Hub{}::Status[{}] STOP ... reader={}",
                        myself, n, self.port_out[n].reader.is_some());
                    self.port_out[n].reader = None; // clear outbound reader (port disabled)
                    let m = (n + 1) % self.ports.len(); // wrap-around fail-over port numbers
                    self.failover[n].status_d = Some(status.clone()); // save Port status for FAILOVER_D
                    // enqueue failover control message
                    println!("Hub{}::Status FAILOVER Port({}) -> Port({})", myself, n, m);
                    let id = TreeId::new(0x8888); // FIXME: need the TreeId of our peer node
                    let msg = Payload::ctrl_msg(
                        &id,
                        frame::FAILOVER_R,
                        activity.ait_balance as u8,
                        activity.sequence,
                        0x44556677
                    );
                    self.port_out[m].ctrl_msgs.push_back(msg);
                    if let Some(_payload) = &self.failover[n].payload_r {
                        // got early FAILOVER_R, send FAILOVER_D immediately...
                        println!("Hub{}::Status early failover_r[{}->{}]", myself, n, m);
                        assert!(self.failover[n].status_r.is_none());
                        // enqueue failover done message
                        let id = TreeId::new(0x8888); // FIXME: need the TreeId of our peer node
                        let msg = Payload::ctrl_msg(
                            &id,
                            frame::FAILOVER_D,
                            activity.ait_balance as u8,
                            activity.sequence,
                            0x44556677
                        );
                        self.port_out[m].ctrl_msgs.push_back(msg);
                        self.failover[n].payload_r = None; // clear early FAILOVER_R
                        // FIXME: `failover[n].payload_r` is never used, could be a Boolean flag?
                    } else {
                        println!("Hub{}::Status[{}] waiting for failover_d...", myself, n);
                        self.failover[n].status_r = Some(status.clone()); // save Port status for FAILOVER_R
                    }
                    self.try_everyone();
                }
            }
            HubEvent::PortToHubWrite(cust, payload) => {
                let n = self.port_to_port_num(&cust);
                //println!("Hub::PortToHubWrite[{}] port={}", n, cust);
                if payload.ctrl {
                    let myself = self.myself.as_ref().expect("Hub::myself not set!");
                    //println!("Hub{}::Control[{}] port={} msg={:?}", myself, n, cust, payload);
                    if payload.get_op() == frame::FAILOVER_R {
                        let peer_bal = payload.get_u8() as i8 as isize;
                        let peer_seq = payload.get_u16();
                        println!("Hub{}::Control FAILOVER_R peer bal={} seq={}", myself, peer_bal, peer_seq);
                        // check for STOP'd Port status
                        let m = (if n < 1 { self.ports.len() } else { n }) - 1; // failed port number
                        match &self.failover[m].status_r {
                            Some(status) => {
                                println!("Hub{}::failover_r[{}] {:?}", myself, m, status);
                                let activity = &status.activity;
                                // enqueue failover done message
                                let id = TreeId::new(0x8888); // FIXME: need the TreeId of our peer node
                                let msg = Payload::ctrl_msg(
                                    &id,
                                    frame::FAILOVER_D,
                                    activity.ait_balance as u8,
                                    activity.sequence,
                                    0x44556677
                                );
                                self.port_out[n].ctrl_msgs.push_back(msg);
                                // clear saved status
                                self.failover[m].status_r = None;
                            },
                            None => {
                                // store FAILOVER_R info and wait for STOP from failing Port...
                                println!("Hub{}::failover_r[{}] waiting for STOP...", myself, m);
                                self.failover[m].payload_r = Some(payload.clone());
                            },
                        };
                    } else if payload.get_op() == frame::FAILOVER_D {
                        let peer_bal = payload.get_u8() as i8 as isize;
                        let peer_seq = payload.get_u16();
                        println!("Hub{}::Control FAILOVER_D peer bal={} seq={}",
                            myself, peer_bal, peer_seq);
                        let m = (if n < 1 { self.ports.len() } else { n }) - 1; // failed port number
                        match &self.failover[m].status_d {
                            Some(status) => {
                                println!("Hub{}::failover_d[{}->{}] {:?}", myself, m, n, status);
                                let balance = status.activity.ait_balance;
                                let resend = (balance == -1 && peer_bal == 0)
                                    || (balance == 0 && status.outbound.is_some());
                                if resend {
                                    // resend AIT
                                    if let Some(_ait) = &status.outbound {
                                        println!("Hub{}::failover_d[{}->{}] resend AIT... (balance {})",
                                            myself, m, n, balance);
                                        self.port_out[n].ctrl_msgs.push_front(_ait.clone());
                                    } else {
                                        panic!("Hub{}:failover_d missing payload! (balance {})",
                                            myself, balance);
                                    }
                                }
                                // update routing "table"
                                println!("Hub{}::reroute Port({}) -> Port({})", myself, m, n);
                                self.route_port = n;
                                // re-route waiting token from cell
                                let routes = &mut self.cell_out.send_to;
                                for i in 0..routes.len() {
                                    match routes[i] {
                                        Route::Cell => {}
                                        Route::Port(num) => {
                                            if num == m {
                                                routes[i] = Route::Port(n);
                                                println!("Hub{}::reroute cell-out {} -> {}", myself, m, n);
                                            }
                                        }
                                    }
                                }
                                // attempt to re-start stopped link
                                let credit = self.port_in[m].payload.is_none(); // read credit needed
                                println!("Hub{}::restarting Port({}) ... credit={}", myself, m, credit);
                                if credit {
                                    // provide read credit
                                    self.port_out[m].reader = Some(self.ports[m].clone());
                                }
                                self.ports
                                    .get(m)
                                    .unwrap()
                                    .send(PortEvent::new_start(&myself));
                                // clear saved status
                                self.failover[m].status_d = None;
                            },
                            None => {
                                panic!("Hub::FAILOVER_D missing STOP status!");
                            },
                        };
                    } else {
                        println!("Hub{}::CONTROL UNKNOWN ... port={} msg={:?}", myself, cust, payload);
                    }
                    cust.send(PortEvent::new_hub_to_port_read(&myself)); // ack control msg
                    self.try_everyone();
                } else {
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
            }
            HubEvent::PortToHubRead(cust) => {
                let n = self.port_to_port_num(&cust);
                //println!("Hub::PortToHubRead[{}] port={}", n, cust);
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
                //println!("Hub::CellToHubWrite cell={}", cust);
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
                //println!("Hub::CellToHubRead cell={}", cust);
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
    fn port_to_port_num(&self, port: &Cap<PortEvent>) -> usize {
        self.ports
            .iter()
            .enumerate()
            .find(|(_port_num, port_cap)| *port_cap == port)
            .expect("unknown Port")
            .0
    }
    fn find_routes(&mut self, from: Route, payload: &Payload) {
        // FIXME: this is a completely bogus "routing table" lookup!
        // The TreeId in the Payload should determine the routes, excluding `from`.
        let _tree_id = &payload.id;
        match from {
            Route::Cell => {
                let routes = &mut self.cell_out.send_to;
                assert!(routes.is_empty()); // there shouldn't be any left-over routes
                routes.push(Route::Port(self.route_port)); // all Cell tokens route to same Port
            }
            Route::Port(n) => {
                let routes = &mut self.port_in[n].send_to;
                assert!(routes.is_empty()); // there shouldn't be any left-over routes
                routes.push(Route::Cell); // all Port(_) tokens route to Cell
            }
        }
    }
    fn send_to_routes(
        hub: &Cap<HubEvent>,
        payload: &Payload,
        routes: &mut Vec<Route>,
        cell_in: &mut CellIn,
        port_out: &mut Vec<PortOut>,
    ) {
        let mut i: usize = 0; // current route index
        while i < routes.len() {
            match routes[i] {
                Route::Cell => {
                    if let Some(cell) = &cell_in.reader {
                        //println!("Hub{}::send_to_routes {:?} to Cell {}", hub, payload, cell);
                        cell.send(CellEvent::new_hub_to_cell_write(&payload));
                        cell_in.reader = None;
                        routes.remove(i);
                    } else {
                        i += 1; // route not ready
                    }
                }
                Route::Port(to) => {
                    if let Some(port) = &port_out[to].reader {
                        //println!("Hub{}::send_to_routes {:?} to Port({}) {}", hub, payload, to, port);
                        port.send(PortEvent::new_hub_to_port_write(&hub, &payload));
                        port_out[to].reader = None;
                        routes.remove(i);
                    } else {
                        i += 1; // route not ready
                    }
                }
            }
        }
    }
    fn try_everyone(&mut self) {
        let myself = self.myself.as_ref().expect("Hub::myself not set!");
        // try sending control messages to each Port
        let mut it = self.port_out.iter_mut();
        while let Some(port_out) = it.next() {
            if let Some(port) = &port_out.reader {
                if let Some(payload) = &port_out.ctrl_msgs.pop_front() {
                    //println!("Hub{}::try_everyone control {:?} to port {}", myself, payload, port);
                    port.send(PortEvent::new_hub_to_port_write(&myself, &payload));
                    port_out.reader = None;
                }
            }
        }
        // try sending from Cell
        let cell_out = &mut self.cell_out;
        if let Some(cell) = &cell_out.writer {
            if let Some(payload) = &cell_out.payload {
                let routes = &mut cell_out.send_to;
                if !routes.is_empty() {
                    Self::send_to_routes(
                        &myself,
                        &payload,
                        routes,
                        &mut self.cell_in,
                        &mut self.port_out,
                    );
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
        while from < self.ports.len() {
            let port_in = &mut self.port_in[from];
            if let Some(port) = &port_in.writer {
                if let Some(payload) = &port_in.payload {
                    let routes = &mut port_in.send_to;
                    if !routes.is_empty() {
                        Self::send_to_routes(
                            &myself,
                            &payload,
                            routes,
                            &mut self.cell_in,
                            &mut self.port_out,
                        );
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
