use pnet::datalink::Channel::Ethernet;
use pnet::datalink::{self, NetworkInterface};
use pnet::packet::ethernet::EthernetPacket;
use pnet::packet::Packet;
use pretty_hex::pretty_hex;

use std::env;

// Invoke as: client <interface name>
fn main() {
    println!("CLIENT");
    let interface_name = match env::args().nth(1) {
        None => {
            println!("usage: client interface");
            return ()
        }
        Some(name) => name,
    };
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
    let (mut tx, mut rx) = match datalink::channel(&interface, Default::default()) {
        Ok(Ethernet(tx, rx)) => (tx, rx),
        Ok(_) => panic!("Unhandled channel type"),
        Err(e) => panic!(
            "An error occurred when creating the datalink channel: {}",
            e
        ),
    };

    // Construct and send a single packet.
    println!("SEND");
    let frame = b"\
        \x7F\xFF\xFF\xFF\xFF\xFF\
        \x7F\x00\x12\x34\x56\x78\
        \x88\xB5\
        \x00\xFF\
        \x20\x20\x20\x20\x20\x20\x20\x20\
        \x20\x20\x20\x20\x20\x20\x20\x20\
        \x20\x20\x20\x20\x20\x20\x20\x20\
        \x20\x20\x20\x20\x20\x20\x20\x20\
        \x20\x20\x20\x20\x20\x20\x20\x20\
        \x20\x20\x20\x20";
    println!("HEXDUMP {}", pretty_hex(&frame));
    let packet = EthernetPacket::new(frame).expect("bad packet");
    println!("packet={:?}", packet);
    tx.send_to(packet.packet(), None);

    // Listen for reply...
    println!("LISTEN");
    match rx.next() {
        Ok(frame) => {
            println!("HEXDUMP {}", pretty_hex(&frame));
        }
        Err(e) => {
            // If an error occurs, we can handle it here
            panic!("An error occurred while reading: {}", e);
        }
    }

    println!("EXIT");
}
