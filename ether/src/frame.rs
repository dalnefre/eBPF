/*** Ethernet Frame Format

I/U (Protocol Bits): { 00 = TICK, 01 = TECK, 10 = ~TECK, 11 = TACK }
                       0x03 (3)   0x07 (7)   0x0B (11)   0x0F (15)
        CTRL +0x80     0x83 (131) 0x87 (135) 0x8B (139)  0x8F (143)
T...: Tree Index/ID (Zero padded MSB to LSB)
X/Y: { 0 = Unicast, 1 = Broadcast }
M/N: { 0 = Global, 1 = Local Admin }
Ethertype: { 0x88b5 = Reset, 0x88b6 = Entangled }
Z...: Nonce/NodeID?

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  32  64 128
   |I I I I I I X M T T T T T T T T T T T T T T T T T T T T T T T T|   0   0   0
   +         MAC destination       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |T T T T T T T T T T T T T T T T|U U U U U U Y N Z Z Z Z Z Z Z Z|   1   .   .
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+           MAC source          +
   |Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z|   2   1   .
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |   Ethertype = 0x88b5/0x88b6   |    Reserved (Sequence #?)     |   3   .   .
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

***/

use std::convert::TryInto;

pub const PAYLOAD_SIZE: usize = 44; // maximum payload, 44 octets
pub const FRAME_SIZE: usize = 60; // maximum frame, 60 octets (64 - 4 octet FCS)

pub const TICK: u8 = 0x03;
pub const TECK: u8 = 0x07;
pub const RTECK: u8 = 0x0B;
pub const TACK: u8 = 0x0F;
pub const CTRL: u8 = 0x80;

pub const FAILOVER_R: u8 = 0x1E;
pub const FAILOVER_D: u8 = 0xE1;

#[derive(Debug, Clone)]
pub struct TreeId {
    id: u32,
}
impl TreeId {
    pub fn new(id: u32) -> TreeId {
        TreeId { id }
    }
    pub fn get_id(&self) -> u32 {
        self.id
    }
}

#[derive(Debug, Clone)]
pub struct Payload {
    pub ctrl: bool,
    pub id: TreeId,
    pub data: [u8; PAYLOAD_SIZE],
}
impl Payload {
    pub fn new(id: &TreeId, data: &[u8]) -> Payload {
        let ctrl = false;
        let id = id.clone();
        let data = data.try_into().expect("44 octet payload required");
        Payload { ctrl, id, data }
    }
    pub fn ctrl(id: &TreeId, data: &[u8]) -> Payload {
        let mut payload = Payload::new(&id, &data);
        payload.ctrl = true;
        payload
    }
    pub fn ctrl_msg(id: &TreeId, op: u8, b: u8, n: u16, w: u32) -> Payload {
        let data = [0xFF_u8; PAYLOAD_SIZE];
        let mut payload = Payload::ctrl(&id, &data);
        payload.set_op(op);
        payload.set_u8(b);
        payload.set_u16(n);
        payload.set_u32(w);
        payload
    }

    pub fn set_op(&mut self, op: u8) {
        self.data[42] = op;
    }
    pub fn get_op(&self) -> u8 {
        self.data[42]
    }

    pub fn set_u8(&mut self, b: u8) {
        self.data[43] = b;
    }
    pub fn get_u8(&self) -> u8 {
        self.data[43]
    }

    pub fn set_u16(&mut self, n: u16) {
        self.data[40..42].copy_from_slice(&n.to_be_bytes());
    }
    pub fn get_u16(&self) -> u16 {
        let mut n = [0; 2];
        n.copy_from_slice(&self.data[40..42]);
        u16::from_be_bytes(n)
    }

    pub fn set_u32(&mut self, w: u32) {
        self.data[36..40].copy_from_slice(&w.to_be_bytes());
    }
    pub fn get_u32(&self) -> u32 {
        let mut w = [0; 4];
        w.copy_from_slice(&self.data[36..40]);
        u32::from_be_bytes(w)
    }
}

