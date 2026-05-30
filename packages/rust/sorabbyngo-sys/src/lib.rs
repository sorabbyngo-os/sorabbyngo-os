//! # sorabbyngo-sys
//! System-level Rust bindings for Sorabbyngo OS.
//! Provides hardware identity access, process hardening,
//! seccomp profile application, and system integrity checks.

pub mod identity;
pub mod hardening;
pub mod integrity;
pub mod error;

pub use error::{SysError, Result};
pub use identity::NodeIdentity;
pub use hardening::ProcessHardener;
pub use integrity::IntegrityChecker;
