use crossbeam::crossbeam_channel::unbounded as channel;
//use crossbeam::crossbeam_channel::{Receiver, Sender};

use ether::actor::{self, Actor, Cap};
use ether::cell::CellEvent;
use ether::frame::{self, Frame, Payload, TreeId};
use ether::hub::{Hub, HubEvent};
use ether::link::{Link, LinkEvent, LinkState};
use ether::port::{Port, PortEvent};
use ether::wire::{Wire, WireEvent};

#[test]
fn exactly_once_in_order_ait_port_to_port() {
    pub const N_END: u8 = 0x03;

    #[derive(Debug, Clone)]
    pub struct VerifyEvent; // verify mock

    #[derive(Debug, Clone)]
    pub enum PortMockEvent {
        // aggregate event type
        Mock(PortEvent),
        Ctrl(VerifyEvent),
    }

    pub struct PortMock {
        myself: Option<Cap<PortEvent>>,
        link: Cap<LinkEvent>,
        n_send: u8,
        n_recv: u8,
        in_order: bool,
    }
    impl PortMock {
        pub fn create(link: &Cap<LinkEvent>) -> Cap<PortMockEvent> {
            actor::create(PortMock {
                myself: None,
                link: link.clone(),
                n_send: 0,
                n_recv: 0,
                in_order: true,
            })
        }
    }
    impl Actor for PortMock {
        type Event = PortMockEvent;

        fn on_event(&mut self, event: Self::Event) {
            match &event {
                PortMockEvent::Mock(port_event) => match &port_event {
                    PortEvent::Init(myself) => match &self.myself {
                        None => {
                            self.myself = Some(myself.clone());
                        },
                        Some(_) => panic!("Port::port already set"),
                    },
                    PortEvent::Poll(cust) => {
                        println!("Port::Poll cust={:?}", cust);
                    },
                    PortEvent::LinkStatus(state, balance) => {
                        println!("Port::LinkStatus state={:?}, balance={}", state, balance);
                    }
                    PortEvent::LinkToPortWrite(payload) => {
                        if let Some(myself) = &self.myself {
                            self.n_recv += 1;
                            if payload.data[0] != self.n_recv {
                                self.in_order = false;
                            }
                            self.link.send(LinkEvent::new_read(&myself));
                        }
                    }
                    PortEvent::LinkToPortRead => {
                        if let Some(myself) = &self.myself {
                            if self.n_send < N_END {
                                self.n_send += 1;
                                let tree_id = TreeId::new(144); // FIXME: fake TreeId
                                let data = [self.n_send; frame::PAYLOAD_SIZE];
                                let payload = Payload::new(&tree_id, &data);
                                self.link.send(LinkEvent::new_write(&myself, &payload));
                            }
                        }
                    }
                    PortEvent::HubToPortWrite(cust, payload) => {
                        println!("Port::HubToPortWrite cust={:?} payload={:?}", cust, payload);
                    },
                    PortEvent::HubToPortRead(cust) => {
                        println!("Port::HubToPortRead cust={:?}", cust);
                    },
                },
                PortMockEvent::Ctrl(_verify_event) => {
                    println!("VERIFYING...");
                    assert!(self.myself.is_some());
                    assert_eq!(N_END, self.n_send);
                    assert_eq!(N_END, self.n_recv);
                    assert_eq!(true, self.in_order);
                }
            }
        }
    }

    struct PortMockFacet {
        mock: Cap<PortMockEvent>,
    }
    impl PortMockFacet {
        pub fn create(mock: &Cap<PortMockEvent>) -> Cap<PortEvent> {
            let port = actor::create(PortMockFacet { mock: mock.clone() });
            port.send(PortEvent::new_init(&port));
            port
        }
    }
    impl Actor for PortMockFacet {
        type Event = PortEvent;

        fn on_event(&mut self, event: Self::Event) {
            self.mock.send(PortMockEvent::Mock(event));
        }
    }

    struct PortCtrlFacet {
        mock: Cap<PortMockEvent>,
    }
    impl PortCtrlFacet {
        pub fn create(mock: &Cap<PortMockEvent>) -> Cap<VerifyEvent> {
            actor::create(PortCtrlFacet { mock: mock.clone() })
        }
    }
    impl Actor for PortCtrlFacet {
        type Event = VerifyEvent;

        fn on_event(&mut self, event: Self::Event) {
            self.mock.send(PortMockEvent::Ctrl(event));
        }
    }

    let (a_to_b_tx, a_to_b_rx) = channel::<Frame>();
    let (b_to_a_tx, b_to_a_rx) = channel::<Frame>();

    // Alice
    let a_wire = Wire::create(&a_to_b_tx, &b_to_a_rx);
    let a_nonce = 12345;
    let a_link = Link::create(&a_wire, a_nonce);
    a_wire.send(WireEvent::new_listen(&a_link)); // start listening
    let a_port_mock = PortMock::create(&a_link);
    let a_port_ctrl = PortCtrlFacet::create(&a_port_mock);
    let a_port = PortMockFacet::create(&a_port_mock);
    a_link.send(LinkEvent::new_start(&a_port)); // start link
    a_port.send(PortEvent::new_link_to_port_read()); // Port is ready to receive
    a_link.send(LinkEvent::new_read(&a_port)); // Link is ready to receive

    // Bob
    let b_wire = Wire::create(&b_to_a_tx, &a_to_b_rx);
    let b_nonce = 67890;
    let b_link = Link::create(&b_wire, b_nonce);
    b_wire.send(WireEvent::new_listen(&b_link)); // start listening
    let b_port_mock = PortMock::create(&b_link);
    let b_port_ctrl = PortCtrlFacet::create(&b_port_mock);
    let b_port = PortMockFacet::create(&b_port_mock);
    b_link.send(LinkEvent::new_start(&b_port)); // start link
    b_port.send(PortEvent::new_link_to_port_read()); // Port is ready to receive
    b_link.send(LinkEvent::new_read(&b_port)); // Link is ready to receive

    // keep test thread alive long enough to deliver events
    std::thread::sleep(core::time::Duration::from_millis(250));
    a_port_ctrl.send(VerifyEvent);
    b_port_ctrl.send(VerifyEvent);

    // keep test thread alive long enough verify mock(s)
    std::thread::sleep(core::time::Duration::from_millis(50));
}

