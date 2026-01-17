//! Chain management for vivid

use std::ffi::CString;

use crate::operator::Operator;

/// A reference to a vivid chain
///
/// The chain is owned by the context and provides access to operators.
/// This is a lightweight handle that borrows from the context.
pub struct Chain {
    ptr: *mut vivid_sys::VividChain,
}

impl Chain {
    /// Create a chain handle from a raw pointer
    ///
    /// # Safety
    ///
    /// The pointer must be valid and point to a valid VividChain.
    pub(crate) fn from_raw(ptr: *mut vivid_sys::VividChain) -> Self {
        Self { ptr }
    }

    /// Get the number of operators in the chain
    pub fn operator_count(&self) -> usize {
        let count = unsafe { vivid_sys::vivid_chain_get_operator_count(self.ptr) };
        count.max(0) as usize
    }

    /// Get an operator by index
    ///
    /// Returns `None` if index is out of bounds.
    pub fn operator_by_index(&self, index: usize) -> Option<Operator> {
        let ptr = unsafe { vivid_sys::vivid_chain_get_operator_by_index(self.ptr, index as i32) };
        if ptr.is_null() {
            None
        } else {
            Some(Operator::from_raw(ptr))
        }
    }

    /// Get an operator by name
    ///
    /// Returns `None` if not found.
    pub fn operator_by_name(&self, name: &str) -> Option<Operator> {
        let c_name = CString::new(name).ok()?;
        let ptr = unsafe { vivid_sys::vivid_chain_get_operator_by_name(self.ptr, c_name.as_ptr()) };
        if ptr.is_null() {
            None
        } else {
            Some(Operator::from_raw(ptr))
        }
    }

    /// Get the output operator
    ///
    /// Returns `None` if no output is set.
    pub fn output_operator(&self) -> Option<Operator> {
        let ptr = unsafe { vivid_sys::vivid_chain_get_output_operator(self.ptr) };
        if ptr.is_null() {
            None
        } else {
            Some(Operator::from_raw(ptr))
        }
    }

    /// Iterate over all operators in the chain
    pub fn operators(&self) -> OperatorIterator {
        OperatorIterator {
            chain: self,
            index: 0,
            count: self.operator_count(),
        }
    }

    /// Get the raw chain pointer
    pub fn as_raw(&self) -> *mut vivid_sys::VividChain {
        self.ptr
    }
}

/// Iterator over operators in a chain
pub struct OperatorIterator<'a> {
    chain: &'a Chain,
    index: usize,
    count: usize,
}

impl<'a> Iterator for OperatorIterator<'a> {
    type Item = Operator;

    fn next(&mut self) -> Option<Self::Item> {
        if self.index >= self.count {
            return None;
        }
        let op = self.chain.operator_by_index(self.index);
        self.index += 1;
        op
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = self.count - self.index;
        (remaining, Some(remaining))
    }
}

impl<'a> ExactSizeIterator for OperatorIterator<'a> {}