#[derive(Debug, Clone)]
pub struct Frame {
    pub data: [u8; FRAME_SIZE],
}
impl Default for Frame {
    fn default() -> Frame {
        let header = b"\
                \xFF\xFF\xFF\xFF\xFF\xFF\
                \x00\x00\x00\x00\x00\x00\
                \x88\xB6\
                \x00\x00"; // skeletal entangled frame
        let mut data = [0x20_u8; FRAME_SIZE];
        data[0..16].copy_from_slice(header);
        Frame { data }
    }
}
impl Frame {
    pub fn new(data: &[u8]) -> Frame {
        let data = data.try_into().expect("60 octet frame required");
        Frame { data }
    }
    pub fn new_reset(nonce: u32) -> Frame {
        let mut frame = Self::default();
        frame.set_reset();
        frame.set_nonce(nonce);
        frame
    }
    pub fn new_entangled(seq: u16, i: u8, u: u8) -> Frame {
        let mut frame = Self::default();
        frame.set_entangled();
        frame.set_sequence(seq);
        frame.set_i_state(i);
        frame.set_u_state(u);
        frame
    }

    /*
        pub fn get_data(&self) -> &[u8] {
            &self.data[..]
        }
    */

    pub fn set_reset(&mut self) {
        self.data[13] = 0xB5;
    }
    pub fn is_reset(&self) -> bool {
        (self.data[12] == 0x88) && (self.data[13] == 0xB5)
    }

    pub fn set_entangled(&mut self) {
        self.data[13] = 0xB6;
    }
    pub fn is_entangled(&self) -> bool {
        (self.data[12] == 0x88) && (self.data[13] == 0xB6)
    }

    pub fn set_nonce(&mut self, nonce: u32) {
        // `copy_from_slice` will not panic because
        // the slice is the same size as `nonce` (4 octets)
        self.data[8..12].copy_from_slice(&nonce.to_be_bytes());
    }
    pub fn get_nonce(&self) -> u32 {
        let mut nonce = [0; 4];
        nonce.copy_from_slice(&self.data[8..12]);
        u32::from_be_bytes(nonce)
    }

    pub fn set_tree_id(&mut self, tree_id: &TreeId) {
        // `copy_from_slice` will not panic because
        // the slice is the same size as `id` (4 octets)
        self.data[2..6].copy_from_slice(&tree_id.get_id().to_be_bytes());
    }
    pub fn get_tree_id(&self) -> TreeId {
        let mut tree_id = [0; 4];
        tree_id.copy_from_slice(&self.data[2..6]);
        TreeId::new(u32::from_be_bytes(tree_id))
    }

    pub fn set_i_state(&mut self, i: u8) {
        self.data[0] = i;
    }
    pub fn get_i_state(&self) -> u8 {
        self.data[0] & !CTRL // mask out CTRL bit
    }

    pub fn set_u_state(&mut self, u: u8) {
        self.data[6] = u;
    }
    pub fn get_u_state(&self) -> u8 {
        self.data[6]
    }

    pub fn set_sequence(&mut self, sequence: u16) {
        // `copy_from_slice` will not panic because
        // the slice is the same size as `sequence` (2 octets)
        self.data[14..16].copy_from_slice(&sequence.to_be_bytes());
    }
    pub fn get_sequence(&self) -> u16 {
        let mut sequence = [0; 2];
        sequence.copy_from_slice(&self.data[14..16]);
        u16::from_be_bytes(sequence)
    }

    pub fn set_control(&mut self) {
        self.data[0] |= CTRL;
    }
    pub fn is_control(&self) -> bool {
        (self.data[0] & CTRL) == CTRL
    }

    pub fn set_payload(&mut self, payload: &Payload) {
        if payload.ctrl {
            self.set_control();
        }
        self.set_tree_id(&payload.id);
        self.data[16..60].copy_from_slice(&payload.data[..]);
    }
    pub fn get_payload(&self) -> Payload {
        let tree_id = self.get_tree_id();
        //self.data[16..60].try_into().expect("Bad payload size")
        if self.is_control() {
            Payload::ctrl(&tree_id, &self.data[16..60])
        } else {
            Payload::new(&tree_id, &self.data[16..60])
        }
    }
}
