#!/usr/bin/env python3
"""Real-world application examples using approximate_filters."""

import approximate_filters as af
import hashlib
import random
from datetime import datetime, timedelta
from typing import List, Set, Tuple

class BloomFilterCache:
    """Distributed cache with Bloom filter for quick negative lookups."""
    
    def __init__(self, expected_items: int = 10000, fpr_target: float = 0.01):
        self.cache = {}  # Actual cache storage
        self.filter = None
        self.expected_items = expected_items
        self.fpr_target = fpr_target
        self._rebuild_filter()
    
    def _rebuild_filter(self):
        """Rebuild the Bloom filter from current cache keys."""
        if self.cache:
            # Choose storage size based on FPR target
            if self.fpr_target > 0.1:
                bits = 8
            elif self.fpr_target > 0.001:
                bits = 16
            elif self.fpr_target > 0.000001:
                bits = 32
            else:
                bits = 64
            
            self.filter = af.create_filter(list(self.cache.keys()), bits=bits)
    
    def get(self, key: str):
        """Get value with Bloom filter pre-check."""
        # Quick negative check
        if self.filter and key not in self.filter:
            return None  # Definitely not in cache
        
        # Might be in cache, check actual storage
        return self.cache.get(key)
    
    def set(self, key: str, value: any):
        """Set value and update filter if needed."""
        self.cache[key] = value
        
        # Rebuild filter periodically
        if len(self.cache) % 1000 == 0:
            self._rebuild_filter()
    
    def stats(self):
        """Get cache statistics."""
        return {
            "items": len(self.cache),
            "filter_size": self.filter.storage_bytes() if self.filter else 0,
            "cache_size": sum(len(str(k)) + len(str(v)) for k, v in self.cache.items()),
            "fpr": self.filter.fpr if self.filter else 0
        }


class URLDeduplicator:
    """Web crawler URL deduplication using approximate filters."""
    
    def __init__(self):
        self.seen_urls_filter = None
        self.crawled_count = 0
        self.buffer = []
        self.buffer_size = 10000
    
    def should_crawl(self, url: str) -> bool:
        """Check if URL should be crawled."""
        # Normalize URL
        normalized = self._normalize_url(url)
        
        # Check filter
        if self.seen_urls_filter and normalized in self.seen_urls_filter:
            return False  # Likely already crawled
        
        # Add to buffer
        self.buffer.append(normalized)
        
        # Rebuild filter when buffer is full
        if len(self.buffer) >= self.buffer_size:
            self._flush_buffer()
        
        return True
    
    def _normalize_url(self, url: str) -> str:
        """Normalize URL for deduplication."""
        # Simple normalization (real implementation would be more complex)
        url = url.lower().strip()
        if url.endswith('/'):
            url = url[:-1]
        return url.replace('www.', '')
    
    def _flush_buffer(self):
        """Flush buffer to filter."""
        if self.buffer:
            # Create new filter with all URLs
            all_urls = self.buffer
            if self.seen_urls_filter:
                # In practice, we'd merge filters or use a scalable solution
                pass
            
            # Use 16-bit for reasonable accuracy with web-scale data
            self.seen_urls_filter = af.create_filter(all_urls, bits=16)
            self.crawled_count += len(self.buffer)
            self.buffer = []
    
    def get_stats(self):
        """Get crawler statistics."""
        return {
            "crawled": self.crawled_count,
            "buffered": len(self.buffer),
            "filter_size_bytes": self.seen_urls_filter.storage_bytes() if self.seen_urls_filter else 0,
            "estimated_memory_saved": self.crawled_count * 100  # Avg URL length
        }


