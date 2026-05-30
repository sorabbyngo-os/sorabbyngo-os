//! System integrity checking — file hashes, module verification.

use crate::error::{Result, SysError};
use std::collections::HashMap;
use std::fs;
use std::path::Path;
use sha2::{Sha256, Digest};

pub struct IntegrityChecker {
    /// Expected SHA-256 hashes for critical files
    pub manifest: HashMap<String, String>,
}

impl IntegrityChecker {
    pub fn new() -> Self {
        Self { manifest: HashMap::new() }
    }

    /// Add a file to the integrity manifest
    pub fn register(&mut self, path: impl Into<String>, expected_sha256: impl Into<String>) {
        self.manifest.insert(path.into(), expected_sha256.into());
    }

    /// Verify all registered files
    pub fn verify_all(&self) -> Result<Vec<IntegrityResult>> {
        let mut results = Vec::new();
        for (path, expected) in &self.manifest {
            let result = self.verify_file(path, expected);
            results.push(result);
        }
        Ok(results)
    }

    /// Verify a single file against expected hash
    pub fn verify_file(&self, path: &str, expected: &str) -> IntegrityResult {
        let p = Path::new(path);
        if !p.exists() {
            return IntegrityResult {
                path: path.to_string(),
                status: IntegrityStatus::Missing,
                actual_hash: None,
            };
        }

        match fs::read(p) {
            Ok(data) => {
                let actual = format!("{:x}", Sha256::digest(&data));
                let status = if actual == expected {
                    IntegrityStatus::Ok
                } else {
                    IntegrityStatus::Modified
                };
                IntegrityResult {
                    path: path.to_string(),
                    status,
                    actual_hash: Some(actual),
                }
            }
            Err(e) => IntegrityResult {
                path: path.to_string(),
                status: IntegrityStatus::Error(e.to_string()),
                actual_hash: None,
            },
        }
    }

    /// Hash a file and return SHA-256
    pub fn hash_file(path: &Path) -> Result<String> {
        let data = fs::read(path)?;
        Ok(format!("{:x}", Sha256::digest(&data)))
    }
}

#[derive(Debug, Clone)]
pub struct IntegrityResult {
    pub path:        String,
    pub status:      IntegrityStatus,
    pub actual_hash: Option<String>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum IntegrityStatus {
    Ok,
    Modified,
    Missing,
    Error(String),
}

impl Default for IntegrityChecker {
    fn default() -> Self { Self::new() }
}
