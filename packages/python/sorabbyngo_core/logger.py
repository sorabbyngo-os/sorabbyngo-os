"""
Standardized logger for all Sorabbyngo services.
"""
from __future__ import annotations
import logging
import sys
from pathlib import Path

LOG_DIR = Path("/var/log/sorabbyngo")
_INITIALIZED = set()

def get_logger(name: str, level: str = "INFO", log_file: bool = True) -> logging.Logger:
    """
    Get a standardized Sorabbyngo logger.
    Adds console + file handlers with consistent formatting.
    """
    logger = logging.getLogger(f"sorabbyngo.{name}")

    if name in _INITIALIZED:
        return logger

    logger.setLevel(getattr(logging, level.upper(), logging.INFO))

    fmt = logging.Formatter(
        "%(asctime)s [%(levelname)s] %(name)s — %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%SZ"
    )

    # Console handler
    ch = logging.StreamHandler(sys.stdout)
    ch.setFormatter(fmt)
    logger.addHandler(ch)

    # File handler
    if log_file:
        try:
            LOG_DIR.mkdir(parents=True, exist_ok=True)
            fh = logging.FileHandler(LOG_DIR / f"{name}.log")
            fh.setFormatter(fmt)
            logger.addHandler(fh)
        except PermissionError:
            pass  # Non-root processes may not write to /var/log

    _INITIALIZED.add(name)
    return logger
