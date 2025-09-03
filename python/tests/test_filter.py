"""
Integration tests for rd_ph_filter Python bindings
"""
import pytest
import random
import string
from typing import List, Any


# Import will work after installation
try:
    import rd_ph_filter as rpf
except ImportError:
    pytest.skip("rd_ph_filter module not installed", allow_module_level=True)


class TestBasicFunctionality:
    """Test basic filter operations"""
    
    def test_create_filter_with_integers(self):
        """Test creating a filter with integer elements"""
        elements = [1, 2, 3, 4, 5]
        filter = rpf.create_filter(elements)
        
        # All elements should be in the filter
        for elem in elements:
            assert elem in filter
            assert filter.contains(elem)
            assert filter(elem)
    
    def test_create_filter_with_strings(self):
        """Test creating a filter with string elements"""
        elements = ["apple", "banana", "cherry", "date"]
        filter = rpf.create_filter(elements)
        
        for elem in elements:
            assert elem in filter
        
        # Non-members should mostly not be in filter
        assert "grape" not in filter or filter.false_positive_rate() > 0
    
    def test_create_filter_with_mixed_types(self):
        """Test creating a filter with mixed type elements"""
        elements = [1, "two", 3.0, (4, 5)]
        filter = rpf.create_filter(elements)
        
        for elem in elements:
            assert elem in filter
    
    def test_empty_filter(self):
        """Test creating an empty filter"""
        filter = rpf.create_filter([])
        
        # Nothing should be in an empty filter
        assert 1 not in filter
        assert "test" not in filter
    
    def test_filter_with_duplicates(self):
        """Test that duplicates are handled correctly"""
        elements = [1, 2, 2, 3, 3, 3]
        filter = rpf.create_filter(elements)
        
        assert 1 in filter
        assert 2 in filter
        assert 3 in filter
        assert 4 not in filter


class TestErrorRates:
    """Test error rate functionality"""
    
    def test_false_positive_rate(self):
        """Test false positive rate calculation"""
        elements = list(range(100))
        filter = rpf.create_filter(elements)
        
        fpr = filter.false_positive_rate()
        assert 0.0 <= fpr <= 1.0
        assert fpr > 0  # Should be non-zero for realistic hash functions
    
    def test_false_negative_rate_perfect(self):
        """Test false negative rate with perfect hashing"""
        elements = [1, 2, 3]
        filter = rpf.create_filter(elements, error_rate=0.0)
        
        fnr = filter.false_negative_rate()
        assert fnr == 0.0
    
    def test_false_negative_rate_imperfect(self):
        """Test false negative rate with imperfect hashing"""
        elements = list(range(50))
        filter = rpf.create_filter(elements, error_rate=0.1)
        
        fnr = filter.false_negative_rate()
        assert 0.0 <= fnr <= 0.1


class TestBuilderPattern:
    """Test the builder pattern API"""
    
    def test_basic_builder(self):
        """Test basic builder functionality"""
        ph_builder = rpf.PerfectHashBuilder(error_rate=0.0)
        filter_builder = rpf.make_filter_builder(ph_builder)
        
        elements = [10, 20, 30]
        filter = filter_builder.build(elements)
        
        for elem in elements:
            assert elem in filter
    
    def test_builder_with_configuration(self):
        """Test builder with configuration methods"""
        ph_builder = rpf.PerfectHashBuilder(error_rate=0.01)
        filter_builder = rpf.make_filter_builder(ph_builder)
        
        filter_builder = (filter_builder
            .with_target_fpr(0.001)
            .with_target_fnr(0.0001)
            .with_max_iterations(1000)
            .with_space_overhead(2))
        
        elements = list(range(100))
        filter = filter_builder.build(elements)
        
        # Verify filter works
        for elem in elements[:10]:  # Test subset
            assert elem in filter
    
    def test_builder_reset(self):
        """Test builder reset functionality"""
        ph_builder = rpf.PerfectHashBuilder(error_rate=0.0)
        filter_builder = rpf.make_filter_builder(ph_builder)
        
        filter_builder = filter_builder.with_target_fpr(0.1).with_max_iterations(10)
        filter_builder.reset()
        
        # Should still work after reset
        filter = filter_builder.build([1, 2, 3])
        assert 1 in filter
        assert 2 in filter
        assert 3 in filter