#[test]
fn exactly_once_in_order_ait_cell_to_cell() {
    pub const N_END: u8 = 0x03;

    #[derive(Debug, Clone)]
    pub struct VerifyEvent; // verify mock

    #[derive(Debug, Clone)]
    pub enum CellMockEvent {
        // aggregate event type
        Mock(CellEvent),
        Ctrl(VerifyEvent),
    }

    pub struct CellMock {
        myself: Option<Cap<CellEvent>>,
        hub: Cap<HubEvent>,
        n_send: u8,
        n_recv: u8,
        in_order: bool,
    }
    impl CellMock {
        pub fn create(hub: &Cap<HubEvent>) -> Cap<CellMockEvent> {
            actor::create(CellMock {
                myself: None,
                hub: hub.clone(),
                n_send: 0,
                n_recv: 0,
                in_order: true,
            })
        }
    }
    impl Actor for CellMock {
        type Event = CellMockEvent;

        fn on_event(&mut self, event: Self::Event) {
            match &event {
                CellMockEvent::Mock(cell_event) => match &cell_event {
                    CellEvent::Init(myself) => match &self.myself {
                        None => {
                            self.myself = Some(myself.clone());
                        },
                        Some(_) => panic!("Cell::cell already set"),
                    },
                    CellEvent::HubToCellWrite(payload) => {
                        if let Some(myself) = &self.myself {
                            self.n_recv += 1;
                            if payload.data[0] != self.n_recv {
                                self.in_order = false;
                            }
                            self.hub.send(HubEvent::new_cell_to_hub_read(&myself));
                        }
                    },
                    CellEvent::HubToCellRead => {
                        if let Some(myself) = &self.myself {
                            if self.n_send < N_END {
                                self.n_send += 1;
                                let tree_id = TreeId::new(144); // FIXME: fake TreeId
                                let data = [self.n_send; frame::PAYLOAD_SIZE];
                                let payload = Payload::new(&tree_id, &data);
                                self.hub.send(HubEvent::new_cell_to_hub_write(&myself, &payload));
                            }
                        }
                    },
                },
                CellMockEvent::Ctrl(_verify_event) => {
                    println!("VERIFYING...");
                    assert!(self.myself.is_some());
                    assert_eq!(N_END, self.n_send);
                    assert_eq!(N_END, self.n_recv);
                    assert_eq!(true, self.in_order);
                }
            }
        }
    }

    struct CellMockFacet {
        mock: Cap<CellMockEvent>,
    }
    impl CellMockFacet {
        pub fn create(mock: &Cap<CellMockEvent>) -> Cap<CellEvent> {
            let cell = actor::create(CellMockFacet { mock: mock.clone() });
            cell.send(CellEvent::new_init(&cell));
            cell
        }
    }
    impl Actor for CellMockFacet {
        type Event = CellEvent;

        fn on_event(&mut self, event: Self::Event) {
            self.mock.send(CellMockEvent::Mock(event));
        }
    }

    struct CellCtrlFacet {
        mock: Cap<CellMockEvent>,
    }
    impl CellCtrlFacet {
        pub fn create(mock: &Cap<CellMockEvent>) -> Cap<VerifyEvent> {
            actor::create(CellCtrlFacet { mock: mock.clone() })
        }
    }
    impl Actor for CellCtrlFacet {
        type Event = VerifyEvent;

        fn on_event(&mut self, event: Self::Event) {
            self.mock.send(CellMockEvent::Ctrl(event));
        }
    }

    let (a_to_b_tx, a_to_b_rx) = channel::<Frame>();
    let (b_to_a_tx, b_to_a_rx) = channel::<Frame>();

    // Alice
    let a_wire = Wire::create(&a_to_b_tx, &b_to_a_rx);
    let a_nonce = 12345;
    let a_link = Link::create(&a_wire, a_nonce);
    a_wire.send(WireEvent::new_listen(&a_link)); // start listening
    let a_port = Port::create(&a_link);
    let a_hub = Hub::create(&[a_port.clone()]);
    let a_cell_mock = CellMock::create(&a_hub);
    let a_cell_ctrl = CellCtrlFacet::create(&a_cell_mock);
    let a_cell = CellMockFacet::create(&a_cell_mock);
    a_cell.send(CellEvent::new_hub_to_cell_read()); // Hub ready to receive
    a_hub.send(HubEvent::new_cell_to_hub_read(&a_cell)); // Cell ready to receive

    // Bob
    let b_wire = Wire::create(&b_to_a_tx, &a_to_b_rx);
    let b_nonce = 67890;
    let b_link = Link::create(&b_wire, b_nonce);
    b_wire.send(WireEvent::new_listen(&b_link)); // start listening
    let b_port = Port::create(&b_link);
    let b_hub = Hub::create(&[b_port.clone()]);
    let b_cell_mock = CellMock::create(&b_hub);
    let b_cell_ctrl = CellCtrlFacet::create(&b_cell_mock);
    let b_cell = CellMockFacet::create(&b_cell_mock);
    b_cell.send(CellEvent::new_hub_to_cell_read()); // Hub ready to receive
    b_hub.send(HubEvent::new_cell_to_hub_read(&b_cell)); // Cell ready to receive

    // keep test thread alive long enough to deliver events
    std::thread::sleep(core::time::Duration::from_millis(250));
    a_cell_ctrl.send(VerifyEvent);
    b_cell_ctrl.send(VerifyEvent);

    // keep test thread alive long enough verify mock(s)
    std::thread::sleep(core::time::Duration::from_millis(50));
}

