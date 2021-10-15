use ether::frame::{self, Frame, TreeId};

#[test]
fn reset_constructor_makes_correct_frame() {
    let frame = Frame::new_reset(42);
    assert!(frame.is_reset());
    assert!(!frame.is_entangled());
    assert_eq!(42, frame.get_nonce());
}

#[test]
fn entangled_constructor_makes_correct_frame() {
    let frame = Frame::new_entangled(&TreeId::new(144), frame::TACK, frame::TECK);
    assert!(!frame.is_reset());
    assert!(frame.is_entangled());
    assert_eq!(144, frame.get_tree_id().get_id());
    assert_eq!(frame::TACK, frame.get_i_state());
    assert_eq!(frame::TECK, frame.get_u_state());
}
