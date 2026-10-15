"""Canonical AppConfig lives in AppConfig.py; this module re-exports it."""

from eaglegd.app.AppConfig import AppConfig

__all__ = ["AppConfig"]
