use ether::frame::{self, Frame};

#[test]
fn reset_constructor_makes_correct_frame() {
    let frame = Frame::new_reset(42);
    assert!(frame.is_reset());
    assert!(!frame.is_entangled());
    assert_eq!(42, frame.get_nonce());
}

#[test]
fn entangled_constructor_makes_correct_frame() {
    let frame = Frame::new_entangled(144, frame::TACK, frame::TECK);
    assert!(!frame.is_reset());
    assert!(frame.is_entangled());
    assert_eq!(144, frame.get_tree_id());
    assert_eq!(frame::TACK, frame.get_i_state());
    assert_eq!(frame::TECK, frame.get_u_state());
}

use ether::actor::{self, Actor, Cap};

#[test]
fn once_actor_forwards_only_one_event() {

    #[derive(Debug, Clone)]
    pub struct EmptyEvent; // an empty message

    #[derive(Debug, Clone)]
    struct VerifyEvent; // verify mock

    #[derive(Debug, Clone)]
    enum MockEvent { // aggregate event type
        Mock(EmptyEvent),
        Test(VerifyEvent),
    }

    struct MockDelegate {
        event_count: usize,
    }
    impl MockDelegate {
        pub fn create() -> Cap<MockEvent> {
            actor::create(MockDelegate {
                event_count: 0,
            })
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
                MockEvent::Test(_e) => {
                    println!("VERIFYING...");
                    assert_eq!(1, self.event_count);
                }
            }
        }
    }
    let a_mock = MockDelegate::create();

    struct WrapMock {
        mock: Cap<MockEvent>,
    }
    impl WrapMock {
        pub fn create(mock: &Cap<MockEvent>) -> Cap<EmptyEvent> {
            actor::create(WrapMock {
                mock: mock.clone(),
            })
        }
    }
    impl Actor for WrapMock {
        type Event = EmptyEvent;

        fn on_event(&mut self, event: Self::Event) {
            self.mock.send(MockEvent::Mock(event));
        }
    }
    let a_delegate = WrapMock::create(&a_mock);

    struct WrapTest {
        mock: Cap<MockEvent>,
    }
    impl WrapTest {
        pub fn create(mock: &Cap<MockEvent>) -> Cap<VerifyEvent> {
            actor::create(WrapTest {
                mock: mock.clone(),
            })
        }
    }
    impl Actor for WrapTest {
        type Event = VerifyEvent;

        fn on_event(&mut self, event: Self::Event) {
            self.mock.send(MockEvent::Test(event));
        }
    }
    let a_test = WrapTest::create(&a_mock);

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
