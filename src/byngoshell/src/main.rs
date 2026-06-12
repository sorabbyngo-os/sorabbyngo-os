mod config;
mod builtins;

use std::collections::HashMap;
use std::env;
use std::path::PathBuf;
use std::process::{Command, Stdio};

use reedline::{
    DefaultCompleter, DefaultHinter, DefaultValidator, FileBackedHistory,
    Reedline, Signal,
};
use reedline::default_emacs_keybindings;
use reedline::{Prompt, PromptEditMode, PromptHistorySearch, PromptHistorySearchStatus};

use config::{Config, Strings};

const BANNER: &str = r#"
  ╔══════════════════════════════════════════════════════════════╗
  ║  ░█▀▄░█░█░█▀█░█▀▀░█▀█░█▀▀░█░█░█▀▀░█░░░█░                  ║
  ║  ░█▀▄░░█░░█░█░█░█░█░█░▀▀█░█▀█░█▀▀░█░░░█░                  ║
  ║  ░▀▀░░░▀░░▀░▀░▀▀▀░▀▀▀░▀▀▀░▀░▀░▀▀▀░▀▀▀░▀▀▀                  ║
  ╚══════════════════════════════════════════════════════════════╝"#;

struct ByngoPrompt { node_short: String, lang: String }

impl ByngoPrompt {
    fn new(cfg: &Config) -> Self {
        ByngoPrompt { node_short: cfg.identity.short.clone(), lang: cfg.lang.code().to_string() }
    }
    fn cwd_str() -> String {
        let cwd = env::current_dir().unwrap_or_else(|_| PathBuf::from("~"));
        let home = env::var("HOME").unwrap_or_default();
        let s = cwd.to_string_lossy().to_string();
        if s.starts_with(&home) { format!("~{}", &s[home.len()..]) } else { s }
    }
}

impl Prompt for ByngoPrompt {
    fn render_prompt_left(&self) -> std::borrow::Cow<'_, str> {
        let user = std::env::var("USER").unwrap_or_else(|_| "user".to_string());
        format!("byngo@{}:{} $ ", user, Self::cwd_str()).into()
    }
    fn render_prompt_right(&self) -> std::borrow::Cow<str> { "".into() }
    fn render_prompt_indicator(&self, _: PromptEditMode) -> std::borrow::Cow<str> { "".into() }
    fn render_prompt_multiline_indicator(&self) -> std::borrow::Cow<str> { "  ".into() }
    fn render_prompt_history_search_indicator(&self, h: PromptHistorySearch) -> std::borrow::Cow<str> {
        format!("(reverse-i-search: {}) ", h.term).into()
    }
}

fn shell_split(line: &str) -> Vec<String> {
    if line.contains('|') || line.contains('>') || line.contains('<') {
        return vec!["/bin/sh".into(), "-c".into(), line.into()];
    }
    let mut tokens = Vec::new();
    let mut current = String::new();
    let mut in_single = false;
    let mut in_double = false;
    for ch in line.chars() {
        match ch {
            '\'' if !in_double => in_single = !in_single,
            '"' if !in_single => in_double = !in_double,
            ' ' | '\t' if !in_single && !in_double => {
                if !current.is_empty() { tokens.push(current.clone()); current.clear(); }
            }
            _ => current.push(ch),
        }
    }
    if !current.is_empty() { tokens.push(current); }
    tokens
}

fn run_external(cmd: &str, args: &[String]) {
    match Command::new(cmd).args(args)
        .stdin(Stdio::inherit()).stdout(Stdio::inherit()).stderr(Stdio::inherit())
        .spawn()
    {
        Ok(mut c) => { let _ = c.wait(); }
        Err(e) if e.kind() == std::io::ErrorKind::NotFound =>
            eprintln!("  byngoshell: {}: command not found", cmd),
        Err(e) => eprintln!("  byngoshell: {}: {}", cmd, e),
    }
}

