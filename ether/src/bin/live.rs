use std::env;

/*** Ethernet Frame Format

  0                   1                   2                   3
  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  32  64 128
 |R 1 1 1 1 1 X M 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1|   0   0   0
 +   MAC destination = 0xffffff  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1|S 1 1 1 1 1 Y N 0 0 0 0 0 0 0 0|   1   .   .
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+     MAC source = 0xf0????     +
 |                                         (Tree ID)             |   2   1   .
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |      Ethertype = 0x88b5       |         Protocol Bits         |   3   .   .
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+---------------+---------------+
 |                                                               |   4   2   1
 +               +               +               +               +
 |                                                               |   5   .   .
 +               +               +               +               +
 |                                                               |   6   3   .
 +               +               +               +               +
 |                                                               |   7   .   .
 +               +               +               +               +
 |                                                               |   8   4   2
 +               +               +               +               +
 |                     Payload (44 octets)                       |   9   .   .
 +               +               +               +               +
 |                                                               |  10   5   .
 +               +               +               +               +
 |                                                               |  11   .   .
 +               +               +               +               +
 |                                                               |  12   6   3
 +               +               +               +               +
 |                                                               |  13   .   .
 +               +               +               +               +
 |                                                               |  14   7   .
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                     Frame check sequence                      |  15   .   .
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Protocol Bits: { 00 = TICK, 01 = TECK, 10 = ~TECK, 11 = TACK }
R/S: { 0 = Reset, 1 = Entangled }
X/Y: { 0 = Unicast, 1 = Broadcast }
M/N: { 0 = Global, 1 = Local Admin }

***/

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

        pub fn send_reset_frame(&mut self, nonce: u32) {
            // Construct and send a reset packet.
            let header = b"\
                \x7F\xFF\xFF\xFF\xFF\xFF\
                \x7F\x00\x00\x00\x00\x00\
                \x88\xB5\
                \x00\x00";
            let mut buffer = [0x20_u8; 60];
            buffer[0..16].copy_from_slice(header);
            buffer[8..12].copy_from_slice(&nonce.to_be_bytes());
            println!("SEND_RESET {}", pretty_hex(&buffer));
            self.tx.send_to(&buffer, None);
        }

        pub fn send_proto_frame(&mut self, tree_id: u32, i: u8, u: u8) {
            // Construct and send a protocol packet.
            let header = b"\
                \xFF\xFF\xFF\xFF\xFF\xFF\
                \xFF\x00\x00\x00\x00\x00\
                \x88\xB5\
                \x00\x00";
            let mut buffer = [0x20_u8; 60];
            buffer[0..16].copy_from_slice(header);
            buffer[8..12].copy_from_slice(&tree_id.to_be_bytes());
            buffer[14] = i;
            buffer[15] = u;
            println!("SEND_PROTO {}", pretty_hex(&buffer));
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

        pub fn recv_frame(&mut self) -> &[u8] {
            match self.wire.recv_frame() {
                Ok(frame) => frame,
                Err(e) => {
                    // If an error occurs, we can handle it here
                    panic!("An error occurred while reading: {}", e);
                }
            }
        }

        pub fn send_reset(&mut self) {
            self.wire.send_reset_frame(self.nonce);
        }

        pub fn send_proto(&mut self, i: u8, u: u8) {
            self.wire.send_proto_frame(self.nonce, i, u);
        }

        pub fn event_loop(&mut self) {
            self.send_reset(); // Start protocol
            loop {
                println!("LOOP");
                let mut frame = [0; 60];
                frame.copy_from_slice(self.recv_frame());
                self.on_recv(&frame);
            }
        }

        pub fn on_recv(&mut self, frame: &[u8]) {
            println!("LINK_RECV {}", pretty_hex(&frame));
            if (frame[12] != 0x88) || (frame[13] != 0xB5) {
                panic!("Expected ethertype 0x88B5");
            }
            let reset = (frame[6] & 0x80) == 0x00;
            if reset {
                // init/reset protocol
                println!("init/reset protocol {}", reset);
                let mut tree_id = [0; 4];
                tree_id.copy_from_slice(&frame[8..12]);
                let other = u32::from_be_bytes(tree_id);
                println!("nonce {}, other {}", self.nonce, other);
            } else {
                // entangled protocol
                let i_state = frame[14];
                let u_state = frame[15];
                println!("entangled protocol {}, i={}, u={}", reset, i_state, u_state);
                self.send_proto(u_state, i_state);
            }
        }
    }
}
use link::Link;

