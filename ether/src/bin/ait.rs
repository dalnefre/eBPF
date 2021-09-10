use std::env;
use std::sync::mpsc::channel;
use std::thread;

//use ether::reactor::*;
use ether::link::Link;
use ether::wire::Wire;

fn async_io() {
    println!("AIT");

    // Create a simple streaming channel
    let (inbound_tx, inbound_rx) = channel::<&[u8]>();
    let (outbound_tx, outbound_rx) = channel::<&[u8]>();
    thread::spawn(move || {
        let outbound_msg = "bar".as_bytes();
        outbound_tx.send(outbound_msg).unwrap();
        println!("inbound: {:?}", inbound_rx.recv().unwrap());
    });
    println!("outbound: {:?}", outbound_rx.recv().unwrap());
    let inbound_msg = "foo".as_bytes();
    inbound_tx.send(inbound_msg).unwrap();
}

fn liveness(if_name: &str) {
    println!("AIT");

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
        None => async_io(),
        Some(name) => liveness(&name),
    };
}
