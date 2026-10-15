"""Canonical ServiceSupervisor lives in ServiceSupervisor.py; re-export."""

from eaglegd.app.ServiceSupervisor import ServiceStatus, ServiceSupervisor

__all__ = ["ServiceStatus", "ServiceSupervisor"]