/*** Reference Implementation in Humus

# The _link_ is modeled as two separate endpoints,
# one in each node, connected by a _wire_.
# Each endpoint has:
#   * a nonce (for symmetry breaking)
#   * a liveness flag
#   * an AIT buffer (reader, writer, out_pkt)
#   * an information balance counter
LET link_beh(wire, nonce, live, ait, xfer) = \msg.[
    CASE msg OF
    (cust, #poll) : [
        SEND (SELF, nonce, live, ait, xfer) TO cust
        BECOME link_beh(wire, nonce, FALSE, ait, xfer)
    ]
    ($wire, #TICK) : [
        CASE ait OF
        (_, ?, _) : [  # entangled liveness
            SEND (SELF, #TICK) TO wire
            BECOME link_beh(wire, nonce, TRUE, ait, 0)
        ]
        (_, _, out_pkt) : [  # initiate AIT
            SEND (SELF, #TECK, out_pkt) TO wire
            BECOME link_beh(wire, nonce, TRUE, ait, -1)
        ]
        END
    ]
    ($wire, #TECK, in_pkt) : [
        CASE ait OF
        (?, _, _) : [  # no reader, reject AIT
            SEND (#LINK, SELF, #REVERSE, msg, ait) TO println
            SEND (SELF, #~TECK, in_pkt) TO wire
            BECOME link_beh(wire, nonce, TRUE, ait, xfer)
        ]
        (r, w, out_pkt) : [  # deliver AIT received
            SEND (SELF, #TACK, in_pkt) TO wire
            SEND (SELF, #write, in_pkt) TO r
            BECOME link_beh(wire, nonce, TRUE, (?, w, out_pkt), 1)
        ]
        END
    ]
    ($wire, #TACK, _) : [
        LET (r, w, _) = $ait
        SEND (SELF, #TICK) TO wire
        SEND (SELF, #read) TO w
        BECOME link_beh(wire, nonce, TRUE, (r, ?, ?), 0)
    ]
    ($wire, #~TECK, _) : [
        SEND (SELF, #TICK) TO wire
        BECOME link_beh(wire, nonce, TRUE, ait, 0)
    ]
    (cust, #read) : [
        CASE ait OF
        (?, w, out_pkt) : [
            BECOME link_beh(wire, nonce, live, (cust, w, out_pkt), xfer)
        ]
        _ : [ THROW (#Unexpected, NOW, SELF, msg, live, ait) ]
        END
    ]
    (cust, #write, out_pkt) : [
        CASE ait OF
        (r, ?, _) : [
            BECOME link_beh(wire, nonce, live, (r, cust, out_pkt), xfer)
        ]
        _ : [ THROW (#Unexpected, NOW, SELF, msg, live, ait) ]
        END
    ]
    ($wire, #INIT, nonce') : [
        CASE compare(nonce, nonce') OF
        1 : [  # entangle link
            SEND (NOW, SELF, #entangle) TO println
            SEND (SELF, #TICK) TO wire
        ]
        -1 : [  # ignore (wait for other endpoint to entangle)
            SEND (NOW, SELF, #waiting) TO println
        ]
        _ : [  # error! re-key...
            SEND (NOW, SELF, #re-key) TO println
            SEND (SELF, nonce_limit) TO random
        ]
        END
    ]
    (_, _) : [ THROW (#Unexpected, NOW, SELF, msg) ]
    nonce' : [
        BECOME link_beh(wire, nonce', live, ait, xfer)
        SEND (SELF, #INIT, nonce') TO wire
    ]
    END
]

***/

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

    // Listen loop...
    link.event_loop();
}
