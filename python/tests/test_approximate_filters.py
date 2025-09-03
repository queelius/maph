"""
Tests for approximate_filters Python module
"""
import pytest
import random
import string
from typing import List


# This will be imported after building
try:
    import approximate_filters as af
except ImportError:
    pytest.skip("approximate_filters module not installed", allow_module_level=True)


class TestBasicFilters:
    """Test basic filter functionality with different storage sizes"""
    
    def test_filter_8bit(self):
        """Test 8-bit filter (FPR ≈ 1/256)"""
        elements = [1, 2, 3, 4, 5, 10, 20, 30]
        builder = af.PerfectHashBuilder(error_rate=0.0)
        filter = af.ApproxFilter8(elements, builder)
        
        # All elements should be in filter
        for elem in elements:
            assert elem in filter
            assert filter.contains(elem)
        
        # Check storage is correct
        assert filter.storage_bytes() == len(elements)  # 8 bits = 1 byte each
        
        # FPR should be approximately 1/256
        assert filter.fpr == pytest.approx(1/256, rel=0.01)
        
        # Test non-members (most should be false)
        non_members = [100, 200, 300, 400, 500]
        false_positives = sum(1 for x in non_members if x in filter)
        assert false_positives <= 1  # At most 1 false positive expected
    
    def test_filter_16bit(self):
        """Test 16-bit filter (FPR ≈ 1/65536)"""
        elements = list(range(50))
        builder = af.PerfectHashBuilder(error_rate=0.0)
        filter = af.ApproxFilter16(elements, builder)
        
        # All elements should be in filter
        for elem in elements:
            assert elem in filter
        
        # Check storage
        assert filter.storage_bytes() == len(elements) * 2  # 16 bits = 2 bytes
        
        # FPR should be approximately 1/65536
        assert filter.fpr == pytest.approx(1/65536, rel=0.01)
        
        # Test many non-members - should have very few false positives
        non_members = list(range(1000, 2000))
        false_positives = sum(1 for x in non_members if x in filter)
        assert false_positives < 5  # Very low FPR
    
    def test_filter_32bit(self):
        """Test 32-bit filter (FPR ≈ 1/2^32)"""
        elements = ["apple", "banana", "cherry", "date", "elderberry"]
        builder = af.PerfectHashBuilder(error_rate=0.0)
        filter = af.ApproxFilter32(elements, builder)
        
        # All elements should be in filter
        for elem in elements:
            assert elem in filter
        
        # Check storage
        assert filter.storage_bytes() == len(elements) * 4  # 32 bits = 4 bytes
        
        # FPR should be extremely low
        assert filter.fpr < 1e-9
        
        # Non-members should almost never be false positives
        non_members = ["grape", "kiwi", "lemon", "mango", "orange"]
        for x in non_members:
            assert x not in filter
    
    def test_filter_64bit(self):
        """Test 64-bit filter (FPR ≈ 1/2^64)"""
        elements = [f"item_{i}" for i in range(20)]
        builder = af.PerfectHashBuilder(error_rate=0.0)
        filter = af.ApproxFilter64(elements, builder)
        
        # All elements should be in filter
        for elem in elements:
            assert elem in filter
        
        # Check storage
        assert filter.storage_bytes() == len(elements) * 8  # 64 bits = 8 bytes
        
        # FPR should be essentially zero
        assert filter.fpr < 1e-18
    
    def test_mixed_types(self):
        """Test filters with mixed Python types"""
        elements = [1, "two", 3.0, (4, 5), None]
        builder = af.PerfectHashBuilder(error_rate=0.0)
        filter = af.ApproxFilter32(elements, builder)
        
        for elem in elements:
            assert elem in filter
        
        # Non-members
        assert "three" not in filter
        assert 2 not in filter
        assert (5, 4) not in filter


