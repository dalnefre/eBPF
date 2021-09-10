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

use pnet::datalink::{self, Channel::Ethernet, DataLinkReceiver, DataLinkSender, NetworkInterface};
use pretty_hex::pretty_hex;
use std::io::{Error, ErrorKind};

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

    pub fn recv_frame(&mut self) -> Result<&[u8], Error> {
        self.cnt += 1;
        if self.cnt > 5 {
            Err(Error::new(ErrorKind::Other, "Recv limit reached"))
        } else {
            self.rx.next()
        }
    }
}