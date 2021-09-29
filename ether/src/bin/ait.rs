use crossbeam::crossbeam_channel::unbounded as channel;
use crossbeam::crossbeam_channel::{Receiver, Sender};
use std::convert::TryInto;
use std::env;
use std::thread;

use ether::reactor::{Behavior, Config, Effect, Error, Event, Message};
use pnet::datalink::{self, Channel::Ethernet, NetworkInterface};
use pretty_hex::pretty_hex;
use rand::Rng;
extern crate alloc;
//use alloc::boxed::Box;
use alloc::rc::Rc;

use ether::frame::Frame;
use ether::link::LinkBeh;
use ether::port::Port;
use ether::wire::WireBeh;

fn insert_payload(tx: &Sender<[u8; 44]>, s: &str) {
    assert!(s.len() <= 44);
    let mut buf = [0_u8; 44];
    buf[..s.len()].copy_from_slice(&s.as_bytes());
    tx.send(buf).expect("insert_payload failed");
}

fn monitor_node_out(tx: &Sender<[u8; 44]>) {
    loop {
        let mut line = String::new();
        std::io::stdin()
            .read_line(&mut line)
            .expect("read_line() failed");
        println!("line={:?}", line);
        insert_payload(tx, &line);
    }
}

fn monitor_node_in(rx: &Receiver<[u8; 44]>) {
    loop {
        thread::sleep(std::time::Duration::from_micros(500));
        match rx.recv() {
            Ok(data) => {
                println!("Node::in {}", pretty_hex(&data));
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
        let (in_port_tx, in_port_rx) = channel::<[u8; 44]>();
        let (out_port_tx, out_port_rx) = channel::<[u8; 44]>();
        insert_payload(&out_port_tx, "Uno");
        insert_payload(&out_port_tx, "Dos");
        insert_payload(&out_port_tx, "Tres");
        thread::spawn(move || {
            monitor_node_in(&in_port_rx);
        });
        let port = Port::new(in_port_tx, out_port_rx);
        run_reactor(port, out_wire_tx, in_wire_rx);
    });

    thread::spawn(move || {
        let (in_port_tx, in_port_rx) = channel::<[u8; 44]>();
        let (out_port_tx, out_port_rx) = channel::<[u8; 44]>();
        insert_payload(&out_port_tx, "One");
        insert_payload(&out_port_tx, "Two");
        insert_payload(&out_port_tx, "Three");
        insert_payload(&out_port_tx, "Four");
        insert_payload(&out_port_tx, "Five");
        thread::spawn(move || {
            monitor_node_in(&in_port_rx);
        });
        let port = Port::new(in_port_tx, out_port_rx);
        run_reactor(port, in_wire_tx, out_wire_rx);
    });

    // fail-safe exit after timeout
    let delay = std::time::Duration::from_millis(3_000);
    //let delay = std::time::Duration::from_millis(3);
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

    // Run ReActor in Main thread...
    //thread::spawn(move || {
    let (in_port_tx, in_port_rx) = channel::<[u8; 44]>();
    let (out_port_tx, out_port_rx) = channel::<[u8; 44]>();
    thread::spawn(move || {
        monitor_node_in(&in_port_rx);
    });
    thread::spawn(move || {
        monitor_node_out(&out_port_tx);
    });
    let port = Port::new(in_port_tx, out_port_rx);
    run_reactor(port, out_wire_tx, in_wire_rx);
    //});
}

fn run_reactor(port: Port, tx: Sender<[u8; 60]>, rx: Receiver<[u8; 60]>) {
    struct Boot {
        port: Port,
        tx: Sender<[u8; 60]>,
        rx: Receiver<[u8; 60]>,
    }
    impl Boot {
        pub fn new(port: Port, tx: Sender<[u8; 60]>, rx: Receiver<[u8; 60]>) -> Box<dyn Behavior> {
            Box::new(Boot { port, tx, rx })
        }
    }
    impl Behavior for Boot {
        fn react(&self, _event: Event) -> Result<Effect, Error> {
            let mut effect = Effect::new();

            let nonce = rand::thread_rng().gen();
            let wire = effect.create(WireBoot::new(nonce, self.tx.clone(), self.rx.clone()));
            let link = effect.create(LinkBeh::new(self.port.clone(), &wire, nonce, 0));
            effect.send(&wire, Message::Addr(Rc::clone(&link)));

            Ok(effect)
        }
    }
    struct WireBoot {
        nonce: u32,
        tx: Sender<[u8; 60]>,
        rx: Receiver<[u8; 60]>,
    }
    impl WireBoot {
        pub fn new(nonce: u32, tx: Sender<[u8; 60]>, rx: Receiver<[u8; 60]>) -> Box<dyn Behavior> {
            Box::new(WireBoot { nonce, tx, rx })
        }
    }
    impl Behavior for WireBoot {
        fn react(&self, event: Event) -> Result<Effect, Error> {
            let mut effect = Effect::new();
            match event.message {
                Message::Addr(link) => {
                    effect.update(WireBeh::new(&link, self.tx.clone(), self.rx.clone()))?;
                    let reply = Frame::reset(self.nonce);
                    effect.send(&event.target, Message::Frame(reply.data));
                    effect.send(&event.target, Message::Empty); // start polling
                    Ok(effect)
                }
                _ => Err("unknown message: expected Addr(link)"),
            }
        }
    }

    let mut config = Config::new();
    config.boot(Boot::new(port, tx, rx));
    loop {
        let _pending = config.dispatch(100);
        //println!("ACTOR DISPATCH (100), pending={}", pending);
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
