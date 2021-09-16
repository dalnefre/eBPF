/*** Ethernet Frame Format (PROPOSED FOR FUTURE USE)

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  32  64 128
   |I I I I I I X M T T T T T T T T T T T T T T T T T T T T T T T T|   0   0   0
   +         MAC destination       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |T T T T T T T T T T T T T T T T|U U U U U U Y N Z Z Z Z Z Z Z Z|   1   .   .
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+           MAC source          +
   |Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z Z|   2   1   .
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |   Ethertype = 0x88b5/0x88b6   |     Reserved (Checksum?)      |   3   .   .
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

I/U (Protocol Bits): { 00 = TICK, 01 = TECK, 10 = ~TECK, 11 = TACK }
T...: Tree Index/ID (Zero padded MSB to LSB)
X/Y: { 0 = Unicast, 1 = Broadcast }
M/N: { 0 = Global, 1 = Local Admin }
Ethertype: { 0x88b5 = Reset, 0x88b6 = Entangled }
Z...: Nonce/NodeID?

***/

use std::convert::TryInto;

pub const TICK: u8 = 0xF0;
pub const TECK: u8 = 0xE1;
pub const RTECK: u8 = 0xD2;
pub const TACK: u8 = 0xC3;

pub struct Frame {
    pub data: [u8; 60],
}

type Error = Box<dyn std::error::Error>;

impl Default for Frame {
    fn default() -> Frame {
        let header = b"\
                \xFF\xFF\xFF\xFF\xFF\xFF\
                \xFF\x00\x00\x00\x00\x00\
                \x88\xB5\
                \x00\x00";  // skeletal entangled frame
        let mut data = [0x20_u8; 60];
        data[0..16].copy_from_slice(header);
        Frame { data }
    }
}

impl Frame {
    pub fn new(data: &[u8]) -> Result<Frame, Error> {
        let data = data.try_into()?;
        Ok(Frame { data })
    }
/*
    pub fn get_data(&self) -> &[u8] {
        &self.data[..]
    }
*/
    pub fn set_reset(&mut self) {
        self.data[0] = 0x7F;
        self.data[6] = 0x7F;
    }

    pub fn is_reset(&self) -> bool {
        (self.data[12] == 0x88) && (self.data[13] == 0xB5) && ((self.data[6] & 0x80) == 0x00)
    }

    pub fn is_entangled(&self) -> bool {
        (self.data[12] == 0x88) && (self.data[13] == 0xB5) && ((self.data[6] & 0x80) == 0x80)
    }

    pub fn set_tree_id(&mut self, id: u32) {
        // `copy_from_slice` will not panic because
        // the slice is the same size as `id` (4 octets)
        self.data[8..12].copy_from_slice(&id.to_be_bytes());
    }

    pub fn get_tree_id(&self) -> u32 {
        let mut tree_id = [0; 4];
        tree_id.copy_from_slice(&self.data[8..12]);
        u32::from_be_bytes(tree_id)
    }

    pub fn set_i_state(&mut self, i: u8) {
        self.data[14] = i;
    }

    pub fn get_i_state(&self) -> u8 {
        self.data[14]
    }

    pub fn set_u_state(&mut self, u: u8) {
        self.data[15] = u;
    }

    pub fn get_u_state(&self) -> u8 {
        self.data[15]
    }
}
