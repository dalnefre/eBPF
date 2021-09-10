//! # ReActor
//!
//! An [Actor](https://en.wikipedia.org/wiki/Actor_model) runtime for Rust.
//!

//#![no_std]  <-- FIXME: need std for println

extern crate alloc;

use alloc::boxed::Box;
use alloc::collections::VecDeque;
use alloc::rc::Rc;
use alloc::rc::Weak;
use alloc::vec::Vec;
use core::cell::RefCell;
use core::fmt;
//use alloc::collections::BTreeMap;

pub trait Behavior {
    fn react(&self, event: Event) -> Result<Effect, Error>;
}

pub struct Actor {
    behavior: RefCell<Box<dyn Behavior>>,
}
impl Actor {
    fn new(behavior: Box<dyn Behavior>) -> Rc<Actor> {
        Rc::new(Actor {
            behavior: RefCell::new(behavior),
        })
    }

    fn dispatch(&self, event: Event) -> Result<Effect, Error> {
        self.behavior.borrow().react(event)
    }
    fn update(&self, behavior: Box<dyn Behavior>) {
        *self.behavior.borrow_mut() = behavior;
    }
}
impl fmt::Debug for Actor {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        formatter.write_fmt(format_args!("^{:p}", self))
    }
}
impl PartialEq for Actor {
    fn eq(&self, other: &Actor) -> bool {
        self as *const Actor == other as *const Actor
    }
}

pub struct Event {
    pub target: Rc<Actor>,
    pub message: Message,
}
impl Event {
    fn new(target: &Rc<Actor>, message: Message) -> Event {
        Event {
            target: Rc::clone(target),
            message: message,
        }
    }
}

pub type Error = &'static str;

#[derive(Debug, PartialEq, Clone)]
pub enum Message {
    Empty,
    Nat(usize),
    Int(isize),
    Num(isize, isize, isize), // num = int * base ^ exp
    Sym(&'static str),
    //    List(&'static [Message]),
    //    Struct(BTreeMap<String, Message>),
    Pair(Box<Message>, Box<Message>),
    Addr(Rc<Actor>),
}

pub struct Effect {
    actors: Vec<Rc<Actor>>,
    events: Vec<Event>,
    update: Option<Box<dyn Behavior>>,
}
impl Effect {
    pub fn new() -> Effect {
        Effect {
            actors: Vec::new(),
            events: Vec::new(),
            update: None,
        }
    }

    pub fn create(&mut self, behavior: Box<dyn Behavior>) -> Rc<Actor> {
        let actor = Actor::new(behavior);
        self.actors.push(Rc::clone(&actor));
        actor
    }
    pub fn send(&mut self, target: &Rc<Actor>, message: Message) {
        let event = Event::new(target, message);
        self.events.push(event);
    }
    pub fn update(&mut self, behavior: Box<dyn Behavior>) -> Result<(), Error> {
        match self.update {
            Some(_) => Err("Attempted to update the behavior of an actor more than once"),
            None => {
                self.update = Some(behavior);
                Ok(())
            }
        }
    }
}

pub struct Config {
    actors: Vec<Weak<Actor>>,
    events: VecDeque<Event>,
}
impl Config {
    pub fn new() -> Config {
        Config {
            actors: Vec::new(),
            events: VecDeque::new(),
        }
    }

    /// Execute bootstrap `behavior` to initialize Config.
    ///
    /// Returns the number of events enqueued.
    pub fn boot(&mut self, behavior: Box<dyn Behavior>) -> usize {
        let actor = Actor::new(behavior);
        self.actors.push(Rc::downgrade(&actor));
        let event = Event::new(&actor, Message::Empty);
        self.events.push_back(event);
        self.dispatch(1) // dispatch bootstrap message
    }

    /// Dispatch up to `limit` events.
    ///
    /// Returns the number of events still waiting in queue.
    pub fn dispatch(&mut self, mut limit: usize) -> usize {
        while limit > 0 {
            if let Some(event) = self.events.pop_front() {
                let target = Rc::clone(&event.target);
                match target.dispatch(event) {
                    Ok(mut effect) => {
                        while let Some(actor) = effect.actors.pop() {
                            self.actors.push(Rc::downgrade(&actor));
                        }
                        while let Some(event) = effect.events.pop() {
                            self.events.push_back(event);
                        }
                        if let Some(behavior) = effect.update.take() {
                            target.update(behavior);
                        }
                    }
                    Err(reason) => {
                        println!("FAIL! {}", reason); // FIXME: should deliver a signal to meta-controller
                    }
                }
            } else {
                break;
            }
            limit -= 1;
        }
        self.events.len() // remaining event count
    }
}

pub mod idiom {
    use super::*;

    /// A Sink actor simply throws away all messages that it receives.
    ///
    /// If we make a Request, but don’t care about the Reply, we use a Sink as the Customer.
    ///
    /// # Humus
    /// ```Humus
    /// LET sink_beh = \_.[]
    /// CREATE sink WITH sink_beh
    /// ```
    pub struct Sink;
    impl Behavior for Sink {
        fn react(&self, _event: Event) -> Result<Effect, Error> {
            Ok(Effect::new())
        }
    }
    impl Sink {
        pub fn new() -> Box<dyn Behavior> {
            Box::new(Sink)
        }
    }

