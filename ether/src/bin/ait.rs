use std::env;

use ether::wire::Wire;
use ether::link::Link;

// Implement link liveness and AIT protocols
//
// Invoke as: ait <interface name>
fn main() {
    println!("AIT");
    let interface_name = match env::args().nth(1) {
        None => {
            println!("usage: ait interface");
            return ()
        }
        Some(name) => name,
    };
    let wire = Wire::new(&interface_name);
    let mut link = Link::new(wire);

    // Listen loop...
    link.event_loop();
}
