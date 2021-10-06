use crossbeam::crossbeam_channel::unbounded as channel;
use crossbeam::crossbeam_channel::{Receiver, Sender};
use std::convert::TryInto;
use std::env;
use std::thread;

use pnet::datalink::{self, Channel::Ethernet, NetworkInterface};
use pretty_hex::pretty_hex;
use rand::Rng;

use ether::frame::{Frame, Payload};
use ether::link::{Link, LinkEvent};
use ether::port::{Port, PortEvent};
use ether::wire::{Wire, WireEvent};

fn insert_payload(tx: &Sender<Payload>, s: &str) {
    assert!(s.len() <= 44);
    let mut buf = [0_u8; 44];
    buf[..s.len()].copy_from_slice(&s.as_bytes());
    let payload = Payload::new(&buf);
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

fn monitor_node_in(rx: &Receiver<Payload>) {
    loop {
        //thread::sleep(std::time::Duration::from_micros(500));
        thread::sleep(std::time::Duration::from_millis(150));
        match rx.recv() {
            Ok(payload) => {
                println!("Node::in {}", pretty_hex(&payload.data));
            }
            Err(e) => {
                panic!("Node::in ERROR! {}", e);
            }
        }
    }
}

fn sim_ait() {
    println!("SIM_AIT");

    let (in_wire_tx, in_wire_rx) = channel::<[u8; 60]>();
    let (out_wire_tx, out_wire_rx) = channel::<[u8; 60]>();

    thread::spawn(move || {
        let (in_port_tx, in_port_rx) = channel::<Payload>();
        let (out_port_tx, out_port_rx) = channel::<Payload>();
        insert_payload(&out_port_tx, "Uno");
        insert_payload(&out_port_tx, "Dos");
        insert_payload(&out_port_tx, "Tres");
        thread::spawn(move || {
            monitor_node_in(&in_port_rx);
        });
        start_node(&in_port_tx, &out_port_rx, &out_wire_tx, &in_wire_rx);
    });

    thread::spawn(move || {
        let (in_port_tx, in_port_rx) = channel::<Payload>();
        let (out_port_tx, out_port_rx) = channel::<Payload>();
        insert_payload(&out_port_tx, "One");
        insert_payload(&out_port_tx, "Two");
        insert_payload(&out_port_tx, "Three");
        insert_payload(&out_port_tx, "Four");
        insert_payload(&out_port_tx, "Five");
        thread::spawn(move || {
            monitor_node_in(&in_port_rx);
        });
        start_node(&in_port_tx, &out_port_rx, &in_wire_tx, &out_wire_rx);
    });

    // fail-safe exit after timeout
    let delay = std::time::Duration::from_millis(3_000);
    //let delay = std::time::Duration::from_millis(100);
    thread::sleep(delay);
    println!("Time limit {:?} reached.", delay);
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

    let (in_wire_tx, in_wire_rx) = channel::<[u8; 60]>();
    let (out_wire_tx, out_wire_rx) = channel::<[u8; 60]>();

    thread::spawn(move || {
        loop {
            match ether_rx.next() {
                Ok(raw_data) => {
                    let data = raw_data.try_into().expect("Bad frame size");
                    //println!("ETHER_RECV {}", pretty_hex(&data));
                    in_wire_tx.send(data).expect("Send failed on channel");
                }
                Err(e) => {
                    // If an error occurs, we can handle it here
                    panic!("Ethernet read failed: {}", e);
                }
            }
        }
    });

    thread::spawn(move || {
        loop {
            match out_wire_rx.recv() {
                Ok(data) => {
                    //println!("ETHER_SEND {}", pretty_hex(&data));
                    ether_tx.send_to(&data, None);
                }
                Err(e) => {
                    // If an error occurs, we can handle it here
                    panic!("Channel read failed: {}", e);
                }
            }
        }
    });

    // Start Node in Main thread...
    let (in_port_tx, in_port_rx) = channel::<Payload>();
    let (out_port_tx, out_port_rx) = channel::<Payload>();
    thread::spawn(move || {
        monitor_node_in(&in_port_rx);
    });
    thread::spawn(move || {
        monitor_node_out(&out_port_tx);
    });
    start_node(&in_port_tx, &out_port_rx, &out_wire_tx, &in_wire_rx);
}

fn start_node(
    port_tx: &Sender<Payload>,
    port_rx: &Receiver<Payload>,
    wire_tx: &Sender<[u8; 60]>,
    wire_rx: &Receiver<[u8; 60]>,
) {
    let wire = Wire::create(&wire_tx, &wire_rx);
    let nonce = rand::thread_rng().gen();

    let link = Link::create(&wire, nonce);
    wire.send(WireEvent::new_poll(&link, &wire)); // start polling
    let init = Frame::new_reset(nonce);
    wire.send(WireEvent::new_frame(&init)); // send init/reset

    let port = Port::create(&link, &port_tx, &port_rx);
    link.send(LinkEvent::new_read(&port)); // port is ready to receive
    port.send(PortEvent::new_ack_write()); // link is ready to receive

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
