use crossbeam::crossbeam_channel::unbounded as channel;
use crossbeam::crossbeam_channel::{Receiver, Sender};
use std::env;
use std::thread;

use pnet::datalink::{self, Channel::Ethernet, NetworkInterface};
use pnet::datalink::{DataLinkReceiver, DataLinkSender};
use pretty_hex::pretty_hex;
use rand::Rng;

use ether::cell::{Cell, CellEvent};
use ether::frame::{self, Frame, Payload, TreeId};
use ether::hub::{Hub, HubEvent};
use ether::link::Link;
use ether::port::Port;
use ether::wire::{FaultyWire, Wire, WireEvent};

fn insert_payload(tx: &Sender<Payload>, s: &str) {
    assert!(s.len() <= frame::PAYLOAD_SIZE);
    let tree_id = TreeId::new(144); // FIXME: fake TreeId
    let mut buf = [0_u8; frame::PAYLOAD_SIZE];
    buf[..s.len()].copy_from_slice(&s.as_bytes());
    let payload = Payload::new(&tree_id, &buf);
    tx.send(payload).expect("insert_payload failed");
}

fn monitor_node_out(tx: &Sender<Payload>) {
    loop {
        let mut line = String::new();
        std::io::stdin()
            .read_line(&mut line)
            .expect("read_line() failed");
        println!("line={:?}", line);
        insert_payload(tx, &line);
    }
}

fn monitor_node_in(label: &str, rx: &Receiver<Payload>) {
    loop {
        // FIXME: introduce a delay here to cause RTECK back-pressure...
        thread::sleep(std::time::Duration::from_millis(150));
        //thread::sleep(std::time::Duration::from_micros(500));
        match rx.recv() {
            Ok(payload) => {
                println!("Node[{}]::in {}", label, pretty_hex(&payload.data));
            }
            Err(e) => {
                panic!("Node[{}]::in ERROR! {}", label, e);
            }
        }
    }
}

fn sim_ait() {
    println!("SIM_AIT");

    let (in_wire0_tx, in_wire0_rx) = channel::<Frame>();
    let (out_wire0_tx, out_wire0_rx) = channel::<Frame>();
    let (in_wire1_tx, in_wire1_rx) = channel::<Frame>();
    let (out_wire1_tx, out_wire1_rx) = channel::<Frame>();

    thread::spawn(move || {
        let (in_cell_tx, in_cell_rx) = channel::<Payload>();
        let (out_cell_tx, out_cell_rx) = channel::<Payload>();
        insert_payload(&out_cell_tx, "Uno");
        insert_payload(&out_cell_tx, "Dos");
        insert_payload(&out_cell_tx, "Tres");
        insert_payload(&out_cell_tx, "Quatro");
        thread::spawn(move || {
            monitor_node_in("alice", &in_cell_rx);
        });
        start_2port_hub(
            12345,
            &in_cell_tx,
            &out_cell_rx,
            &out_wire0_tx,
            &in_wire0_rx,
            &out_wire1_tx,
            &in_wire1_rx,
        );
    });

    thread::spawn(move || {
        let (in_cell_tx, in_cell_rx) = channel::<Payload>();
        let (out_cell_tx, out_cell_rx) = channel::<Payload>();
        insert_payload(&out_cell_tx, "One");
        insert_payload(&out_cell_tx, "Two");
        insert_payload(&out_cell_tx, "Three");
        insert_payload(&out_cell_tx, "Four");
        insert_payload(&out_cell_tx, "Five");
        thread::spawn(move || {
            monitor_node_in("bob", &in_cell_rx);
        });
        start_2port_hub(
            67890,
            &in_cell_tx,
            &out_cell_rx,
            &in_wire0_tx,
            &out_wire0_rx,
            &in_wire1_tx,
            &out_wire1_rx,
        );
    });

    // fail-safe exit after timeout
    let delay = std::time::Duration::from_millis(4_000);
    //let delay = std::time::Duration::from_millis(100);
    thread::sleep(delay);
    println!("Time limit {:?} reached.", delay);
}

fn send_to_ethernet(ether_tx: &mut Box<dyn DataLinkSender>, wire_rx: &Receiver<Frame>) {
    loop {
        match wire_rx.recv() {
            Ok(frame) => {
                //println!("ETHER_SEND {}", pretty_hex(&frame.data));
                ether_tx.send_to(&frame.data, None);
            }
            Err(e) => {
                // If an error occurs, we can handle it here
                panic!("Channel read failed: {}", e);
            }
        }
    }
}

fn recv_from_ethernet(wire_tx: &Sender<Frame>, ether_rx: &mut Box<dyn DataLinkReceiver>) {
    loop {
        match ether_rx.next() {
            Ok(data) => {
                let frame = Frame::new(data);
                //println!("ETHER_RECV {}", pretty_hex(&frame.data));
                wire_tx.send(frame).expect("Send failed on channel");
            }
            Err(e) => {
                // If an error occurs, we can handle it here
                panic!("Ethernet read failed: {}", e);
            }
        }
    }
}

