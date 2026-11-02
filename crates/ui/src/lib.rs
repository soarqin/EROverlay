//! Plugin-private egui helpers. This library is statically linked; no UI state crosses DLLs.
#![deny(unsafe_op_in_unsafe_fn)]
pub mod fonts;
pub mod input;
pub mod meshes;
