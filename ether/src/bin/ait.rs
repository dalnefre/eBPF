use std::env;
use std::sync::mpsc::channel;
use std::thread;

//use ether::reactor::*;
use ether::link::Link;
use ether::wire::Wire;

fn async_io() {
    println!("AIT");

    // Create a simple streaming channel
    let (tx, rx) = channel::<&[u8]>();
    thread::spawn(move || {
        let msg = "foobar".as_bytes();
        tx.send(msg).unwrap();
    });
    println!("recv: {:?}", rx.recv().unwrap());
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
