//use std::convert::TryInto;
//use std::borrow::BorrowMut;
use std::{cell::RefCell, ops::DerefMut};

//use pretty_hex::pretty_hex;
//use crossbeam::crossbeam_channel::unbounded as channel;
use crate::reactor::Error;
use crossbeam::crossbeam_channel::{Receiver, Sender};

// Simulated Port for driving AIT link protocol tests
#[derive(Debug, Clone)]
pub struct Port {
    tx: Sender<[u8; 44]>,
    rx: Receiver<[u8; 44]>,
    data: RefCell<Option<[u8; 44]>>,
}
impl Port {
    pub fn new(tx: Sender<[u8; 44]>, rx: Receiver<[u8; 44]>) -> Port {
        Port {
            tx,
            rx,
            data: RefCell::new(None),
        }
    }

    pub fn inbound_ready(&self) -> bool {
        self.tx.is_empty() // if all prior data has been consumed, we are ready for more
    }

    pub fn inbound(&self, data: [u8; 44]) -> Result<(), Error> {
        match self.tx.send(data) {
            Ok(_) => Ok(()),
            Err(e) => {
                println!("Port::in ERROR! {}", e);
                Err("Port send failed")
            }
        }
    }

    pub fn outbound(&self) -> Result<[u8; 44], Error> {
        let mut ref_data = self.data.borrow_mut();
        let opt_data = ref_data.deref_mut();
        match opt_data {
            Some(data) => Ok(data.clone()),
            None => {
                match self.rx.try_recv() {
                    Ok(data) => {
                        let _ = opt_data.insert(data.clone()); // data is Copy, clone() would be implicit
                        Ok(data)
                    }
                    Err(_) => Err("Port recv failed"), // FIXME: distinguish "empty" from "error"
                }
            }
        }
    }

    pub fn ack_outbound(&self) {
        self.data.replace(None);
    }
}