    /// A Forwarding actor is an Alias or Proxy for another actor.
    ///
    /// Messages sent to a forwarding actor are passed on to the Subject.
    ///
    /// # Humus
    /// ```Humus
    /// LET forward_beh = \cust.\msg.[
    ///     SEND msg TO cust
    /// ]
    /// ```
    pub struct Forward {
        pub subject: Rc<Actor>,
    }
    impl Behavior for Forward {
        fn react(&self, event: Event) -> Result<Effect, Error> {
            let mut effect = Effect::new();
            effect.send(&self.subject, event.message);
            Ok(effect)
        }
    }
    impl Forward {
        pub fn new(subject: &Rc<Actor>) -> Box<dyn Behavior> {
            Box::new(Forward {
                subject: Rc::clone(&subject),
            })
        }
    }

    /// A Label is a Forward actor that adds some fixed information to each message.
    ///
    /// It acts like a Decorator for messages.
    /// Sometimes it plays the role of an Adaptor between actors,
    /// structuring messages to meet the expectations of the subject.
    ///
    /// # Humus
    /// ```Humus
    /// LET label_beh(cust, label) = \msg.[
    ///     SEND (label, msg) TO cust
    /// ]
    /// ```
    pub struct Label {
        pub cust: Rc<Actor>,
        pub label: Message,
    }
    impl Behavior for Label {
        fn react(&self, event: Event) -> Result<Effect, Error> {
            let mut effect = Effect::new();
            effect.send(
                &self.cust,
                Message::Pair(Box::new(self.label.clone()), Box::new(event.message)),
            );
            Ok(effect)
        }
    }
    impl Label {
        pub fn new(cust: &Rc<Actor>, label: Message) -> Box<dyn Behavior> {
            Box::new(Label {
                cust: Rc::clone(&cust),
                label: label,
            })
        }
    }

    /// A Tag labels each message with a reference to itself.
    ///
    /// A Tag actor is often used as a Customer for a Request when we want to identify a specific Reply.
    ///
    /// # Humus
    /// ```Humus
    /// LET tag_beh(cust) = \msg.[
    ///     SEND (SELF, msg) TO cust
    /// ]
    /// ```
    pub struct Tag {
        pub cust: Rc<Actor>,
    }
    impl Behavior for Tag {
        fn react(&self, event: Event) -> Result<Effect, Error> {
            let mut effect = Effect::new();
            effect.send(
                &self.cust,
                Message::Pair(
                    Box::new(Message::Addr(Rc::clone(&event.target))),
                    Box::new(event.message),
                ),
            );
            Ok(effect)
        }
    }
    impl Tag {
        pub fn new(cust: &Rc<Actor>) -> Box<dyn Behavior> {
            Box::new(Tag {
                cust: Rc::clone(&cust),
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sink_behavior() {
        let sink = Actor::new(Box::new(idiom::Sink));
        assert_eq!(sink, sink);
        println!("sink = {:?}", sink);

        let event = Event::new(&sink, Message::Empty);
        let effect = sink.dispatch(event).expect("Dispatch failed.");

        assert_eq!(0, effect.actors.len());
        assert_eq!(0, effect.events.len());
    }

    struct Once {
        cust: Rc<Actor>,
    }
    impl Behavior for Once {
        fn react(&self, event: Event) -> Result<Effect, Error> {
            let mut effect = Effect::new();
            effect.send(&self.cust, event.message);
            effect.update(Box::new(idiom::Sink))?;
            Ok(effect)
        }
    }

    #[test]
    fn once_behavior() {
        let sink = Actor::new(Box::new(idiom::Sink));
        let once = Actor::new(Box::new(Once {
            cust: Rc::clone(&sink),
        }));

        let event = Event::new(&once, Message::Empty);
        let effect = once.dispatch(event).expect("Dispatch failed.");

        assert_eq!(0, effect.actors.len());
        assert_eq!(1, effect.events.len());

        let behavior = effect.update.expect("Expected update.");
        once.update(behavior);

        let event = Event::new(&once, Message::Empty);
        let effect = once.dispatch(event).expect("Dispatch failed.");

        assert_eq!(0, effect.actors.len());
        assert_eq!(0, effect.events.len());
    }

    struct Maker;
    impl Behavior for Maker {
        fn react(&self, event: Event) -> Result<Effect, Error> {
            let mut effect = Effect::new();
            match event.message {
                Message::Addr(cust) => {
                    let actor = effect.create(Box::new(idiom::Sink));
                    effect.send(&cust, Message::Addr(Rc::clone(&actor)));
                    Ok(effect)
                }
                _ => Err("unknown message"),
            }
        }
    }

    #[test]
    fn maker_behavior() {
        let maker = Actor::new(Box::new(Maker));

        let event = Event::new(&maker, Message::Empty);
        let reason = maker.dispatch(event).err().expect("Error expected.");
        println!("Got error = {:?}", reason);

        let sink = Actor::new(Box::new(idiom::Sink));
        let event = Event::new(&maker, Message::Addr(Rc::clone(&sink)));
        let effect = maker.dispatch(event).expect("Dispatch failed.");

        assert_eq!(1, effect.actors.len());
        assert_eq!(1, effect.events.len());
    }
}