class SpamDetector:
    """Email spam detection using compact lookups for feature scoring."""
    
    def __init__(self):
        # Known spam indicators with scores
        spam_words = {
            "viagra": 10,
            "casino": 8,
            "winner": 5,
            "congratulations": 4,
            "click here": 7,
            "limited offer": 6,
            "act now": 5,
            "free money": 9,
            "guarantee": 3,
            "no risk": 6
        }
        
        # Known ham (good) indicators with negative scores
        ham_words = {
            "meeting": -3,
            "project": -3,
            "deadline": -2,
            "invoice": -2,
            "report": -2,
            "schedule": -2
        }
        
        # Create compact lookups for fast scoring
        all_words = list(spam_words.keys()) + list(ham_words.keys())
        all_scores = list(spam_words.values()) + list(ham_words.values())
        
        # Use 8-bit storage (scores fit in small range)
        self.score_lookup = af.create_lookup(all_words, 
                                            [s + 20 for s in all_scores],  # Offset to make positive
                                            bits=8)
        
        # Spam threshold
        self.threshold = 15
    
    def classify(self, email_text: str) -> Tuple[str, int]:
        """Classify email as spam or ham."""
        words = email_text.lower().split()
        total_score = 0
        
        for word in words:
            score = self.score_lookup.get(word, 20)  # Default is neutral (20)
            total_score += (score - 20)  # Remove offset
        
        classification = "SPAM" if total_score > self.threshold else "HAM"
        return classification, total_score


class RateLimiter:
    """API rate limiting using threshold filters."""
    
    def __init__(self, requests_per_minute: int = 60):
        self.rpm_limit = requests_per_minute
        self.window_start = datetime.now()
        self.current_window_filter = None
        self.request_count = 0
        
    def allow_request(self, client_id: str) -> bool:
        """Check if request should be allowed."""
        now = datetime.now()
        
        # Reset window if minute passed
        if (now - self.window_start).seconds >= 60:
            self._reset_window()
        
        # Create unique key for this request
        request_key = f"{client_id}:{self.request_count}"
        
        # Check if client has exceeded limit using threshold filter
        if self.current_window_filter:
            # Use threshold to implement probabilistic rate limiting
            if request_key in self.current_window_filter:
                return False
        
        self.request_count += 1
        
        # Rebuild filter periodically
        if self.request_count % 100 == 0:
            self._update_filter()
        
        return True
    
    def _reset_window(self):
        """Reset the time window."""
        self.window_start = datetime.now()
        self.request_count = 0
        self.current_window_filter = None
    
    def _update_filter(self):
        """Update the rate limit filter."""
        # Create filter for current window
        # In practice, would track actual requests
        pass


class DatabaseQueryCache:
    """Database query result caching with membership testing."""
    
    def __init__(self):
        self.query_hashes = []
        self.result_cache = {}
        self.filter = None
        
    def get_cached_result(self, query: str):
        """Get cached query result if available."""
        query_hash = self._hash_query(query)
        
        # Quick check with filter
        if self.filter and query_hash not in self.filter:
            return None  # Definitely not cached
        
        # Check actual cache
        return self.result_cache.get(query_hash)
    
    def cache_result(self, query: str, result: any):
        """Cache a query result."""
        query_hash = self._hash_query(query)
        
        self.query_hashes.append(query_hash)
        self.result_cache[query_hash] = result
        
        # Rebuild filter periodically
        if len(self.query_hashes) % 100 == 0:
            self.filter = af.create_filter(self.query_hashes, bits=32)
    
    def _hash_query(self, query: str) -> str:
        """Hash query for consistent identification."""
        # Normalize and hash query
        normalized = ' '.join(query.lower().split())
        return hashlib.md5(normalized.encode()).hexdigest()
    
    def get_stats(self):
        """Get cache statistics."""
        return {
            "cached_queries": len(self.result_cache),
            "filter_size": self.filter.storage_bytes() if self.filter else 0,
            "avg_result_size": sum(len(str(r)) for r in self.result_cache.values()) / max(len(self.result_cache), 1)
        }


