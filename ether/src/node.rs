//use std::collections::VecDeque;
use std::slice::Iter;
use std::rc::Rc;

use crate::frame::{Payload, TreeId};

const MAX_PORTS: usize = 8;

// Point-to-point connection Port
pub struct Port {
    id: usize,
    _token_in: Option<Payload>,
    _token_out: Option<Payload>,
}
impl Port {
    fn new(id: usize) -> Port {
        Port {
            id,
            _token_in: None,
            _token_out: None,
        }
    }
    pub fn get_id(&self) -> usize {
        self.id
    }
}

// Multi-Port tree-routing Node
pub struct Node {
    id: TreeId, // Unique persistent ID of the "black" tree rooted at this Node
    ports: Vec<Port>,
    foo: isize,
}
impl Node {
    pub fn new(id: TreeId, num_ports: usize) -> Node {
        assert!(num_ports > 0);
        assert!(num_ports <= MAX_PORTS);
        let mut ports = Vec::with_capacity(num_ports);
        for i in 0..num_ports {
            ports.push(Port::new(i));
        }
        let node = Node {
            id,
            ports,
            foo: 0,
        };
        node
    }
    pub fn get_id(&self) -> TreeId {
        TreeId::new(self.id.get_id())
    }
    pub fn get_num_ports(&self) -> usize {
        self.ports.len()
    }
    pub fn get_port_iter(&self) -> Iter<Port> {
        self.ports.iter()
    }
    pub fn get_port(&self, id: usize) -> Option<&Port> {
        //self.ports[id]
        self.ports.get(id)
    }
    pub fn is_foo(&self) -> isize {
        self.foo
    }
    pub fn event_forward_foo(&mut self) {
        self.foo = self.foo.wrapping_add(1);
        println!("foo happened");
    }
    pub fn event_reverse_foo(&mut self) {
        self.foo = self.foo.wrapping_sub(1);
        println!("foo unhappened");
        //panic!("foo unhappened");
    }
}

// Automic Information Transfer (AIT)
pub struct AIT {
    _node: Rc<Node>,
}
impl AIT {
    pub fn new(node: &Rc<Node>) -> AIT {
        let ait = AIT {
            _node: Rc::clone(node),
        };
        ait
    }
    pub fn start(&self) {}
    pub fn reverse(&self) {}
    pub fn cancel(&self) {}
    pub fn is_complete(&self) {}
    pub fn is_failed(&self) {}
    pub fn get_result(&self) {}
}

#[cfg(test)]
mod tests {
    use std::{rc::Rc, borrow::BorrowMut};

    use super::*;

    #[test]
    fn can_construct_a_single_node() {
        let node = Rc::new(Node::new(TreeId::new(12345), 5));
        assert_eq!(12345, node.get_id().get_id());
        assert_eq!(5, node.get_num_ports());
        assert!(node.get_port(MAX_PORTS).is_none());
        let mut port_id = 0;
        for port in node.get_port_iter() {
            assert_eq!(port_id, port.get_id());
            port_id += 1;
        }
        node.get_port_iter()
            .enumerate()
            .for_each(|(index, port)| {
                assert_eq!(index, port.get_id());
            })
    }

    #[test]
    fn events_have_forward_and_reverse_directions() {
        // Events have two "parts", a forward and reverse direction,
        // and each direction has two "parts", a caller and callee.
        // In this test, the "caller" is the test iteself,
        // and the "callee" is the Node created by the test.
        let mut node = Node::new(TreeId::new(2112), 3);

        println!("before foo forward foo={}", node.is_foo());
        node.borrow_mut().event_forward_foo();
        println!("after foo forward foo={}", node.is_foo());

        println!("before foo reverse foo={}", node.is_foo());
        node.borrow_mut().event_reverse_foo();
        println!("after foo reverse foo={}", node.is_foo());

        println!("before foo event foo={}", node.is_foo());
        //let x = node.event_foo();
        let x = AIT::new(&Rc::new(node));
        x.start();
        println!("after foo event foo={}", node.is_foo());
        x.reverse();
        x.cancel();
        x.is_complete();
        x.is_failed();
        x.get_result();
    }
}