fn live_ait(if_name: &str) {
    println!("LIVE_AIT");

    let if_names_match = |iface: &NetworkInterface| iface.name == if_name;
    // Find the network interface with the provided name
    let interfaces = datalink::interfaces();
    let interface = interfaces
        .into_iter()
        .filter(if_names_match)
        .next()
        .expect(&format!("Bad interface {}", if_name));
    // Create a new channel, dealing with layer 2 packets
    let (mut ether_tx, mut ether_rx) = match datalink::channel(&interface, Default::default()) {
        Ok(Ethernet(tx, rx)) => (tx, rx),
        Ok(_) => panic!("Unhandled channel type"),
        Err(e) => panic!(
            "An error occurred when creating the datalink channel: {}",
            e
        ),
    };

    // Start Ethernet adapter
    let (in_wire_tx, in_wire_rx) = channel::<Frame>();
    let (out_wire_tx, out_wire_rx) = channel::<Frame>();
    thread::spawn(move || {
        send_to_ethernet(&mut ether_tx, &out_wire_rx);
    });
    thread::spawn(move || {
        recv_from_ethernet(&in_wire_tx, &mut ether_rx);
    });

    // Start Node in Main thread...
    let (in_cell_tx, in_cell_rx) = channel::<Payload>();
    let (out_cell_tx, out_cell_rx) = channel::<Payload>();
    thread::spawn(move || {
        monitor_node_in("local", &in_cell_rx);
    });
    thread::spawn(move || {
        monitor_node_out(&out_cell_tx);
    });
    let nonce = rand::thread_rng().gen();
    start_1port_hub(nonce, &in_cell_tx, &out_cell_rx, &out_wire_tx, &in_wire_rx);
}

fn start_1port_hub(
    nonce: u32,
    cell_tx: &Sender<Payload>,
    cell_rx: &Receiver<Payload>,
    wire_tx: &Sender<Frame>,
    wire_rx: &Receiver<Frame>,
) {
    let wire = Wire::create(&wire_tx, &wire_rx);
    /*
    let wire = FaultyWire::create(&wire_tx, &wire_rx, "Three");
    */
    //let wire = FaultyWire::create(&wire_tx, &wire_rx, 17); // drop 17th frame

    let link = Link::create(&wire, nonce);
    wire.send(WireEvent::new_listen(&link)); // start listening on Wire

    let port = Port::create(&link);

    let hub = Hub::create(&[port.clone()]);

    let cell = Cell::create(&hub, &cell_tx, &cell_rx);
    cell.send(CellEvent::new_hub_to_cell_read()); // Hub ready to receive
    hub.send(HubEvent::new_cell_to_hub_read(&cell)); // Cell ready to receive

    loop {
        // FIXME: there is no dispatch loop,
        // but we have to keep this thread alive
        // to avoid killing the actor threads
    }
}

fn start_2port_hub(
    nonce: u32,
    cell_tx: &Sender<Payload>,
    cell_rx: &Receiver<Payload>,
    wire0_tx: &Sender<Frame>,
    wire0_rx: &Receiver<Frame>,
    wire1_tx: &Sender<Frame>,
    wire1_rx: &Receiver<Frame>,
) {
    //let wire0 = Wire::create(&wire0_tx, &wire0_rx); // "reliable" connection
    //let wire0 = FaultyWire::create(&wire0_tx, &wire0_rx, 16); // drop 16th frame (before)
    //let wire0 = FaultyWire::create(&wire0_tx, &wire0_rx, 17); // drop 17th frame (AIT 1)
    let wire0 = FaultyWire::create(&wire0_tx, &wire0_rx, 18); // drop 18th frame (AIT 2)
    //let wire0 = FaultyWire::create(&wire0_tx, &wire0_rx, 19); // drop 19th frame (AIT 3)
    //let wire0 = FaultyWire::create(&wire0_tx, &wire0_rx, 20); // drop 20th frame (AIT 4)
    //let wire0 = FaultyWire::create(&wire0_tx, &wire0_rx, 21); // drop 21st frame (after)
    let wire1 = Wire::create(&wire1_tx, &wire1_rx); // "reliable" connection

    let link0 = Link::create(&wire0, nonce);
    wire0.send(WireEvent::new_listen(&link0)); // start listening on Wire
    let link1 = Link::create(&wire1, nonce);
    wire1.send(WireEvent::new_listen(&link1)); // start listening on Wire

    let port0 = Port::create(&link0);
    let port1 = Port::create(&link1);

    let hub = Hub::create(&[port0.clone(), port1.clone()]);

    let cell = Cell::create(&hub, &cell_tx, &cell_rx);
    cell.send(CellEvent::new_hub_to_cell_read()); // Hub ready to receive
    hub.send(HubEvent::new_cell_to_hub_read(&cell)); // Cell ready to receive

    loop {
        // FIXME: there is no dispatch loop,
        // but we have to keep this thread alive
        // to avoid killing the actor threads
    }
}

// Implement link liveness and AIT protocols
//
// Invoke as: ait <interface name>
fn main() {
    match env::args().nth(1) {
        None => sim_ait(),
        Some(name) => live_ait(&name),
    };
}
