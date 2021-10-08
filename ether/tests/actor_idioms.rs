use ether::actor::{self, Actor, Cap};

#[test]
fn once_actor_forwards_only_one_event() {
    #[derive(Debug, Clone)]
    pub struct EmptyEvent; // an empty message

    #[derive(Debug, Clone)]
    struct VerifyEvent; // verify mock

    #[derive(Debug, Clone)]
    enum MockEvent {
        // aggregate event type
        Mock(EmptyEvent),
        Ctrl(VerifyEvent),
    }

    struct MockDelegate {
        event_count: usize,
    }
    impl MockDelegate {
        pub fn create() -> Cap<MockEvent> {
            actor::create(MockDelegate { event_count: 0 })
        }
    }
    impl Actor for MockDelegate {
        type Event = MockEvent;

        fn on_event(&mut self, event: Self::Event) {
            match &event {
                MockEvent::Mock(_e) => {
                    self.event_count += 1;
                    println!("EVENT_COUNT = {}", self.event_count);
                }
                MockEvent::Ctrl(_e) => {
                    println!("VERIFYING...");
                    assert_eq!(1, self.event_count);
                }
            }
        }
    }
    let a_mock = MockDelegate::create();

    struct MockFacet {
        mock: Cap<MockEvent>,
    }
    impl MockFacet {
        pub fn create(mock: &Cap<MockEvent>) -> Cap<EmptyEvent> {
            actor::create(MockFacet { mock: mock.clone() })
        }
    }
    impl Actor for MockFacet {
        type Event = EmptyEvent;

        fn on_event(&mut self, event: Self::Event) {
            self.mock.send(MockEvent::Mock(event));
        }
    }
    let a_delegate = MockFacet::create(&a_mock);

    struct CtrlFacet {
        mock: Cap<MockEvent>,
    }
    impl CtrlFacet {
        pub fn create(mock: &Cap<MockEvent>) -> Cap<VerifyEvent> {
            actor::create(CtrlFacet { mock: mock.clone() })
        }
    }
    impl Actor for CtrlFacet {
        type Event = VerifyEvent;

        fn on_event(&mut self, event: Self::Event) {
            self.mock.send(MockEvent::Ctrl(event));
        }
    }
    let a_test = CtrlFacet::create(&a_mock);

    pub struct Once {
        delegate: Option<Cap<EmptyEvent>>,
    }
    impl Once {
        pub fn create(delegate: &Cap<EmptyEvent>) -> Cap<EmptyEvent> {
            actor::create(Once {
                delegate: Some(delegate.clone()),
            })
        }
    }
    impl Actor for Once {
        type Event = EmptyEvent;

        fn on_event(&mut self, event: Self::Event) {
            match &self.delegate {
                None => {
                    println!("GOT NONE");
                }
                Some(target) => {
                    println!("GOT SOME");
                    target.send(event);
                    self.delegate = None;
                }
            }
        }
    }
    let a_once = Once::create(&a_delegate);
    a_once.send(EmptyEvent);
    a_once.send(EmptyEvent);
    a_once.send(EmptyEvent);

    // keep test thread alive long enough to deliver events
    std::thread::sleep(core::time::Duration::from_millis(10));
    a_test.send(VerifyEvent);

    // keep test thread alive long enough verify mock(s)
    std::thread::sleep(core::time::Duration::from_millis(10));
}
