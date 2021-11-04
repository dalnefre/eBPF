// channel+thread actor mechanism

use crossbeam::crossbeam_channel::unbounded as channel;
use crossbeam::crossbeam_channel::Sender;
use std::marker::Send;
//use std::sync::mpsc::{channel, Sender};
use std::thread;
//use tokio::task;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::hash::{Hash, Hasher};

#[derive(Debug, Clone)]
pub struct Cap<Event> {
    id: usize,
    tx: Sender<Event>,
}
impl<Event> Cap<Event> {
    pub fn send(&self, event: Event) {
        self.tx.send(event).expect("send failed");
    }
}
impl<Event> PartialEq for Cap<Event> {
    fn eq(&self, other: &Self) -> bool {
        self.id == other.id
    }
}
impl<Event> Eq for Cap<Event> {}
impl<Event> Hash for Cap<Event> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.id.hash(state);
    }
}

pub trait Actor {
    type Event: Send; // Type of event(s) handled by this Actor

    fn on_event(&mut self, event: Self::Event); // Event handler
}

pub fn create<T: Actor + Send + 'static>(mut actor: T) -> Cap<T::Event> {
    //let id = &actor as *const _ as usize; // BAD CODE!! THIS IS JUST THE ADDRESS OF THE TEMPORARY!
    let (tx, rx) = channel::<T::Event>();
    thread::spawn(move || {
    //task::spawn_blocking(move || {
        while let Ok(event) = rx.recv() {
            actor.on_event(event);
        }
    });
    let mut cap = Cap { id: 0, tx };
    cap.id = swiss_number(&cap);
    cap
}

static SWISS_NUMBER: AtomicUsize = AtomicUsize::new(1);

pub fn swiss_number<Event>(_cap: &Cap<Event>) -> usize {
    //let id = _cap as *const _ as usize; // ALTERNATE IMPLEMENTATION -- MAY NOT BE SAFE!
    let id = SWISS_NUMBER.fetch_add(1, Ordering::SeqCst);
    assert_ne!(id, 0); // can't be zero (maybe we wrapped?)
    id
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn capability_equality_implies_actor_identity() {
        #[derive(Debug, Clone)]
        enum AnEvent {
            Myself(Cap<AnEvent>),
            Another(Cap<AnEvent>),
        }
        impl AnEvent {
            pub fn new_myself(cap: &Cap<AnEvent>) -> AnEvent {
                AnEvent::Myself(cap.clone())
            }
            pub fn new_another(cap: &Cap<AnEvent>) -> AnEvent {
                AnEvent::Another(cap.clone())
            }
        }

        struct AnActor {
            myself: Option<Cap<AnEvent>>,
            label: isize,
        }
        impl AnActor {
            pub fn create(label: isize) -> Cap<AnEvent> {
                let actor = self::create(AnActor {
                    myself: None,
                    label,
                });
                actor.send(AnEvent::new_myself(&actor));
                actor
            }
        }
        impl Actor for AnActor {
            type Event = AnEvent;

            fn on_event(&mut self, event: Self::Event) {
                match &event {
                    AnEvent::Myself(cap) => {
                        println!("AnActor[{}]::Myself({:?})", self.label, cap);
                        match &self.myself {
                            Some(myself) => {
                                assert_eq!(cap, myself);
                            }
                            None => {
                                self.myself = Some(cap.clone());
                            }
                        }
                    }
                    AnEvent::Another(cap) => {
                        println!("AnActor[{}]::Another({:?})", self.label, cap);
                        if let Some(myself) = &self.myself {
                            assert_ne!(cap, myself);
                        }
                    }
                }
            }
        }

        let a = AnActor::create(123);
        println!("swiss_number(a={:?}) = {}", a, swiss_number(&a));
        let b = AnActor::create(456);
        println!("swiss_number(b={:?}) = {}", b, swiss_number(&b));
        let c = a.clone();
        println!("swiss_number(c={:?}) = {}", c, swiss_number(&c));
        let d = b.clone();
        println!("swiss_number(d={:?}) = {}", d, swiss_number(&d));

        assert_eq!(a, a);
        assert_ne!(a, b);
        assert_eq!(a, c);

        a.send(AnEvent::new_myself(&a));
        a.send(AnEvent::new_another(&b));
        a.send(AnEvent::new_myself(&c));

        // keep test thread alive long enough to deliver events
        std::thread::sleep(core::time::Duration::from_millis(10));

        println!("ready to compare c={:?} with d={:?}", c, d);
        c.send(AnEvent::new_another(&d));

        // keep test thread alive long enough to deliver events
        std::thread::sleep(core::time::Duration::from_millis(10));
    }

    #[test]
    fn counting_actor_accumulates_correct_total() {
        #[derive(Debug, Clone)]
        enum CountingEvent {
            Accum(isize),
            Total(isize),
        }

        struct Counter {
            count: isize,
        }
        impl Counter {
            pub fn create() -> Cap<CountingEvent> {
                self::create(Counter { count: 0 })
            }
        }
        impl Actor for Counter {
            type Event = CountingEvent;

            fn on_event(&mut self, event: Self::Event) {
                match event {
                    CountingEvent::Accum(change) => {
                        self.count += change;
                        println!("COUNT + {} = {}", change, self.count);
                    }
                    CountingEvent::Total(expect) => {
                        println!("TOTAL = {}", self.count);
                        assert_eq!(expect, self.count);
                    }
                }
            }
        }

        let a_counter = Counter::create();
        a_counter.send(CountingEvent::Accum(8));
        a_counter.send(CountingEvent::Accum(-5));

        // keep test thread alive long enough to deliver events
        std::thread::sleep(core::time::Duration::from_millis(10));

        a_counter.send(CountingEvent::Total(3));

        // keep test thread alive long enough verify total
        std::thread::sleep(core::time::Duration::from_millis(10));
    }
}