fn dispatch(line: &str, cfg: &Config, strings: &Strings, env_vars: &mut HashMap<String, String>) {
    let line = line.trim();
    if line.is_empty() || line.starts_with('#') { return; }

    if let Some(eq) = line.find('=') {
        if !line[..eq].contains(char::is_whitespace) {
            let key = line[..eq].to_string();
            let val = line[eq+1..].trim_matches('"').to_string();
            env_vars.insert(key.clone(), val.clone());
            env::set_var(&key, &val);
            return;
        }
    }

    let mut parts = shell_split(line);
    if parts.is_empty() { return; }
    for part in &mut parts {
        if part.starts_with('$') {
            let key = &part[1..].to_string();
            *part = env::var(key).or_else(|_| env_vars.get(key).cloned().ok_or(env::VarError::NotPresent)).unwrap_or_default();
        }
    }

    let cmd = parts[0].clone();
    let args = parts[1..].to_vec();

    match cmd.as_str() {
        "sb_id"     => print!("{}", builtins::sb_id(cfg, strings)),
        "sb_health" => print!("{}", builtins::sb_health(cfg, strings)),
        "sb_ask" => {
            if args.is_empty() { println!("  Usage: sb_ask <prompt>"); }
            else { let o = builtins::sb_ask(&args.join(" "), cfg, strings); if !o.is_empty() { print!("{}", o); } }
        }
        "sb_policy" => {
            if args.len() < 3 { println!("  Usage: sb_policy <subject> <object> <action>"); }
            else { print!("{}", builtins::sb_policy(&args[0], &args[1], &args[2], cfg, strings)); }
        }
        "sb_send" => {
            if args.len() < 2 { println!("  Usage: sb_send <subject> <message>"); }
            else { print!("{}", builtins::sb_send(&args[0], &args[1..].join(" "), cfg, strings)); }
        }
        "sb_net"    => print!("{}", builtins::sb_net(cfg, strings)),
        "sb_nodeid" => println!("{}", cfg.identity.nodeid),
        "sb_lang"   => {
            if args.is_empty() { println!("  Language: {} | Usage: sb_lang <en|sw>", cfg.lang.code()); }
            else { env::set_var("BYNGO_LANG", &args[0]); println!("  Set to '{}'. Restart to apply.", args[0]); }
        }
        "help" | "?" => print!("{}", builtins::help(cfg)),
        "clear" => { print!("\x1b[2J\x1b[H"); println!("{}", BANNER); }
        "exit" | "quit" => {
            println!("\n  {}\n", strings.get("shell.goodbye"));
            std::process::exit(0);
        }
        "cd" => {
            let path = args.first().map(|s| s.as_str()).unwrap_or("~");
            let target = if path == "~" || path.starts_with("~/") {
                PathBuf::from(format!("{}{}", env::var("HOME").unwrap_or_default(), &path[1..]))
            } else { PathBuf::from(path) };
            if let Err(e) = env::set_current_dir(&target) { eprintln!("  cd: {}: {}", path, e); }
        }
        "pwd"  => println!("{}", env::current_dir().unwrap_or_default().display()),
        "echo" => println!("{}", args.join(" ")),
        "export" => {
            for arg in &args {
                if let Some(eq) = arg.find('=') {
                    env::set_var(&arg[..eq], &arg[eq+1..]);
                }
            }
        }
        "source" | "." => {
            if let Some(path) = args.first() {
                if let Ok(content) = std::fs::read_to_string(path) {
                    for l in content.lines() { dispatch(l, cfg, strings, env_vars); }
                }
            }
        }
        _ => run_external(&cmd, &args),
    }
}

fn make_completer() -> DefaultCompleter {
    DefaultCompleter::new_with_wordlen(vec![
        "sb_id".into(), "sb_health".into(), "sb_ask".into(), "sb_policy".into(),
        "sb_send".into(), "sb_net".into(), "sb_lang".into(), "sb_nodeid".into(),
        "help".into(), "exit".into(), "cd".into(), "ls".into(), "cat".into(),
        "echo".into(), "clear".into(), "pwd".into(), "git".into(), "python3".into(),
    ], 1)
}

fn main() {
    let cfg     = Config::load();
    let strings = Strings::load(&cfg.lang);

    let history_path = {
        let home = env::var("HOME").map(PathBuf::from).unwrap_or_else(|_| PathBuf::from("/tmp"));
        let dir = home.join(".sorabbyngo");
        let _ = std::fs::create_dir_all(&dir);
        dir.join("byngoshell_history")
    };

    let history   = Box::new(FileBackedHistory::with_file(1000, history_path)
        .unwrap_or_else(|_| FileBackedHistory::new(1000)));
    let completer = Box::new(make_completer());
    let hinter    = Box::new(DefaultHinter::default());
    let validator = Box::new(DefaultValidator);
    let keybindings = default_emacs_keybindings();

    let mut rl = Reedline::create()
        .with_history(history)
        .with_completer(completer)
        .with_hinter(hinter)
        .with_validator(validator)
        .with_edit_mode(Box::new(reedline::Emacs::new(keybindings)));

    let prompt = ByngoPrompt::new(&cfg);
    let mut env_vars: HashMap<String, String> = HashMap::new();

    println!("{}", BANNER);

    loop {
        match rl.read_line(&prompt) {
            Ok(Signal::Success(line)) => dispatch(line.trim(), &cfg, &strings, &mut env_vars),
            Ok(Signal::CtrlC) => println!("  (Use 'exit' or Ctrl+D to quit)"),
            Ok(Signal::CtrlD) => { println!("\n  {}\n", strings.get("shell.goodbye")); break; }
            Err(e) => { eprintln!("  Error: {}", e); break; }
        }
    }
}
