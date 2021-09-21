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
use ether::link::{Link, LinkBeh};
use ether::wire::{Wire, WireBeh};

fn sim_ait() {
    println!("SIM_AIT");

    let (inbound_tx, inbound_rx) = channel::<[u8; 60]>();
    let (outbound_tx, outbound_rx) = channel::<[u8; 60]>();

    thread::spawn(move || {
        run_reactor(outbound_tx, inbound_rx);
    });

    thread::spawn(move || {
        run_reactor(inbound_tx, outbound_rx);
    });
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

    let (inbound_tx, inbound_rx) = channel::<[u8; 60]>();
    let (outbound_tx, outbound_rx) = channel::<[u8; 60]>();

    thread::spawn(move || {
        loop {
            match ether_rx.next() {
                Ok(raw_data) => {
                    let data = raw_data.try_into().expect("Bad frame size");
                    println!("ETHER_RECV {}", pretty_hex(&data));
                    inbound_tx.send(data).expect("Send failed on channel");
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
            match outbound_rx.recv() {
                Ok(data) => {
                    println!("ETHER_SEND {}", pretty_hex(&data));
                    ether_tx.send_to(&data, None);
                }
                Err(e) => {
                    // If an error occurs, we can handle it here
                    panic!("Channel read failed: {}", e);
                }
            }
        }
    });

    thread::spawn(move || {
        run_reactor(outbound_tx, inbound_rx);
    });
}

fn run_reactor(tx: Sender<[u8; 60]>, rx: Receiver<[u8; 60]>) {
    struct Boot {
        tx: Sender<[u8; 60]>,
        rx: Receiver<[u8; 60]>,
    }
    impl Boot {
        pub fn new(tx: Sender<[u8; 60]>, rx: Receiver<[u8; 60]>) -> Box<dyn Behavior> {
            Box::new(Boot { tx, rx })
        }
    }
    impl Behavior for Boot {
        fn react(&self, _event: Event) -> Result<Effect, Error> {
            let mut effect = Effect::new();

            let nonce = rand::thread_rng().gen();
            let wire = effect.create(WireBoot::new(nonce, self.tx.clone(), self.rx.clone()));
            let link = effect.create(LinkBeh::new(&wire, nonce, 0));
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
                    let mut reply = Frame::default();
                    reply.set_reset(); // send INIT
                    reply.set_tree_id(self.nonce);
                    effect.send(&event.target, Message::Frame(reply.data));
                    effect.send(&event.target, Message::Empty); // start polling
                    Ok(effect)
                }
                _ => Err("unknown message: expected Addr(link)"),
            }
        }
    }

    let mut config = Config::new();
    config.boot(Boot::new(tx, rx));
    loop {
        let _pending = config.dispatch(100);
        //println!("ACTOR DISPATCH (100), pending={}", pending);
    }
}

fn _liveness(if_name: &str) {
    // this version is just a refactor of `live.rs`
    // to be removed once the actor-based version works
    println!("LIVENESS");

    let wire = Wire::new(&if_name);
    let mut link = Link::new(wire);

    // Listen loop...
    link.event_loop();
}

// Implement link liveness and AIT protocols
//
// Invoke as: ait <interface name>
fn main() {
    match env::args().nth(1) {
        None => sim_ait(),
        Some(name) => live_ait(&name),
    };

    // MAIN THREAD
    let mut line = String::new();
    std::io::stdin()
        .read_line(&mut line)
        .expect("read_line() failed");
    println!("line={}", line);
}