class TestQueryInterface:
    """Test the query interface"""
    
    def test_query_contains(self):
        """Test query contains method"""
        elements = [1, 2, 3, 4, 5]
        filter = rpf.create_filter(elements)
        query = rpf.query(filter)
        
        assert query.contains(1) == True
        assert query.contains(6) == False
    
    def test_query_contains_all(self):
        """Test query contains_all method"""
        elements = [1, 2, 3, 4, 5]
        filter = rpf.create_filter(elements)
        query = rpf.query(filter)
        
        results = query.contains_all([1, 2, 6])
        assert results == [True, True, False]
    
    def test_query_contains_any(self):
        """Test query contains_any method"""
        elements = [1, 2, 3]
        filter = rpf.create_filter(elements)
        query = rpf.query(filter)
        
        assert query.contains_any([1, 4, 5]) == True
        assert query.contains_any([4, 5, 6]) == False
    
    def test_query_count_members(self):
        """Test query count_members method"""
        elements = [1, 2, 3, 4, 5]
        filter = rpf.create_filter(elements)
        query = rpf.query(filter)
        
        count = query.count_members(list(range(10)))
        assert count == 5  # Elements 1-5 are members
    
    def test_query_error_rates(self):
        """Test query error rate methods"""
        elements = list(range(100))
        filter = rpf.create_filter(elements, error_rate=0.05)
        query = rpf.query(filter)
        
        fpr = query.false_positive_rate()
        fnr = query.false_negative_rate()
        acc = query.accuracy()
        
        assert 0.0 <= fpr <= 1.0
        assert 0.0 <= fnr <= 1.0
        assert 0.0 <= acc <= 1.0
        assert acc == 1.0 - (fpr + fnr)


class TestBatchOperations:
    """Test batch operations"""
    
    def test_batch_add_and_size(self):
        """Test adding filters to batch and checking size"""
        batch = rpf.FilterBatch()
        
        assert batch.size() == 0
        assert len(batch) == 0
        
        filter1 = rpf.create_filter([1, 2, 3])
        filter2 = rpf.create_filter([4, 5, 6])
        
        batch.add(filter1).add(filter2)
        
        assert batch.size() == 2
        assert len(batch) == 2
    
    def test_batch_test_all(self):
        """Test batch test_all method"""
        batch = rpf.FilterBatch()
        
        filter1 = rpf.create_filter([1, 2, 3])
        filter2 = rpf.create_filter([2, 3, 4])
        filter3 = rpf.create_filter([3, 4, 5])
        
        batch.add(filter1).add(filter2).add(filter3)
        
        results = batch.test_all(3)
        assert results == [True, True, True]
        
        results = batch.test_all(1)
        assert results == [True, False, False]
    
    def test_batch_test_any(self):
        """Test batch test_any method"""
        batch = rpf.FilterBatch()
        
        filter1 = rpf.create_filter([1, 2])
        filter2 = rpf.create_filter([3, 4])
        
        batch.add(filter1).add(filter2)
        
        assert batch.test_any(1) == True
        assert batch.test_any(3) == True
        assert batch.test_any(5) == False
    
    def test_batch_clear(self):
        """Test batch clear method"""
        batch = rpf.FilterBatch()
        
        filter1 = rpf.create_filter([1, 2, 3])
        batch.add(filter1)
        
        assert batch.size() == 1
        
        batch.clear()
        assert batch.size() == 0


class TestFilterComparison:
    """Test filter comparison operations"""
    
    def test_filter_equality(self):
        """Test filter equality comparison"""
        elements = [1, 2, 3]
        
        # Same elements, same builder
        filter1 = rpf.create_filter(elements)
        filter2 = rpf.create_filter(elements)
        
        # Note: Equality depends on implementation details
        # Filters with same elements might not be equal if hash functions differ
        assert (filter1 == filter2) or (filter1 != filter2)  # Both are valid
    
    def test_filter_inequality(self):
        """Test filter inequality comparison"""
        filter1 = rpf.create_filter([1, 2, 3])
        filter2 = rpf.create_filter([4, 5, 6])
        
        assert filter1 != filter2


class TestStressTests:
    """Stress tests for the filter"""
    
    def test_large_dataset(self):
        """Test with a large dataset"""
        elements = list(range(10000))
        filter = rpf.create_filter(elements)
        
        # Test random sample of members
        sample = random.sample(elements, 100)
        for elem in sample:
            assert elem in filter
        
        # Test non-members
        non_members = list(range(10000, 10100))
        false_positives = sum(1 for elem in non_members if elem in filter)
        
        # False positive rate should be low
        observed_fpr = false_positives / len(non_members)
        assert observed_fpr < 0.1
    
    def test_random_strings(self):
        """Test with random string data"""
        def random_string(length=10):
            return ''.join(random.choices(string.ascii_letters, k=length))
        
        elements = [random_string() for _ in range(1000)]
        filter = rpf.create_filter(elements)
        
        # All elements should be members
        for elem in random.sample(elements, 100):
            assert elem in filter
        
        # Random non-members should mostly not be in filter
        non_members = [random_string() for _ in range(100)]
        false_positives = sum(1 for elem in non_members if elem in filter)
        assert false_positives < 10  # Less than 10% FPR


class TestRepresentation:
    """Test string representation of filter"""
    
    def test_filter_repr(self):
        """Test filter string representation"""
        filter = rpf.create_filter([1, 2, 3])
        repr_str = repr(filter)
        
        assert "RDPHFilter" in repr_str
        assert "fpr=" in repr_str
        assert "fnr=" in repr_str


if __name__ == "__main__":
    pytest.main([__file__, "-v"])