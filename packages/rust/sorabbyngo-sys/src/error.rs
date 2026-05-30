use thiserror::Error;

pub type Result<T> = std::result::Result<T, SysError>;

#[derive(Debug, Error)]
pub enum SysError {
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("NodeID mismatch: live={live} sealed={sealed}")]
    NodeIdMismatch { live: String, sealed: String },

    #[error("NodeID not found")]
    NodeIdNotFound,

    #[error("Seccomp error: {0}")]
    Seccomp(String),

    #[error("Integrity violation: {0}")]
    IntegrityViolation(String),

    #[error("Permission denied: {0}")]
    PermissionDenied(String),

    #[error("System error: {0}")]
    System(String),
}
