use crate::actor::{self, Actor, Cap};
use crate::frame::Payload;
use crate::hub::HubEvent;

//use pretty_hex::pretty_hex;
//use crossbeam::crossbeam_channel::unbounded as channel;
use crossbeam::crossbeam_channel::{Receiver, Sender};

#[derive(Debug, Clone)]
pub enum CellEvent {
    Init(Cap<CellEvent>),
    HubToCellWrite(Payload),
    HubToCellRead,
}
impl CellEvent {
    pub fn new_init(cell: &Cap<CellEvent>) -> CellEvent {
        CellEvent::Init(cell.clone())
    }
    pub fn new_hub_to_cell_write(payload: &Payload) -> CellEvent {
        CellEvent::HubToCellWrite(payload.clone())
    }
    pub fn new_hub_to_cell_read() -> CellEvent {
        CellEvent::HubToCellRead
    }
}

// Simulated Cell for driving AIT link protocol tests
pub struct Cell {
    myself: Option<Cap<CellEvent>>,
    hub: Cap<HubEvent>,
    tx: Sender<Payload>,
    rx: Receiver<Payload>,
}
impl Cell {
    pub fn create(
        hub: &Cap<HubEvent>,
        tx: &Sender<Payload>,
        rx: &Receiver<Payload>,
    ) -> Cap<CellEvent> {
        let cell = actor::create(Cell {
            myself: None,
            hub: hub.clone(),
            tx: tx.clone(),
            rx: rx.clone(),
        });
        cell.send(CellEvent::new_init(&cell));
        cell
    }
}
impl Actor for Cell {
    type Event = CellEvent;

    fn on_event(&mut self, event: Self::Event) {
        match &event {
            CellEvent::Init(myself) => match &self.myself {
                None => self.myself = Some(myself.clone()),
                Some(_) => panic!("Cell::myself already set"),
            },
            CellEvent::HubToCellWrite(payload) => {
                println!("Cell::HubToCellWrite");
                if let Some(myself) = &self.myself {
                    if self.tx.is_empty() {
                        // if all prior data has been consumed, we are ready for more
                        self.tx
                            .send(payload.clone())
                            .expect("Cell::inbound failed!");
                        self.hub.send(HubEvent::new_cell_to_hub_read(myself)); // Ack Write
                    } else {
                        // try again...
                        myself.send(event);
                    }
                }
            }
            CellEvent::HubToCellRead => {
                println!("Cell::HubToCellRead");
                if let Some(myself) = &self.myself {
                    match self.rx.try_recv() {
                        Ok(payload) => {
                            // send next payload
                            self.hub.send(HubEvent::new_cell_to_hub_write(myself, &payload));
                        }
                        Err(_) => {
                            // try again...
                            myself.send(event);
                        }
                    }
                }
            }
        }
    }
}
