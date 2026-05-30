"""
Configuration loader for Sorabbyngo environments.
Supports YAML with inheritance via 'extends' key.
"""
from __future__ import annotations
import os
import copy
from pathlib import Path
from typing import Any, Optional

try:
    import yaml
    YAML_AVAILABLE = True
except ImportError:
    YAML_AVAILABLE = False

ENV_VAR = "SORABBYNGO_ENV"
CONFIG_ROOT = Path(__file__).parent.parent.parent.parent / "environments"

def get_env() -> str:
    """Return current environment name from SORABBYNGO_ENV or default to dev."""
    return os.environ.get(ENV_VAR, "dev").lower()

def _deep_merge(base: dict, override: dict) -> dict:
    """Recursively merge override into base."""
    result = copy.deepcopy(base)
    for k, v in override.items():
        if k == "extends":
            continue
        if isinstance(v, dict) and isinstance(result.get(k), dict):
            result[k] = _deep_merge(result[k], v)
        else:
            result[k] = v
    return result

def load_config(env: Optional[str] = None, config_root: Optional[Path] = None) -> "SorabbyngoConfig":
    """Load environment config with inheritance resolution."""
    if not YAML_AVAILABLE:
        raise ImportError("PyYAML required: pip install pyyaml")

    env = env or get_env()
    root = config_root or CONFIG_ROOT

    config_path = root / env / "config.yaml"
    if not config_path.exists():
        raise FileNotFoundError(f"Config not found: {config_path}")

    with open(config_path) as f:
        raw = yaml.safe_load(f)

    # Resolve inheritance
    if "extends" in raw:
        parent_path = (config_path.parent / raw["extends"]).resolve()
        with open(parent_path) as f:
            parent = yaml.safe_load(f)
        raw = _deep_merge(parent, raw)

    return SorabbyngoConfig(raw, env=env)


class SorabbyngoConfig:
    """Typed config wrapper with dot-access and validation."""

    def __init__(self, data: dict, env: str = "dev"):
        self._data = data
        self.env   = env

    def get(self, *keys: str, default: Any = None) -> Any:
        """Nested key access: config.get('security', 'apparmor')"""
        d = self._data
        for k in keys:
            if not isinstance(d, dict):
                return default
            d = d.get(k, default)
        return d

    def is_prod(self) -> bool:
        return self.env == "prod"

    def is_dev(self) -> bool:
        return self.env == "dev"

    def is_lab(self) -> bool:
        return self.env == "lab"

    def is_sandbox(self) -> bool:
        return self.env == "sandbox"

    def nats_url(self) -> str:
        return self.get("nats", "url", default="nats://localhost:4222")

    def log_level(self) -> str:
        return self.get("logging", "level", default="INFO")

    def ai_tier(self) -> int:
        return self.get("ai", "tier", default=1)

    def hardening_enabled(self) -> bool:
        return self.get("kernel", "hardening", default=True)

    def __repr__(self) -> str:
        return f"SorabbyngoConfig(env={self.env})"