#[test]
fn detect_link_failure_by_harvesting_events() {
    #[derive(Debug, Clone)]
    pub enum LogEvent {
        LinkStatus(LinkState, isize),
        UnexpectedEvent,
    }
    impl LogEvent {
        pub fn new_link_status(state: &LinkState, balance: &isize) -> LogEvent {
            LogEvent::LinkStatus(state.clone(), balance.clone())
        }
        pub fn new_unexpected_event() -> LogEvent {
            LogEvent::UnexpectedEvent
        }
    }
    pub struct FakeLog {
        id: isize,
    }
    impl FakeLog {
        pub fn create(id: isize) -> Cap<LogEvent> {
            actor::create(FakeLog { id })
        }
    }
    impl Actor for FakeLog {
        type Event = LogEvent;

        fn on_event(&mut self, event: Self::Event) {
            println!("Log[{}]::event {:?}", self.id, event);
        }
    }

    pub struct FakePort {
        log: Cap<LogEvent>,
    }
    impl FakePort {
        pub fn create(log: &Cap<LogEvent>) -> Cap<PortEvent> {
            actor::create(FakePort { log: log.clone() })
        }
    }
    impl Actor for FakePort {
        type Event = PortEvent;

        fn on_event(&mut self, event: Self::Event) {
            match &event {
                PortEvent::LinkStatus(state, balance) => {
                    //println!("Port::LinkStatus state={:?}, balance={}", state, balance);
                    self.log.send(LogEvent::new_link_status(state, balance));
                }
                _ => {
                    self.log.send(LogEvent::new_unexpected_event());
                }
            }
        }
    }

    let (a_to_b_tx, a_to_b_rx) = channel::<Frame>();
    let (b_to_a_tx, b_to_a_rx) = channel::<Frame>();

    // Alice
    let a_wire = Wire::create(&a_to_b_tx, &b_to_a_rx);
    let a_nonce = 12345;
    let a_link = Link::create(&a_wire, a_nonce);
    a_wire.send(WireEvent::new_listen(&a_link)); // start listening
    let a_log = FakeLog::create(0);
    let a_port = FakePort::create(&a_log);
    a_link.send(LinkEvent::new_poll(&a_port)); // poll for link status
    a_link.send(LinkEvent::new_start(&a_port)); // start link

    // Bob
    let b_wire = Wire::create(&b_to_a_tx, &a_to_b_rx);
    let b_nonce = 67890;
    let b_link = Link::create(&b_wire, b_nonce);
    b_wire.send(WireEvent::new_listen(&b_link)); // start listening
    let b_log = FakeLog::create(1);
    let b_port = FakePort::create(&b_log);
    b_link.send(LinkEvent::new_poll(&b_port)); // poll for link status
    b_link.send(LinkEvent::new_start(&b_port)); // start link

    // wait for entanglement to be established
    std::thread::sleep(core::time::Duration::from_millis(30));
    a_link.send(LinkEvent::new_poll(&a_port));
    b_link.send(LinkEvent::new_poll(&b_port));
    a_link.send(LinkEvent::new_poll(&a_port));
    b_link.send(LinkEvent::new_poll(&b_port));

    // wait before checking again...
    std::thread::sleep(core::time::Duration::from_millis(10));
    a_link.send(LinkEvent::new_poll(&a_port));
    b_link.send(LinkEvent::new_poll(&b_port));

    // wait before stopping link...
    std::thread::sleep(core::time::Duration::from_millis(10));
    a_link.send(LinkEvent::new_stop(&a_port));
    //b_link.send(LinkEvent::new_stop(&b_port)); // NOTE: stopping one side should freeze the other
    b_link.send(LinkEvent::new_poll(&b_port));

    // wait before checking again...
    std::thread::sleep(core::time::Duration::from_millis(10));
    a_link.send(LinkEvent::new_poll(&a_port));
    b_link.send(LinkEvent::new_poll(&b_port));

    // wait to see if link recovers...
    std::thread::sleep(core::time::Duration::from_millis(30));
    a_link.send(LinkEvent::new_poll(&a_port));
    b_link.send(LinkEvent::new_poll(&b_port));

    // keep test thread alive long enough for verification
    std::thread::sleep(core::time::Duration::from_millis(50));
}