class GeoIPFilter:
    """Geolocation-based IP filtering."""
    
    def __init__(self):
        # Simulate IP ranges for different countries (in practice, use real data)
        self.country_filters = {}
        
        # Create filters for different regions
        us_ips = [f"72.{random.randint(0,255)}.{random.randint(0,255)}.{random.randint(0,255)}" 
                  for _ in range(1000)]
        eu_ips = [f"85.{random.randint(0,255)}.{random.randint(0,255)}.{random.randint(0,255)}" 
                  for _ in range(1000)]
        asia_ips = [f"122.{random.randint(0,255)}.{random.randint(0,255)}.{random.randint(0,255)}" 
                    for _ in range(1000)]
        
        # Use different accuracy for different regions
        self.country_filters['US'] = af.create_filter(us_ips, bits=32)  # High accuracy
        self.country_filters['EU'] = af.create_filter(eu_ips, bits=16)  # Medium accuracy
        self.country_filters['ASIA'] = af.create_filter(asia_ips, bits=8)  # Lower accuracy
    
    def get_region(self, ip: str) -> str:
        """Get probable region for IP."""
        for region, filter_obj in self.country_filters.items():
            if ip in filter_obj:
                return region
        return "UNKNOWN"
    
    def is_blocked_region(self, ip: str, blocked_regions: List[str]) -> bool:
        """Check if IP is from a blocked region."""
        for region in blocked_regions:
            if region in self.country_filters and ip in self.country_filters[region]:
                return True
        return False


def demo_cache():
    """Demonstrate cache with Bloom filter."""
    print("=== Bloom Filter Cache Demo ===\n")
    
    cache = BloomFilterCache(expected_items=1000, fpr_target=0.01)
    
    # Add some items
    for i in range(100):
        cache.set(f"key_{i}", f"value_{i}")
    
    # Test lookups
    hits = 0
    misses = 0
    false_positives = 0
    
    for i in range(200):
        key = f"key_{i}"
        result = cache.get(key)
        
        if result:
            hits += 1
        elif i < 100:  # Should have been found
            misses += 1
        else:  # Correctly not found
            if cache.filter and key in cache.filter:
                false_positives += 1
    
    stats = cache.stats()
    print(f"Cache Stats:")
    print(f"  Items: {stats['items']}")
    print(f"  Filter size: {stats['filter_size']} bytes")
    print(f"  Cache size: {stats['cache_size']} bytes")
    print(f"  Hits: {hits}, Misses: {misses}, False positives: {false_positives}")
    print()


def demo_url_dedup():
    """Demonstrate URL deduplication."""
    print("=== URL Deduplicator Demo ===\n")
    
    dedup = URLDeduplicator()
    
    # Simulate crawling
    urls = [
        "https://example.com/page1",
        "https://www.example.com/page1",  # Duplicate (normalized)
        "https://example.com/page2",
        "https://example.com/page1/",  # Duplicate (normalized)
        "https://example.com/page3",
    ] * 100  # Repeat to trigger filter creation
    
    crawled = []
    skipped = 0
    
    for url in urls:
        if dedup.should_crawl(url):
            crawled.append(url)
        else:
            skipped += 1
    
    stats = dedup.get_stats()
    print(f"Crawl Stats:")
    print(f"  Total URLs: {len(urls)}")
    print(f"  Crawled: {len(crawled)}")
    print(f"  Skipped: {skipped}")
    print(f"  Filter size: {stats['filter_size_bytes']} bytes")
    print(f"  Est. memory saved: {stats['estimated_memory_saved']} bytes")
    print()


def demo_spam_detection():
    """Demonstrate spam detection."""
    print("=== Spam Detector Demo ===\n")
    
    detector = SpamDetector()
    
    emails = [
        "Meeting scheduled for tomorrow to discuss the project deadline",
        "Congratulations! You're a WINNER! Click here for FREE MONEY!",
        "Please review the attached invoice and report",
        "Limited offer! Act now! Viagra casino guarantee no risk!",
        "Can we schedule a meeting to discuss the project schedule?",
    ]
    
    for email in emails:
        classification, score = detector.classify(email)
        print(f"{classification} (score: {score}): {email[:50]}...")
    print()


if __name__ == "__main__":
    demo_cache()
    demo_url_dedup()
    demo_spam_detection()
    
    print("\n" + "="*50)
    print("Real-world application demos completed!")