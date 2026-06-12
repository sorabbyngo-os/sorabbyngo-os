// src/config.rs — ByngoShell runtime configuration
//
// Reads BYNGO_LANG, ENABLE_CLOUD_AI, NATS_URL and derives
// system state from /proc/sorabbyngo/nodeid and /sys interfaces.
//

use std::collections::HashMap;
use std::fs;
use std::net::TcpStream;
use std::path::PathBuf;

use sha2::{Digest, Sha256};

/// Language code — en (English) or sw (Swahili)
#[derive(Debug, Clone, PartialEq)]
pub enum Lang {
    En,
    Sw,
}

impl Lang {
    pub fn from_env() -> Self {
        match std::env::var("BYNGO_LANG")
            .unwrap_or_default()
            .to_lowercase()
            .as_str()
        {
            "sw" | "swahili" => Lang::Sw,
            _ => Lang::En,
        }
    }

    pub fn code(&self) -> &'static str {
        match self {
            Lang::En => "en",
            Lang::Sw => "sw",
        }
    }
}

/// Where the NodeID came from
#[derive(Debug, Clone, PartialEq)]
pub enum NodeIdSource {
    Kernel,    // /proc/sorabbyngo/nodeid
    Userspace, // SHA-256(CPUID|MAC)
}

/// This node's hardware identity
#[derive(Debug, Clone)]
pub struct NodeIdentity {
    pub nodeid: String,     // 64-char hex
    pub short:  String,     // first 8 chars
    pub source: NodeIdSource,
}

impl NodeIdentity {
    /// Derive or read NodeID. Mirrors identity.c fallback logic.
    pub fn load() -> Self {
        // Try kernel proc entry first
        if let Ok(id) = fs::read_to_string("/proc/sorabbyngo/nodeid") {
            let id = id.trim().to_string();
            if id.len() == 64 && id.chars().all(|c| c.is_ascii_hexdigit()) {
                let short = id[..8].to_string();
                return NodeIdentity { nodeid: id, short, source: NodeIdSource::Kernel };
            }
        }

        // Userspace fallback: SHA-256(cpu_model|mac)
        let cpu  = Self::read_cpu_model();
        let mac  = Self::read_primary_mac();
        let input = format!("{}|{}", cpu, mac);

        let hash   = Sha256::digest(input.as_bytes());
        let nodeid = hex::encode(hash);
        let short  = nodeid[..8].to_string();

        NodeIdentity { nodeid, short, source: NodeIdSource::Userspace }
    }

    fn read_cpu_model() -> String {
        if let Ok(content) = fs::read_to_string("/proc/cpuinfo") {
            for line in content.lines() {
                if line.starts_with("model name") {
                    if let Some(val) = line.splitn(2, ':').nth(1) {
                        return val.trim().to_string();
                    }
                }
            }
        }
        "unknown-cpu".to_string()
    }

    fn read_primary_mac() -> String {
        // Read first non-loopback interface MAC from /sys/class/net/
        if let Ok(entries) = fs::read_dir("/sys/class/net") {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().to_string();
                if name == "lo" { continue; }
                let mac_path = format!("/sys/class/net/{}/address", name);
                if let Ok(mac) = fs::read_to_string(&mac_path) {
                    let mac = mac.trim().to_string();
                    if mac != "00:00:00:00:00:00" {
                        return mac;
                    }
                }
            }
        }
        "00:00:00:00:00:00".to_string()
    }
}

/// Runtime configuration for ByngoShell
#[derive(Debug, Clone)]
pub struct Config {
    pub lang:          Lang,
    pub identity:      NodeIdentity,
    pub nats_url:      String,
    pub ollama_url:    String,
    pub cloud_enabled: bool,
    pub data_dir:      PathBuf,
}

impl Config {
    pub fn load() -> Self {
        let lang     = Lang::from_env();
        let identity = NodeIdentity::load();

        let nats_url = std::env::var("NATS_URL")
            .unwrap_or_else(|_| "nats://localhost:4222".to_string());

        let ollama_url = std::env::var("OLLAMA_URL")
            .unwrap_or_else(|_| "http://localhost:11434".to_string());

        let cloud_enabled = std::env::var("ENABLE_CLOUD_AI")
            .unwrap_or_default()
            .to_lowercase()
            == "true";

        // Storage: /data/cml if exists, else ~/.sorabbyngo
        let data_dir = if PathBuf::from("/data").exists() {
            PathBuf::from("/data/cml")
        } else {
            dirs_home().join(".sorabbyngo").join("cml")
        };

        Config { lang, identity, nats_url, ollama_url, cloud_enabled, data_dir }
    }

    /// Check if NATS is reachable (fast TCP probe)
    pub fn nats_reachable(&self) -> bool {
        let addr = self.nats_url
            .trim_start_matches("nats://")
            .to_string();
        TcpStream::connect_timeout(
            &addr.parse().unwrap_or("127.0.0.1:4222".parse().unwrap()),
            std::time::Duration::from_millis(500),
        ).is_ok()
    }

    /// Check if Ollama is reachable (fast TCP probe on port 11434)
    pub fn ollama_reachable(&self) -> bool {
        TcpStream::connect_timeout(
            &"127.0.0.1:11434".parse().unwrap(),
            std::time::Duration::from_millis(500),
        ).is_ok()
    }
}

fn dirs_home() -> PathBuf {
    std::env::var("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("/tmp"))
}

/// Language strings loaded from lang/en.toml or lang/sw.toml
#[derive(Debug, Clone)]
pub struct Strings(pub HashMap<String, String>);

impl Strings {
    pub fn load(lang: &Lang) -> Self {
        let code = lang.code();

        // Try binary-embedded strings first, then filesystem
        let content = match code {
            "sw" => include_str!("../lang/sw.toml"),
            _    => include_str!("../lang/en.toml"),
        };

        let map = Self::parse_toml_flat(content);
        Strings(map)
    }

    /// Parse flat TOML (section.key = "value") into a flat HashMap
    fn parse_toml_flat(content: &str) -> HashMap<String, String> {
        let mut map   = HashMap::new();
        let mut section = String::new();

        for line in content.lines() {
            let line = line.trim();
            if line.starts_with('#') || line.is_empty() { continue; }

            if line.starts_with('[') && line.ends_with(']') {
                section = line[1..line.len()-1].to_string();
                continue;
            }

            if let Some(eq) = line.find('=') {
                let key = line[..eq].trim();
                let val = line[eq+1..].trim().trim_matches('"');
                let full_key = if section.is_empty() {
                    key.to_string()
                } else {
                    format!("{}.{}", section, key)
                };
                map.insert(full_key, val.to_string());
            }
        }
        map
    }

    pub fn get(&self, key: &str) -> String {
        self.0.get(key).cloned().unwrap_or_else(|| key.to_string())
    }

    pub fn fmt(&self, key: &str, vars: &[(&str, &str)]) -> String {
        let mut s = self.get(key).to_string();
        for (k, v) in vars {
            s = s.replace(&format!("{{{}}}", k), v);
        }
        s
    }
}