class TestThresholdFilters:
    """Test threshold filters with configurable FPR"""
    
    def test_threshold_filter_8bit(self):
        """Test 8-bit threshold filter"""
        elements = [1, 2, 3, 4, 5]
        builder = af.PerfectHashBuilder(error_rate=0.0)
        target_fpr = 0.2  # 20% false positive rate
        
        filter = af.ThresholdFilter8(elements, builder, target_fpr)
        
        # All members should be detected
        for elem in elements:
            assert elem in filter
        
        # Check storage
        assert filter.storage_bytes() == len(elements)
    
    def test_threshold_filter_32bit(self):
        """Test 32-bit threshold filter"""
        elements = list(range(100))
        builder = af.PerfectHashBuilder(error_rate=0.0)
        target_fpr = 0.1  # 10% false positive rate
        
        filter = af.ThresholdFilter32(elements, builder, target_fpr)
        
        # All members should be detected
        for elem in elements:
            assert elem in filter
        
        # Check storage
        assert filter.storage_bytes() == len(elements) * 4
    
    def test_create_threshold_filter_helper(self):
        """Test the create_threshold_filter helper function"""
        elements = ["a", "b", "c", "d", "e"]
        filter = af.create_threshold_filter(elements, target_fpr=0.05, bits=32)
        
        for elem in elements:
            assert elem in filter


class TestCompactLookup:
    """Test compact lookup table functionality"""
    
    def test_compact_lookup_8bit(self):
        """Test 8-bit compact lookup table"""
        keys = ["red", "green", "blue", "yellow"]
        values = [10, 20, 30, 40]
        builder = af.PerfectHashBuilder(error_rate=0.0)
        
        lookup = af.CompactLookup8(keys, values, builder)
        
        # Check lookups
        assert lookup["red"] == 10
        assert lookup["green"] == 20
        assert lookup["blue"] == 30
        assert lookup["yellow"] == 40
        
        # Check storage efficiency
        assert lookup.storage_bytes() == len(keys)  # 1 byte per entry
        
        # Check get with default
        assert lookup.get("purple", 99) == 99
    
    def test_compact_lookup_32bit(self):
        """Test 32-bit compact lookup table"""
        keys = [f"key_{i}" for i in range(50)]
        values = [i * 100 for i in range(50)]
        builder = af.PerfectHashBuilder(error_rate=0.0)
        
        lookup = af.CompactLookup32(keys, values, builder)
        
        # Check lookups
        for i in range(50):
            assert lookup[f"key_{i}"] == i * 100
        
        # Check storage
        assert lookup.storage_bytes() == len(keys) * 4
    
    def test_create_lookup_helper(self):
        """Test the create_lookup helper function"""
        keys = [1, 2, 3, 4, 5]
        values = [100, 200, 300, 400, 500]
        
        # 8-bit lookup
        lookup8 = af.create_lookup(keys, values, bits=8)
        assert lookup8[3] == 300
        
        # 32-bit lookup
        lookup32 = af.create_lookup(keys, values, bits=32)
        assert lookup32[5] == 500
    
    def test_lookup_mismatched_sizes(self):
        """Test that mismatched key/value sizes raise error"""
        keys = [1, 2, 3]
        values = [10, 20]  # Too few values
        builder = af.PerfectHashBuilder(error_rate=0.0)
        
        with pytest.raises(ValueError):
            af.CompactLookup8(keys, values, builder)


class TestBuilderPattern:
    """Test the builder pattern API"""
    
    def test_builder_basic(self):
        """Test basic builder functionality"""
        ph_builder = af.PerfectHashBuilder(error_rate=0.01)
        map_builder = af.ApproxMapBuilder(ph_builder)
        
        elements = [10, 20, 30, 40, 50]
        
        # Build different storage sizes
        filter8 = map_builder.build_filter_8bit(elements)
        filter16 = map_builder.build_filter_16bit(elements)
        filter32 = map_builder.build_filter_32bit(elements)
        filter64 = map_builder.build_filter_64bit(elements)
        
        # All should contain the elements
        for elem in elements:
            assert elem in filter8
            assert elem in filter16
            assert elem in filter32
            assert elem in filter64
        
        # Storage should be different
        assert filter8.storage_bytes() < filter16.storage_bytes()
        assert filter16.storage_bytes() < filter32.storage_bytes()
        assert filter32.storage_bytes() < filter64.storage_bytes()
    
    def test_builder_with_load_factor(self):
        """Test builder with load factor configuration"""
        ph_builder = af.PerfectHashBuilder(error_rate=0.0)
        map_builder = af.ApproxMapBuilder(ph_builder)
        
        # Configure load factor
        map_builder = map_builder.with_load_factor(2.0)
        
        elements = [1, 2, 3, 4, 5]
        filter = map_builder.build_filter_32bit(elements)
        
        # Should still work correctly
        for elem in elements:
            assert elem in filter


