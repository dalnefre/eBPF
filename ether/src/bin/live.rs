use std::env;

mod wire {
    use ether::frame::Frame;
    use pnet::datalink::{
        self, Channel::Ethernet, DataLinkReceiver, DataLinkSender, NetworkInterface,
    };
    use pretty_hex::pretty_hex;

    pub struct Wire {
        tx: Box<dyn DataLinkSender>,
        rx: Box<dyn DataLinkReceiver>,
        cnt: u16,
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
            let cnt = 0;
            Wire { tx, rx, cnt }
        }

        pub fn send_reset_frame(&mut self, nonce: u32) {
            // Construct and send a reset packet.
            let frame = Frame::new_reset(nonce);
            println!("SEND_RESET {}", pretty_hex(&frame.data));
            self.tx.send_to(&frame.data, None);
        }

        pub fn send_proto_frame(&mut self, i: u8, u: u8) {
            // Construct and send a protocol packet.
            let frame = Frame::new_entangled(0, i, u);
            println!("SEND_PROTO {}", pretty_hex(&frame.data));
            self.tx.send_to(&frame.data, None);
        }

        pub fn recv_frame(&mut self) -> Option<Frame> {
            self.cnt += 1;
            if self.cnt > 5 {
                println!("Recv limit reached");
                None
            } else {
                match self.rx.next() {
                    Ok(data) => Some(Frame::new(data)),
                    Err(e) => {
                        println!("Recv error: {}", e);
                        None
                    }
                }
            }
        }
    }
}
use wire::Wire;

mod link {
    use crate::wire::Wire;
    use ether::frame::{self, Frame};
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

        pub fn recv_frame(&mut self) -> Option<Frame> {
            self.wire.recv_frame()
        }

        pub fn send_reset(&mut self) {
            self.wire.send_reset_frame(self.nonce);
        }

        pub fn send_proto(&mut self, i: u8, u: u8) {
            self.wire.send_proto_frame(i, u);
        }

        pub fn event_loop(&mut self) {
            self.send_reset(); // Start protocol
            loop {
                println!("LOOP");
                match self.recv_frame() {
                    Some(frame) => self.on_recv(&frame),
                    None => break,
                }
            }
            println!("EXIT");
        }

        pub fn on_recv(&mut self, frame: &Frame) {
            println!("LINK_RECV {}", pretty_hex(&frame.data));
            if frame.is_reset() {
                // init/reset protocol

                println!("init/reset");
                let other = frame.get_nonce();
                println!("nonce {}, other {}", self.nonce, other);

                if self.nonce < other {
                    println!("waiting...");
                } else if self.nonce > other {
                    println!("entangle...");
                    self.send_proto(frame::TICK, frame::TICK); // send TICK
                } else {
                    println!("collision...");
                    self.nonce = rand::thread_rng().gen();
                    self.send_reset(); // re-key and send INIT
                }
            } else if frame.is_entangled() {
                // entangled protocol

                let i_state = frame.get_i_state();
                let u_state = frame.get_u_state();
                println!("entangled (i,u)=({},{})", i_state, u_state);

                if i_state == frame::TICK {
                    // TICK recv'd
                    self.send_proto(frame::TICK, i_state); // send TICK
                } else {
                    println!("freeze...");
                }
            } else {
                println!("invalid frame");
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
            println!("usage: live interface");
            return ();
        }
        Some(name) => name,
    };
    let wire = Wire::new(&interface_name);
    let mut link = Link::new(wire);

    // Listen loop...
    link.event_loop();
}
