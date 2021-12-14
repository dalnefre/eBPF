//use std::collections::VecDeque;
use std::slice::Iter;

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
    foo: bool,
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
            foo: false,
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
    //pub fn get_port(&self, id: usize) -> &Port {}
    pub fn is_foo(&self) -> bool {
        self.foo
    }
    pub fn event_forward_foo(&mut self) {
        self.foo = true;
        println!("foo happened");
    }
    pub fn event_reverse_foo(&mut self) {
        self.foo = false;
        println!("foo unhappened");
        //panic!("foo unhappened");
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn can_construct_a_single_node() {
        let node = Node::new(TreeId::new(12345), 3);
        assert_eq!(12345, node.get_id().get_id());
        assert_eq!(3, node.get_num_ports());
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
        let mut node = Node::new(TreeId::new(2112), 5);

        println!("before foo forward foo={}", node.is_foo());
        node.event_forward_foo();
        println!("after foo forward foo={}", node.is_foo());

        println!("before foo reverse foo={}", node.is_foo());
        node.event_reverse_foo();
        println!("after foo reverse foo={}", node.is_foo());
    }
}
