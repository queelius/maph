#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <rd_ph_filter/rd_ph_filter.hpp>
#include <rd_ph_filter/builder.hpp>
#include "ph_wrapper.hpp"

namespace py = pybind11;
using namespace bernoulli;

// Type aliases for Python bindings
using PyFilter = rd_ph_filter<PyPerfectHash>;
using PyFilterBuilder = rd_ph_filter_builder<PyPerfectHash>;
using PyFilterQuery = rd_ph_filter_query<PyPerfectHash>;
using PyFilterBatch = rd_ph_filter_batch<PyPerfectHash>;

PYBIND11_MODULE(rd_ph_filter, m) {
    m.doc() = R"pbdoc(
        Rate-distorted Perfect Hash Filter
        ===================================

        A Python library for rate-distorted perfect hash filters,
        implementing the concept of a Bernoulli set with controllable
        false positive and false negative rates.

        This module provides efficient membership testing with
        probabilistic guarantees, suitable for applications where
        perfect accuracy is not required but space efficiency is critical.
    )pbdoc";

    // PyPerfectHashBuilder class
    py::class_<PyPerfectHashBuilder>(m, "PerfectHashBuilder")
        .def(py::init<double>(), py::arg("error_rate") = 0.0,
             "Create a perfect hash builder with specified error rate")
        .def("set_error_rate", &PyPerfectHashBuilder::set_error_rate,
             "Set the error rate for the perfect hash function")
        .def("get_error_rate", &PyPerfectHashBuilder::get_error_rate,
             "Get the current error rate");

    // Main filter class
    py::class_<PyFilter>(m, "RDPHFilter")
        .def(py::init([](const std::vector<py::object>& elements, 
                        PyPerfectHashBuilder& builder) {
            return PyFilter(elements.begin(), elements.end(), builder);
        }), py::arg("elements"), py::arg("builder"),
            R"pbdoc(
            Construct a rate-distorted perfect hash filter.

            Parameters
            ----------
            elements : list
                Elements to include in the filter
            builder : PerfectHashBuilder
                Builder for the underlying perfect hash function

            Examples
            --------
            >>> builder = PerfectHashBuilder(error_rate=0.01)
            >>> filter = RDPHFilter([1, 2, 3, 4, 5], builder)
            >>> 3 in filter
            True
            >>> 10 in filter
            False
            )pbdoc")
        
        .def("__contains__", [](const PyFilter& self, py::object x) {
            return self(x);
        }, "Test if element is in the filter")
        
        .def("__call__", [](const PyFilter& self, py::object x) {
            return self(x);
        }, "Test if element is in the filter")
        
        .def("contains", [](const PyFilter& self, py::object x) {
            return self(x);
        }, py::arg("element"),
            "Test if element is a member of the set")
        
        .def("false_positive_rate", &PyFilter::fpr,
            "Get the false positive rate of the filter")
        
        .def("false_negative_rate", &PyFilter::fnr,
            "Get the false negative rate of the filter")
        
        .def("__eq__", [](const PyFilter& a, const PyFilter& b) {
            return a == b;
        }, "Test equality of two filters")
        
        .def("__ne__", [](const PyFilter& a, const PyFilter& b) {
            return a != b;
        }, "Test inequality of two filters")
        
        .def("__repr__", [](const PyFilter& self) {
            return "<RDPHFilter fpr=" + std::to_string(self.fpr()) + 
                   " fnr=" + std::to_string(self.fnr()) + ">";
        });

    // Builder pattern class
    py::class_<PyFilterBuilder>(m, "FilterBuilder")
        .def(py::init<std::function<PyPerfectHash(PyPerfectHash::iterator, 
                                                  PyPerfectHash::iterator)>>(),
             py::arg("ph_builder"),
             "Create a filter builder with a perfect hash builder function")
        
        .def("with_target_fpr", &PyFilterBuilder::with_target_fpr,
             py::arg("rate"),
             py::return_value_policy::reference_internal,
             "Set target false positive rate")
        
        .def("with_target_fnr", &PyFilterBuilder::with_target_fnr,
             py::arg("rate"),
             py::return_value_policy::reference_internal,
             "Set target false negative rate")
        
        .def("with_max_iterations", &PyFilterBuilder::with_max_iterations,
             py::arg("iterations"),
             py::return_value_policy::reference_internal,
             "Set maximum iterations for perfect hash construction")
        
        .def("with_space_overhead", &PyFilterBuilder::with_space_overhead,
             py::arg("factor"),
             py::return_value_policy::reference_internal,
             "Set space overhead factor")
        
        .def("build", [](const PyFilterBuilder& self, 
                        const std::vector<py::object>& elements) {
            return self.build(elements.begin(), elements.end());
        }, py::arg("elements"),
           R"pbdoc(
           Build a filter from a list of elements.

           Parameters
           ----------
           elements : list
               Elements to include in the filter

           Returns
           -------
           RDPHFilter
               Constructed filter instance

           Examples
           --------
           >>> builder = FilterBuilder(ph_builder)
           >>> filter = builder.with_target_fpr(0.01).build([1, 2, 3])
           )pbdoc")
        
        .def("reset", &PyFilterBuilder::reset,
             py::return_value_policy::reference_internal,
             "Reset builder to default configuration");

    // Query interface
    py::class_<PyFilterQuery>(m, "FilterQuery")
        .def("contains", [](const PyFilterQuery& self, py::object x) {
            return self.contains(x);
        }, py::arg("element"),
           "Test if element is a member")
        
        .def("contains_all", [](const PyFilterQuery& self, 
                               const std::vector<py::object>& elements) {
            return self.contains_all(elements);
        }, py::arg("elements"),
           "Test multiple elements for membership")
        
        .def("contains_any", [](const PyFilterQuery& self,
                               const std::vector<py::object>& elements) {
            return self.contains_any(elements);
        }, py::arg("elements"),
           "Test if any elements are members")
        
        .def("count_members", [](const PyFilterQuery& self,
                                const std::vector<py::object>& elements) {
            return self.count_members(elements);
        }, py::arg("elements"),
           "Count how many elements are members")
        
        .def("false_positive_rate", &PyFilterQuery::false_positive_rate,
             "Get false positive rate")
        
        .def("false_negative_rate", &PyFilterQuery::false_negative_rate,
             "Get false negative rate")
        
        .def("accuracy", &PyFilterQuery::accuracy,
             "Get overall accuracy (1 - FPR - FNR)");

    // Batch operations
    py::class_<PyFilterBatch>(m, "FilterBatch")
        .def(py::init<>(), "Create a new filter batch")
        
        .def("add", &PyFilterBatch::add,
             py::arg("filter"),
             py::return_value_policy::reference_internal,
             "Add a filter to the batch")
        
        .def("test_all", [](const PyFilterBatch& self, py::object x) {
            return self.test_all(x);
        }, py::arg("element"),
           "Test element against all filters")
        
        .def("test_any", [](const PyFilterBatch& self, py::object x) {
            return self.test_any(x);
        }, py::arg("element"),
           "Test if element is in any filter")
        
        .def("size", &PyFilterBatch::size,
             "Get number of filters in batch")
        
        .def("clear", &PyFilterBatch::clear,
             py::return_value_policy::reference_internal,
             "Clear all filters from batch")
        
        .def("__len__", &PyFilterBatch::size,
             "Get number of filters in batch");

    // Factory functions
    m.def("make_filter_builder", [](PyPerfectHashBuilder& ph_builder) {
        return make_filter_builder<PyPerfectHash>(ph_builder);
    }, py::arg("ph_builder"),
       "Create a filter builder from a perfect hash builder");

    m.def("query", [](const PyFilter& filter) {
        return query(filter);
    }, py::arg("filter"),
       R"pbdoc(
       Create a query object for fluent API operations.

       Parameters
       ----------
       filter : RDPHFilter
           Filter to query

       Returns
       -------
       FilterQuery
           Query object for fluent operations

       Examples
       --------
       >>> q = query(filter)
       >>> q.contains_all([1, 2, 3])
       [True, True, True]
       )pbdoc");

    // Convenience functions
    m.def("create_filter", [](const std::vector<py::object>& elements,
                             double error_rate = 0.0) {
        PyPerfectHashBuilder builder(error_rate);
        return PyFilter(elements.begin(), elements.end(), builder);
    }, py::arg("elements"), py::arg("error_rate") = 0.0,
       R"pbdoc(
       Convenience function to create a filter with default settings.

       Parameters
       ----------
       elements : list
           Elements to include in the filter
       error_rate : float, optional
           Error rate for the perfect hash function (default: 0.0)

       Returns
       -------
       RDPHFilter
           Constructed filter instance

       Examples
       --------
       >>> filter = create_filter([1, 2, 3, 4, 5])
       >>> 3 in filter
       True
       )pbdoc");

    // Version information
    m.attr("__version__") = "1.0.0";
}