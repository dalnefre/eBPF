use std::env;

mod wire {
    use pnet::datalink::{
        self, Channel::Ethernet, DataLinkReceiver, DataLinkSender, NetworkInterface,
    };
    use pretty_hex::pretty_hex;

    pub struct Wire {
        tx: Box<dyn DataLinkSender>,
        rx: Box<dyn DataLinkReceiver>,
    }

    impl Wire {
        pub fn new(interface_name: &str) -> Wire {
            let interface_names_match = |iface: &NetworkInterface| iface.name == interface_name;

            // Find the network interface with the provided name
            println!("INTERFACE {}", interface_name);
            let interfaces = datalink::interfaces();
            let interface = interfaces
                .into_iter()
                .filter(interface_names_match)
                .next()
                .unwrap();

            // Create a new channel, dealing with layer 2 packets
            println!("CHANNEL");
            let (tx, rx) = match datalink::channel(&interface, Default::default()) {
                Ok(Ethernet(tx, rx)) => (tx, rx),
                Ok(_) => panic!("Unhandled channel type"),
                Err(e) => panic!(
                    "An error occurred when creating the datalink channel: {}",
                    e
                ),
            };
            Wire { tx, rx }
        }

        pub fn send_init_frame(&mut self, nonce: u32) {
            // Construct and send a single packet.
            let header = b"\
                \x7F\xFF\xFF\xFF\xFF\xFF\
                \x7F\x00\x00\x00\x00\x00\
                \x88\xB5\
                \x00\x00";
            let mut buffer = [0x20_u8; 60];
            buffer[0..16].copy_from_slice(header);
            buffer[8..12].copy_from_slice(&nonce.to_be_bytes());
            println!("SEND_INIT {}", pretty_hex(&buffer));
            self.tx.send_to(&buffer, None);
        }

        pub fn recv_frame(&mut self) -> Result<&[u8], std::io::Error> {
            self.rx.next()
        }
    }
}
use wire::Wire;

mod link {
    use crate::wire::Wire;
    use pretty_hex::pretty_hex;
    use rand::Rng;

    pub struct Link {
        wire: Wire,
        nonce: u32,
    }

    impl Link {
        pub fn new(wire: Wire) -> Link {
            let nonce: u32 = rand::thread_rng().gen();
            Link { wire, nonce }
        }

        pub fn send_init(&mut self) {
            self.wire.send_init_frame(self.nonce);
        }

        pub fn event_loop(&mut self) {
            loop {
                println!("LOOP");
                match self.wire.recv_frame() {
                    Ok(frame) => {
                        self.on_recv(&frame);
                    }
                    Err(e) => {
                        // If an error occurs, we can handle it here
                        panic!("An error occurred while reading: {}", e);
                    }
                }
            }
        }

        pub fn on_recv(&mut self, frame: &[u8]) {
            println!("LINK_RECV {}", pretty_hex(&frame));
            let proto = frame[6];
            if proto == 0x7F {  // init/reset protocol
                println!("init/reset protocol {}", proto);
                let mut tree_id = [0; 4];
                tree_id.copy_from_slice(&frame[8..12]);
                let other = u32::from_be_bytes(tree_id);
                println!("nonce {}, other {}", self.nonce, other);
            } else if proto == 0xFF {  // entangled protocol
                println!("entangled protocol {}", proto);
            } else {
                panic!("Unexpected protocol {}", proto);
            }
        }
    }
}
use link::Link;

// Implement link liveness protocol
//
// Invoke as: live <interface name>
fn main() {
    println!("LIVE");
    let interface_name = match env::args().nth(1) {
        None => {
            println!("usage: server interface");
            std::process::exit(1);
        }
        Some(name) => name,
    };
    let wire = Wire::new(&interface_name);
    let mut link = Link::new(wire);

    // Start protocol
    link.send_init();

    // Listen loop...
    link.event_loop();
}
