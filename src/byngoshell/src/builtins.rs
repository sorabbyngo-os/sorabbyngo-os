// src/builtins.rs — ByngoShell built-in commands
//
// Every sb_* builtin lives here. Each returns a String output
// and may print directly for streaming commands (sb_ask, sb_listen).
//
// Sorabbyngo Labs — Kisii, Kenya

use std::io::{self, Write};
use std::fs;


use crate::config::{Config, NodeIdSource, Strings};

// ── Colour helpers ────────────────────────────────────────────────────────

fn green(s: impl AsRef<str>) -> String  { format!("\x1b[32m{}\x1b[0m", s.as_ref()) }
fn yellow(s: impl AsRef<str>) -> String { format!("\x1b[33m{}\x1b[0m", s.as_ref()) }
fn red(s: impl AsRef<str>)    -> String { format!("\x1b[31m{}\x1b[0m", s.as_ref()) }
fn cyan(s: impl AsRef<str>)   -> String { format!("\x1b[36m{}\x1b[0m", s.as_ref()) }
fn bold(s: impl AsRef<str>)   -> String { format!("\x1b[1m{}\x1b[0m", s.as_ref()) }
fn dim(s: impl AsRef<str>)    -> String { format!("\x1b[2m{}\x1b[0m", s.as_ref()) }

fn ok_dot()   -> String { green("●") }
fn fail_dot() -> String { red("●") }

// ── sb_id ─────────────────────────────────────────────────────────────────

pub fn sb_id(cfg: &Config, strings: &Strings) -> String {
    let source_str = match cfg.identity.source {
        NodeIdSource::Kernel    => strings.get("builtins.id_source_kernel"),
        NodeIdSource::Userspace => strings.get("builtins.id_source_user"),
    };

    let mesh_str = if cfg.nats_reachable() {
        green(&strings.get("builtins.id_mesh_online"))
    } else {
        dim(&strings.get("builtins.id_mesh_offline"))
    };

    format!(
        "\n  {} {}\n\n  {:12} {}\n  {:12} {}\n  {:12} {}\n  {:12} {}\n",
        bold("◈"),
        bold(&strings.get("builtins.id_title")),
        strings.get("builtins.id_nodeid"),
        cyan(&cfg.identity.nodeid),
        strings.get("builtins.id_short"),
        bold(&cfg.identity.short),
        strings.get("builtins.id_source"),
        dim(source_str),
        strings.get("builtins.id_mesh"),
        mesh_str,
    )
}

// ── sb_health ─────────────────────────────────────────────────────────────

pub fn sb_health(cfg: &Config, strings: &Strings) -> String {
    let nats_ok   = cfg.nats_reachable();
    let ollama_ok = cfg.ollama_reachable();

    // Check Ollama models
    let tier1_ready = ollama_ok && check_ollama_model(&cfg.ollama_url, "phi4-mini");

    // Check kernel version
    let kernel = fs::read_to_string("/proc/version")
        .unwrap_or_default()
        .split_whitespace()
        .nth(2)
        .unwrap_or("unknown")
        .to_string();

    let sorabbyngo_kernel = kernel.contains("sorabbyngo");

    let fmt_status = |ok: bool, ready: String, not_ready: String| -> String {
        if ok {
            format!("{} {}", ok_dot(), green(ready))
        } else {
            format!("{} {}", fail_dot(), dim(not_ready))
        }
    };

    format!(
        "\n  {} {}\n\n  {:16} {}\n  {:16} {}\n  {:16} {}\n  {:16} {}\n  {:16} {}\n  {:16} {}\n",
        bold("◈"),
        bold(&strings.get("builtins.health_title")),
        strings.get("builtins.health_kernel"),
        if sorabbyngo_kernel {
            format!("{} {} {}", ok_dot(), green(&kernel), dim("(sorabbyngo)"))
        } else {
            format!("{} {}", ok_dot(), dim(&kernel))
        },
        strings.get("builtins.health_nats"),
        fmt_status(nats_ok,
            strings.get("builtins.health_online"),
            strings.get("builtins.health_offline")),
        strings.get("builtins.health_ollama"),
        fmt_status(ollama_ok,
            strings.get("builtins.health_online"),
            strings.get("builtins.health_offline")),
        strings.get("builtins.health_tier1"),
        fmt_status(tier1_ready,
            strings.get("builtins.health_ready"),
            strings.get("builtins.health_not_ready")),
        strings.get("builtins.health_mesh"),
        fmt_status(nats_ok,
            strings.get("builtins.health_online"),
            strings.get("builtins.health_offline")),
        "Cloud AI",
        if cfg.cloud_enabled {
            format!("{} enabled (opt-in)", ok_dot())
        } else {
            format!("{} {}", fail_dot(), dim("disabled (ENABLE_CLOUD_AI=false)"))
        },
    )
}

