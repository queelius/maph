"""
maph-client: Python client for maph v3 REST API

A Pythonic interface to maph v3 memory-mapped perfect hash databases
via the REST API server.
"""

from .client import MaphClient, MaphStore

__version__ = "3.0.0"
__all__ = ["MaphClient", "MaphStore"]
