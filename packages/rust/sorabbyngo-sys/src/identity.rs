//! Hardware NodeID management for Sorabbyngo OS.
//! Paths and fallback logic match C (libsorabbyngo) and Python bindings.
use crate::error::{Result, SysError};
use std::fs;
use std::path::Path;

const NODEID_PATH: &str = "/var/lib/sorabbyngo/nodeid";
const PROC_NODEID: &str = "/proc/sorabbyngo/nodeid";
const ENV_NODEID:  &str = "SORABBYNGO_NODE_ID";

pub struct NodeIdentity {
    pub id:     String,
    pub source: NodeIdSource,
}

#[derive(Debug, Clone, PartialEq)]
pub enum NodeIdSource {
    Sealed,    // /var/lib/sorabbyngo/nodeid
    Env,       // SORABBYNGO_NODE_ID
    Fallback,  // hostname-derived (dev only)
}

impl NodeIdentity {
    /// Load NodeID — fails hard if module not loaded and allow_fallback=false.
    pub fn load(allow_fallback: bool) -> Result<Self> {
        // 1. Sealed storage
        if Path::new(NODEID_PATH).exists() {
            let id = fs::read_to_string(NODEID_PATH)?.trim().to_string();
            return Ok(Self { id, source: NodeIdSource::Sealed });
        }
        // 2. Environment override
        if let Ok(env_id) = std::env::var(ENV_NODEID) {
            return Ok(Self { id: env_id.trim().to_string(), source: NodeIdSource::Env });
        }
        // 3. Fallback — dev/CI only
        if allow_fallback {
            let id = Self::derive_from_hostname()?;
            return Ok(Self { id, source: NodeIdSource::Fallback });
        }
        Err(SysError::NodeIdUnavailable)
    }

    /// Verify live kernel NodeID against sealed NodeID.
    pub fn verify(&self) -> Result<bool> {
        let proc_path = Path::new(PROC_NODEID);
        if !proc_path.exists() {
            if self.source == NodeIdSource::Fallback {
                return Ok(true);
            }
            return Err(SysError::KernelModuleNotLoaded("sorabbyngo_nodeid".into()));
        }
        let live = fs::read_to_string(proc_path)?.trim().to_string();
        if live != self.id {
            return Err(SysError::NodeIdMismatch {
                live,
                sealed: self.id.clone(),
            });
        }
        Ok(true)
    }

    /// Seal to persistent storage (called from S05nodeid equivalent in Rust).
    pub fn seal(&self) -> Result<()> {
        if let Some(parent) = Path::new(NODEID_PATH).parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(NODEID_PATH, &self.id)?;
        Ok(())
    }

    pub fn short(&self) -> &str {
        &self.id[..self.id.len().min(12)]
    }

    /// SHA-256(hostname) — matches Python and C fallback exactly.
    fn derive_from_hostname() -> Result<String> {
        use sha2::{Digest, Sha256};
        let hostname = hostname::get()
            .map(|h| h.to_string_lossy().to_string())
            .unwrap_or_else(|_| "sorabbyngo-node".to_string());
        let hash = Sha256::digest(hostname.as_bytes());
        Ok(format!("{:x}", hash))
    }
}

impl std::fmt::Display for NodeIdentity {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.id)
    }
}
