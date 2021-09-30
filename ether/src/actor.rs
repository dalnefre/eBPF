// draft actor mechanism

use crossbeam::crossbeam_channel::unbounded as channel;
use crossbeam::crossbeam_channel::Sender;
use std::thread;
use std::marker::Send;

pub struct OCap<Event> {
    tx: Sender<Event>,
}
impl<Event> OCap<Event> {
    pub fn send(&self, event: Event) {
        self.tx.send(event).expect("send failed");
    }
}

pub trait Actor {
    //type Event : Send + 'static; // Type of event(s) handled by this Actor
    type Event : Send; // Type of event(s) handled by this Actor

    fn on_event(&mut self, event: Self::Event); // Event handler
}

pub fn run_actor<T: Actor + Send + 'static>(mut actor: T) -> OCap<T::Event> {
//pub fn new_actor<T: Actor + Send>(mut actor: T) -> OCap<T::Event> {
    let (tx, rx) = channel::<T::Event>();
    thread::spawn(move || {
        loop {
            let event = rx.recv().expect("recv failed");
            actor.on_event(event);
        }
    });
    OCap { tx }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn once_behavior() {
        static mut ONCE_COUNT: i32 = -1;

        struct Empty; // an empty message

        struct Once {
            delegate: Option<OCap<Empty>>,
        }
        impl Actor for Once {
            type Event = Empty;

            //fn on_event(&mut self, event: Empty) {
            fn on_event(&mut self, event: Self::Event) {
                    match &self.delegate {
                    None => {
                        println!("GOT NONE");
                        unsafe { ONCE_COUNT = 0 };
                    },
                    Some(target) => {
                        println!("GOT SOME");
                        unsafe { ONCE_COUNT = 1 };
                        target.send(event);
                        self.delegate = None;
                    }
                }
            }
        }

        struct Ignore;
        impl Actor for Ignore {
            type Event = Empty;

            fn on_event(&mut self, _event: Self::Event) {
                println!("IGNORE");
                unsafe { ONCE_COUNT = -2 };
            }
        }

        let a_ignore = run_actor(Ignore);
        let once = Once { delegate: Some(a_ignore) };
        let a_once = run_actor(once);
        a_once.send(Empty);
        a_once.send(Empty);

        // FIXME: test results are timing-dependent
        thread::sleep(core::time::Duration::from_millis(10));
        unsafe { assert_eq!(-2, ONCE_COUNT); }
    }
}