class TestConvenienceFunctions:
    """Test module-level convenience functions"""
    
    def test_create_filter(self):
        """Test create_filter convenience function"""
        elements = ["a", "b", "c", "d", "e"]
        
        # Default 32-bit
        filter32 = af.create_filter(elements)
        assert all(x in filter32 for x in elements)
        
        # 8-bit
        filter8 = af.create_filter(elements, bits=8)
        assert all(x in filter8 for x in elements)
        assert filter8.storage_bytes() < filter32.storage_bytes()
        
        # With error rate
        filter_err = af.create_filter(elements, error_rate=0.05)
        assert all(x in filter_err for x in elements)
    
    def test_module_constants(self):
        """Test module-level FPR constants"""
        assert af.FPR_8BIT == pytest.approx(1/256)
        assert af.FPR_16BIT == pytest.approx(1/65536)
        assert af.FPR_32BIT < 1e-9
        assert af.FPR_64BIT < 1e-18
    
    def test_version(self):
        """Test module version"""
        assert af.__version__ == "2.0.0"


class TestErrorRates:
    """Test error rate behavior"""
    
    def test_perfect_hash_error_rate(self):
        """Test that perfect hash error rate affects FNR"""
        elements = list(range(100))
        
        # No error
        builder_perfect = af.PerfectHashBuilder(error_rate=0.0)
        filter_perfect = af.ApproxFilter32(elements, builder_perfect)
        assert filter_perfect.false_negative_rate() == 0.0
        
        # With error
        builder_error = af.PerfectHashBuilder(error_rate=0.1)
        filter_error = af.ApproxFilter32(elements, builder_error)
        assert filter_error.false_negative_rate() > 0.0
    
    def test_storage_size_vs_fpr(self):
        """Test relationship between storage size and FPR"""
        elements = [1, 2, 3, 4, 5]
        builder = af.PerfectHashBuilder(error_rate=0.0)
        
        filter8 = af.ApproxFilter8(elements, builder)
        filter16 = af.ApproxFilter16(elements, builder)
        filter32 = af.ApproxFilter32(elements, builder)
        filter64 = af.ApproxFilter64(elements, builder)
        
        # FPR should decrease with storage size
        assert filter8.fpr > filter16.fpr
        assert filter16.fpr > filter32.fpr
        assert filter32.fpr > filter64.fpr


class TestStress:
    """Stress tests for the filters"""
    
    def test_large_dataset_8bit(self):
        """Test 8-bit filter with large dataset"""
        elements = list(range(10000))
        builder = af.PerfectHashBuilder(error_rate=0.0)
        filter = af.ApproxFilter8(elements, builder)
        
        # Sample check
        sample = random.sample(elements, 100)
        for elem in sample:
            assert elem in filter
        
        # Storage should be efficient
        assert filter.storage_bytes() == 10000
    
    def test_string_dataset(self):
        """Test with random string data"""
        def random_string(length=10):
            return ''.join(random.choices(string.ascii_letters, k=length))
        
        elements = [random_string() for _ in range(1000)]
        builder = af.PerfectHashBuilder(error_rate=0.0)
        filter = af.ApproxFilter16(elements, builder)
        
        # All elements should be members
        sample = random.sample(elements, 100)
        for elem in sample:
            assert elem in filter
        
        # Random non-members should mostly not be in filter
        non_members = [random_string() for _ in range(100)]
        false_positives = sum(1 for elem in non_members if elem in filter)
        assert false_positives < 5  # Less than 5% FPR


if __name__ == "__main__":
    pytest.main([__file__, "-v"])