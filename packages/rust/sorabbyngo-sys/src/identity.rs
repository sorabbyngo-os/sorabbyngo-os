//! Hardware NodeID management for Sorabbyngo OS.

use crate::error::{Result, SysError};
use std::fs;
use std::path::Path;

const NODEID_PATH:  &str = "/var/lib/sorabbyngo/nodeid";
const PROC_NODEID:  &str = "/proc/sorabbyngo/nodeid";
const ENV_NODEID:   &str = "SORABBYNGO_NODE_ID";

pub struct NodeIdentity {
    pub id: String,
}

impl NodeIdentity {
    /// Load NodeID from sealed storage, env var, or derive from hostname
    pub fn load() -> Result<Self> {
        let id = if Path::new(NODEID_PATH).exists() {
            fs::read_to_string(NODEID_PATH)?.trim().to_string()
        } else if let Ok(env_id) = std::env::var(ENV_NODEID) {
            env_id
        } else {
            Self::derive_from_hostname()?
        };
        Ok(Self { id })
    }

    /// Verify live kernel NodeID against sealed NodeID
    pub fn verify(&self) -> Result<bool> {
        let proc_path = Path::new(PROC_NODEID);
        if !proc_path.exists() {
            return Ok(true); // Kernel module not loaded — skip
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

    /// Seal the NodeID to persistent storage
    pub fn seal(&self) -> Result<()> {
        let path = Path::new(NODEID_PATH);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(path, &self.id)?;
        Ok(())
    }

    pub fn short(&self) -> &str {
        &self.id[..self.id.len().min(12)]
    }

    fn derive_from_hostname() -> Result<String> {
        use std::collections::hash_map::DefaultHasher;
        use std::hash::{Hash, Hasher};

        let hostname = hostname::get()
            .map(|h| h.to_string_lossy().to_string())
            .unwrap_or_else(|_| "sorabbyngo-node".to_string());

        let mut hasher = DefaultHasher::new();
        hostname.hash(&mut hasher);
        Ok(format!("{:016x}", hasher.finish()))
    }
}

impl std::fmt::Display for NodeIdentity {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.id)
    }
}