fn check_ollama_model(ollama_url: &str, model: &str) -> bool {
    let url = format!("{}/api/tags", ollama_url);
    // Simple blocking HTTP GET via std (no async needed for health check)
    if let Ok(output) = std::process::Command::new("curl")
        .args(["-s", "--max-time", "1", &url])
        .output()
    {
        let body = String::from_utf8_lossy(&output.stdout);
        return body.contains(model);
    }
    false
}

// ── sb_ask ────────────────────────────────────────────────────────────────

pub fn sb_ask(prompt: &str, cfg: &Config, strings: &Strings) -> String {
    if !cfg.ollama_reachable() {
        return format!("  {}\n", red(&strings.get("builtins.ask_offline")));
    }

    // Build Ollama request
    let system_prompt =
        "You are Chameha, the AI core of SORABBYNGO OS — Africa's sovereign AI \
         operating system. Warm, direct, highly capable. You speak English and \
         Swahili fluently. Keep responses concise — this is a terminal interface. \
         Greeting: 'Sorabbyngo — Rhythm of Digital Life.'";

    let body = serde_json::json!({
        "model": "phi4-mini",
        "prompt": format!("{}\n\nUser: {}\nChameha:", system_prompt, prompt),
        "stream": true,
        "options": { "temperature": 0.7, "num_predict": 512 }
    });

    let url = format!("{}/api/generate", cfg.ollama_url);
    let body_str = serde_json::to_string(&body).unwrap_or_default();

    print!("  {}", cyan(&strings.get("builtins.ask_thinking")));
    io::stdout().flush().ok();

    // Stream via curl subprocess (avoids async complexity in sync context)
    let output = std::process::Command::new("curl")
        .args([
            "-s", "-N", "-X", "POST",
            "-H", "Content-Type: application/json",
            "-d", &body_str,
            &url,
        ])
        .output();

    match output {
        Ok(out) => {
            let raw = String::from_utf8_lossy(&out.stdout);
            let mut response = String::new();

            for line in raw.lines() {
                if line.is_empty() { continue; }
                if let Ok(v) = serde_json::from_str::<serde_json::Value>(line) {
                    if let Some(token) = v["response"].as_str() {
                        print!("{}", token);
                        io::stdout().flush().ok();
                        response.push_str(token);
                    }
                    if v["done"].as_bool().unwrap_or(false) { break; }
                }
            }
            println!();
            String::new() // already printed
        }
        Err(e) => format!("  {}\n", red(&format!("Inference error: {}", e))),
    }
}

// ── sb_policy ─────────────────────────────────────────────────────────────

pub fn sb_policy(
    subject: &str,
    object: &str,
    action: &str,
    _cfg: &Config,
    strings: &Strings,
) -> String {
    // Built-in policy rules (mirrors libsb_sec defaults)
    let decision = evaluate_policy(subject, object, action);
    let (symbol, colour, label) = match decision {
        PolicyDecision::Allow => ("✓", "\x1b[32m", strings.get("builtins.policy_allow")),
        PolicyDecision::Deny  => ("✗", "\x1b[31m", strings.get("builtins.policy_deny")),
        PolicyDecision::Audit => ("~", "\x1b[33m", strings.get("builtins.policy_audit")),
    };

    let (rule_id, reason) = policy_reason(subject, object, action);

    format!(
        "\n  {}{}{}  {}\x1b[0m  {} → {} → {}\n  {} {}\n",
        colour, symbol, "\x1b[0m",
        format!("{}{}{}", colour, label, "\x1b[0m"),
        dim(subject), dim(object), dim(action),
        dim("Rule:"), dim(&format!("{} — {}", rule_id, reason)),
    )
}

#[derive(Debug)]
enum PolicyDecision { Allow, Deny, Audit }

fn evaluate_policy(subject: &str, object: &str, action: &str) -> PolicyDecision {
    // Mirrors libsb_sec built-in rules
    if subject == "sentinel" { return PolicyDecision::Allow; }
    if object.starts_with("cloud.ai") { return PolicyDecision::Deny; }
    if object == "network.external" && action == "connect" { return PolicyDecision::Deny; }
    if object == "audit.ledger" && action == "write" { return PolicyDecision::Deny; }
    if object.starts_with("nodeid") && action == "write" { return PolicyDecision::Deny; }
    if object == "system.health" && action == "read" { return PolicyDecision::Allow; }
    if subject == "novela" && object.starts_with("novela.") { return PolicyDecision::Allow; }
    if subject == "byngox" && object.starts_with("sorabbyngo.") { return PolicyDecision::Allow; }
    if subject == "chameha" && action == "read" { return PolicyDecision::Allow; }
    PolicyDecision::Deny
}

