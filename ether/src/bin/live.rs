use pretty_hex::pretty_hex;
use std::env;

mod wire {
    use pnet::datalink::{
        self, Channel::Ethernet, DataLinkReceiver, DataLinkSender, NetworkInterface,
    };
    use pretty_hex::pretty_hex;
    use rand::Rng;

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

        pub fn send_init_frame(&mut self) {
            // Construct and send a single packet.
            let frame = b"\
                \xFF\xFF\xFF\xFF\xFF\xFF\
                \x00\x00\x00\x00\x00\x00\
                \x88\xB5\
                \xF0\xF0\
                \x20\x20\x20\x20\x20\x20\x20\x20\
                \x20\x20\x20\x20\x20\x20\x20\x20\
                \x20\x20\x20\x20\x20\x20\x20\x20\
                \x20\x20\x20\x20\x20\x20\x20\x20\
                \x20\x20\x20\x20\x20\x20\x20\x20\
                \x20\x20\x20\x20";
            let mut buffer = [0x20_u8; 60];
            buffer.copy_from_slice(frame);
            let nonce: u32 = rand::thread_rng().gen();
            buffer[8..12].copy_from_slice(&nonce.to_le_bytes());
            buffer[2..6].copy_from_slice(&0x12345678_u32.to_be_bytes());
            println!("SEND_INIT {}", pretty_hex(&buffer));
            self.tx.send_to(&buffer, None);
        }

        pub fn recv_frame(&mut self) -> Result<&[u8], std::io::Error> {
            self.rx.next()
        }
    }
}
use wire::Wire;

pub fn on_frame_recv(frame: &[u8]) {
    println!("FRAME_RECV {}", pretty_hex(&frame));
}

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
    let mut wire = Wire::new(&interface_name);

    // Start protocol
    wire.send_init_frame();

    // Listen loop...
    loop {
        println!("LOOP");
        match wire.recv_frame() {
            Ok(frame) => {
                on_frame_recv(&frame);
            }
            Err(e) => {
                // If an error occurs, we can handle it here
                panic!("An error occurred while reading: {}", e);
            }
        }
    }

    //    println!("EXIT");
}
