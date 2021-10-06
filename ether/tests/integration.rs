use ether::frame::{self, Frame};

#[test]
fn frame_constructors_make_correct_frames() {
    let reset = Frame::new_reset(42);
    assert!(reset.is_reset());
    assert!(!reset.is_entangled());
    assert_eq!(42, reset.get_nonce());

    let entangled = Frame::new_entangled(144, frame::TACK, frame::TECK);
    assert!(!entangled.is_reset());
    assert!(entangled.is_entangled());
    assert_eq!(144, entangled.get_tree_id());
    assert_eq!(frame::TACK, entangled.get_i_state());
    assert_eq!(frame::TECK, entangled.get_u_state());
}