fn policy_reason(subject: &str, object: &str, action: &str) -> (String, String) {
    if subject == "sentinel" {
        return ("POL-001".into(), "Sentinel has full access".into());
    }
    if object.starts_with("cloud.ai") {
        return ("POL-009".into(), "Cloud AI disabled by policy".into());
    }
    if object == "network.external" {
        return ("POL-010".into(), "External network blocked".into());
    }
    if object == "audit.ledger" && action == "write" {
        return ("POL-011".into(), "Audit ledger is append-only via CML".into());
    }
    if object.starts_with("nodeid") {
        return ("POL-013".into(), "NodeID is read-only".into());
    }
    if object == "system.health" {
        return ("POL-012".into(), "Health status is public".into());
    }
    if subject == "novela" && object.starts_with("novela.") {
        return ("POL-002".into(), "Novela reads/writes own data".into());
    }
    ("POL-999".into(), "Default deny".into())
}

// ── sb_send ───────────────────────────────────────────────────────────────

pub fn sb_send(subject: &str, message: &str, cfg: &Config, strings: &Strings) -> String {
    if !cfg.nats_reachable() {
        return format!("  {}\n", red(&strings.get("builtins.send_fail")));
    }

    // Use nats CLI if available, otherwise show guidance
    let result = std::process::Command::new("nats")
        .args(["pub", subject, message, "--server", &cfg.nats_url])
        .output();

    match result {
        Ok(out) if out.status.success() => {
            format!("  {}\n",
                green(strings.fmt("builtins.send_ok", &[("subject", subject)])))
        }
        _ => {
            // nats CLI not available — show manual instructions
            format!(
                "  {} nats pub {} '{}' --server {}\n",
                dim("Run:"), cyan(subject), message, cfg.nats_url
            )
        }
    }
}

// ── sb_net ────────────────────────────────────────────────────────────────

pub fn sb_net(cfg: &Config, strings: &Strings) -> String {
    let nats_ok = cfg.nats_reachable();

    let mut out = format!(
        "\n  {} {}\n\n  {:16} {}\n  {:16} {}\n",
        bold("◈"),
        bold(&strings.get("builtins.net_title")),
        "NodeID",
        cyan(&cfg.identity.nodeid),
        "NATS",
        if nats_ok {
            format!("{} {}", ok_dot(), green(&cfg.nats_url))
        } else {
            format!("{} {}", fail_dot(), dim("offline"))
        },
    );

    // Try to read Byngonet proc stats
    if let Ok(stats) = fs::read_to_string("/proc/byngonet_stats") {
        out.push_str(&format!("\n  {}\n", dim("Byngonet stats:")));
        for line in stats.lines().take(10) {
            out.push_str(&format!("  {}\n", dim(line)));
        }
    } else {
        out.push_str(&format!("\n  {}\n", dim(&strings.get("builtins.net_no_peers"))));
    }

    out
}

// ── help ──────────────────────────────────────────────────────────────────

pub fn help(cfg: &Config) -> String {
    let lang_note = format!("Language: {} (set BYNGO_LANG=sw for Swahili)", cfg.lang.code());
    format!(
        "\n  {} ByngoShell — Sorabbyngo OS\n\n\
         {}\n\
         {}\n\n\
         {}\n\
         {}\n\
         {}\n\
         {}\n\
         {}\n\
         {}\n\
         {}\n\
         {}\n\n\
         {}\n\
         {}\n\n\
         {}\n",
        bold("◈"),
        bold("  Sorabbyngo builtins:"),
        dim("  ─────────────────────────────────────────"),
        format!("  {}  — this node's hardware identity and mesh status",
            bold("sb_id")),
        format!("  {}  — system health: kernel, NATS, Ollama, mesh",
            bold("sb_health")),
        format!("  {} <prompt>  — ask Chameha (Tier 1 local inference)",
            bold("sb_ask")),
        format!("  {} <subject> <object> <action>  — query Sentinel policy",
            bold("sb_policy")),
        format!("  {} <subject> <message>  — publish to NATS",
            bold("sb_send")),
        format!("  {}  — Byngonet mesh topology",
            bold("sb_net")),
        format!("  {}  — show this help",
            bold("help")),
        format!("  {}  — exit ByngoShell",
            bold("exit")),
        bold("  Standard POSIX commands also work:"),
        dim("  ls, cd, cat, grep, ps, top, ssh, git, python3, ..."),
        dim(&format!("  {}", lang_note)),
    )
}
