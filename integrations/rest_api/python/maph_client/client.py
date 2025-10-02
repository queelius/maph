"""
maph v3 REST API Python Client

Provides a clean, Pythonic interface to maph v3 databases via REST API.
"""

from typing import Optional, List, Dict, Any
import requests
from dataclasses import dataclass


class MaphError(Exception):
    """Base exception for maph client errors"""
    pass


class StoreNotFoundError(MaphError):
    """Raised when a store does not exist"""
    pass


class KeyNotFoundError(MaphError):
    """Raised when a key does not exist"""
    pass


class StoreExistsError(MaphError):
    """Raised when attempting to create a store that already exists"""
    pass


@dataclass
class StoreStats:
    """Statistics for a maph store"""
    size: int
    load_factor: float


@dataclass
class StoreInfo:
    """Information about a maph store"""
    name: str
    size: int
    load_factor: float


class MaphStore:
    """
    Interface to a single maph store via REST API

    Provides dict-like access to key-value pairs with additional
    maph-specific operations.

    Example:
        store = client.get_store("mydb")
        store["user:1001"] = "Alice Johnson"
        print(store["user:1001"])
        del store["user:1001"]
    """

    def __init__(self, client: 'MaphClient', name: str):
        self._client = client
        self._name = name

    @property
    def name(self) -> str:
        """Store name"""
        return self._name

    def get(self, key: str, default: Optional[str] = None) -> Optional[str]:
        """
        Get value for key

        Args:
            key: Key to retrieve
            default: Value to return if key not found

        Returns:
            Value if found, default otherwise
        """
        try:
            return self[key]
        except KeyNotFoundError:
            return default

    def set(self, key: str, value: str) -> None:
        """
        Set key-value pair

        Args:
            key: Key to set
            value: Value to store
        """
        self[key] = value

    def delete(self, key: str) -> bool:
        """
        Delete a key

        Args:
            key: Key to delete

        Returns:
            True if deleted, False if not found
        """
        try:
            del self[key]
            return True
        except KeyNotFoundError:
            return False

    def contains(self, key: str) -> bool:
        """
        Check if key exists

        Args:
            key: Key to check

        Returns:
            True if key exists
        """
        try:
            self[key]
            return True
        except KeyNotFoundError:
            return False

    def stats(self) -> StoreStats:
        """
        Get store statistics

        Returns:
            StoreStats with size and load_factor
        """
        url = f"{self._client._base_url}/stores/{self._name}/stats"
        response = requests.get(url, timeout=self._client._timeout)

        if response.status_code == 404:
            raise StoreNotFoundError(f"Store '{self._name}' not found")

        response.raise_for_status()
        data = response.json()

        return StoreStats(
            size=data["size"],
            load_factor=data["load_factor"]
        )

    def optimize(self) -> None:
        """
        Optimize store to use perfect hash function

        Converts the store to use a minimal perfect hash function for all
        existing keys, guaranteeing O(1) lookups with zero collisions.

        Use after inserting a known, static set of keys for maximum performance.

        Note:
            - Existing keys get guaranteed O(1) lookups
            - New keys added after optimization use fallback hash
            - Best for mostly-static key sets

        Raises:
            StoreNotFoundError: If store doesn't exist
            MaphError: If optimization fails
        """
        url = f"{self._client._base_url}/stores/{self._name}/optimize"
        response = requests.post(url, timeout=self._client._timeout)

        if response.status_code == 404:
            raise StoreNotFoundError(f"Store '{self._name}' not found")

        if response.status_code != 200:
            data = response.json()
            raise MaphError(data.get("error", "Optimization failed"))

        response.raise_for_status()

    # Dict-like interface

    def __getitem__(self, key: str) -> str:
        """Get value for key (dict-like access)"""
        url = f"{self._client._base_url}/stores/{self._name}/keys/{key}"
        response = requests.get(url, timeout=self._client._timeout)

        if response.status_code == 404:
            data = response.json()
            if "Store not found" in data.get("error", ""):
                raise StoreNotFoundError(f"Store '{self._name}' not found")
            raise KeyNotFoundError(f"Key '{key}' not found")

        response.raise_for_status()
        return response.json()["value"]

    def __setitem__(self, key: str, value: str) -> None:
        """Set key-value pair (dict-like access)"""
        url = f"{self._client._base_url}/stores/{self._name}/keys/{key}"
        response = requests.put(url, data=value, timeout=self._client._timeout)

        if response.status_code == 404:
            raise StoreNotFoundError(f"Store '{self._name}' not found")

        response.raise_for_status()

    def __delitem__(self, key: str) -> None:
        """Delete key (dict-like access)"""
        url = f"{self._client._base_url}/stores/{self._name}/keys/{key}"
        response = requests.delete(url, timeout=self._client._timeout)

        if response.status_code == 404:
            data = response.json()
            if "Store not found" in data.get("error", ""):
                raise StoreNotFoundError(f"Store '{self._name}' not found")
            raise KeyNotFoundError(f"Key '{key}' not found")

        response.raise_for_status()

    def __contains__(self, key: str) -> bool:
        """Check if key exists (dict-like 'in' operator)"""
        return self.contains(key)

    def __repr__(self) -> str:
        return f"MaphStore(name={self._name!r})"


class MaphClient:
    """
    Client for maph v3 REST API server

    Manages connections to maph stores via HTTP REST API.

    Example:
        client = MaphClient("http://localhost:8080")

        # Create a new store
        client.create_store("mydb")

        # Get store and use it
        store = client.get_store("mydb")
        store["greeting"] = "hello world"
        print(store["greeting"])

        # List all stores
        for info in client.list_stores():
            print(f"{info.name}: {info.size} keys, {info.load_factor:.2%} full")
    """

    def __init__(self, base_url: str = "http://localhost:8080", timeout: float = 10.0):
        """
        Initialize maph client

        Args:
            base_url: Base URL of the REST API server
            timeout: Request timeout in seconds
        """
        self._base_url = base_url.rstrip('/')
        self._timeout = timeout

    def create_store(self, name: str) -> MaphStore:
        """
        Create a new store

        Args:
            name: Store name (alphanumeric, hyphens, underscores)

        Returns:
            MaphStore instance

        Raises:
            StoreExistsError: If store already exists
        """
        url = f"{self._base_url}/stores/{name}"
        response = requests.post(url, timeout=self._timeout)

        if response.status_code == 400:
            data = response.json()
            if "already exists" in data.get("error", "").lower():
                raise StoreExistsError(f"Store '{name}' already exists")
            raise MaphError(data.get("error", "Unknown error"))

        response.raise_for_status()
        return MaphStore(self, name)

    def get_store(self, name: str) -> MaphStore:
        """
        Get a reference to an existing store

        Args:
            name: Store name

        Returns:
            MaphStore instance

        Note:
            Does not verify store exists. Use list_stores() to check.
        """
        return MaphStore(self, name)

    def list_stores(self) -> List[StoreInfo]:
        """
        List all stores with their statistics

        Returns:
            List of StoreInfo objects
        """
        url = f"{self._base_url}/stores"
        response = requests.get(url, timeout=self._timeout)
        response.raise_for_status()

        stores = response.json()
        return [
            StoreInfo(
                name=store["name"],
                size=store["size"],
                load_factor=store["load_factor"]
            )
            for store in stores
        ]

    def __repr__(self) -> str:
        return f"MaphClient(base_url={self._base_url!r})"
