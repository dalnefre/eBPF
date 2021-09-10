use pnet::datalink::Channel::Ethernet;
use pnet::datalink::{self, NetworkInterface};
use pnet::packet::ethernet::{EthernetPacket, MutableEthernetPacket};
use pnet::packet::{MutablePacket, Packet};
use pretty_hex::pretty_hex;

use std::env;

// Invoke as: server <interface name>
fn main() {
    println!("SERVER");
    let interface_name = match env::args().nth(1) {
        None => {
            println!("usage: server interface");
            return ();
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
    println!("LISTEN");
    let (mut tx, mut rx) = match datalink::channel(&interface, Default::default()) {
        Ok(Ethernet(tx, rx)) => (tx, rx),
        Ok(_) => panic!("Unhandled channel type"),
        Err(e) => panic!(
            "An error occurred when creating the datalink channel: {}",
            e
        ),
    };

    loop {
        println!("LOOP");
        match rx.next() {
            Ok(packet) => {
                let packet = EthernetPacket::new(packet).unwrap();

                let frame = packet.packet();
                println!("HEXDUMP {}", pretty_hex(&frame));

                // Constructs a single packet, the same length as the the one received,
                // using the provided closure. This allows the packet to be constructed
                // directly in the write buffer, without copying. If copying is not a
                // problem, you could also use send_to.
                //
                // The packet is sent once the closure has finished executing.
                tx.build_and_send(1, packet.packet().len(), &mut |new_packet| {
                    let mut mut_packet = MutableEthernetPacket::new(new_packet).unwrap();

                    // Create a clone of the original packet
                    mut_packet.clone_from(&packet);

                    // Switch the source and destination
                    mut_packet.set_source(packet.get_destination());
                    mut_packet.set_destination(packet.get_source());
                });
            }
            Err(e) => {
                // If an error occurs, we can handle it here
                panic!("An error occurred while reading: {}", e);
            }
        }
    }
}
