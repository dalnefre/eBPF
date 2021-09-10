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

            println!("init/reset = {}", reset);
            let mut tree_id = [0; 4];
            tree_id.copy_from_slice(&frame[8..12]);
            let other = u32::from_be_bytes(tree_id);
            println!("nonce {}, other {}", self.nonce, other);

            if self.nonce < other {
                println!("waiting...");
            } else if self.nonce > other {
                println!("entangle...");
                self.send_proto(0xF0, 0x00); // send TICK
            } else {
                println!("collision...");
                self.nonce = rand::thread_rng().gen();
                self.send_reset(); // re-key and send INIT
            }
        } else {
            // entangled protocol

            let i_state = frame[14];
            let u_state = frame[15];
            println!("entangled (i,u)=({},{})", i_state, u_state);

            if i_state == 0xF0 {
                // TICK recv'd
                self.send_proto(0xF0, i_state); // send TICK
            } else {
                println!("freeze...");
            }
        }
    }
}

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
