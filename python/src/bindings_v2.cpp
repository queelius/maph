#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <rd_ph_filter/approximate_map.hpp>
#include "ph_wrapper.hpp"

namespace py = pybind11;
using namespace approximate;

// Type aliases for different storage sizes
using ApproxMap8 = approximate_map<PyPerfectHash, std::uint8_t, 
                                   SetMembershipDecoder<std::uint8_t, PyPerfectHash::PyHasher>, bool>;
using ApproxMap16 = approximate_map<PyPerfectHash, std::uint16_t,
                                    SetMembershipDecoder<std::uint16_t, PyPerfectHash::PyHasher>, bool>;
using ApproxMap32 = approximate_map<PyPerfectHash, std::uint32_t,
                                    SetMembershipDecoder<std::uint32_t, PyPerfectHash::PyHasher>, bool>;
using ApproxMap64 = approximate_map<PyPerfectHash, std::uint64_t,
                                    SetMembershipDecoder<std::uint64_t, PyPerfectHash::PyHasher>, bool>;

// Threshold decoders
using ThresholdMap8 = approximate_map<PyPerfectHash, std::uint8_t,
                                      ThresholdDecoder<std::uint8_t>, bool>;
using ThresholdMap32 = approximate_map<PyPerfectHash, std::uint32_t,
                                       ThresholdDecoder<std::uint32_t>, bool>;

// Identity decoder for value storage
using IdentityMap8 = approximate_map<PyPerfectHash, std::uint8_t,
                                     IdentityDecoder<std::uint8_t>, std::uint8_t>;
using IdentityMap16 = approximate_map<PyPerfectHash, std::uint16_t,
                                      IdentityDecoder<std::uint16_t>, std::uint16_t>;
using IdentityMap32 = approximate_map<PyPerfectHash, std::uint32_t,
                                      IdentityDecoder<std::uint32_t>, std::uint32_t>;

// Builder
using PyApproxMapBuilder = approximate_map_builder<PyPerfectHash>;

PYBIND11_MODULE(approximate_filters, m) {
    m.doc() = R"pbdoc(
        Approximate Filters - Generalized Framework
        ============================================

        A Python library for approximate membership testing and compact
        function approximation using perfect hashing with configurable
        storage sizes and custom decoders.

        This module provides:
        - Membership filters with 8, 16, 32, 64-bit storage
        - Threshold-based filters with tunable false positive rates
        - Identity mapping for compact lookup tables
        - Custom decoder support for arbitrary mappings
    )pbdoc";

    // ========== Perfect Hash Builder ==========
    py::class_<PyPerfectHashBuilder>(m, "PerfectHashBuilder")
        .def(py::init<double>(), py::arg("error_rate") = 0.0,
             "Create a perfect hash builder with specified error rate")
        .def("set_error_rate", &PyPerfectHashBuilder::set_error_rate,
             "Set the error rate for the perfect hash function")
        .def("get_error_rate", &PyPerfectHashBuilder::get_error_rate,
             "Get the current error rate")
        .def("__repr__", [](const PyPerfectHashBuilder& self) {
            return "<PerfectHashBuilder error_rate=" + 
                   std::to_string(self.get_error_rate()) + ">";
        });

    // ========== Set Membership Filters (Different Storage Sizes) ==========
    
    // 8-bit storage (FPR ≈ 1/256)
    py::class_<ApproxMap8>(m, "ApproxFilter8")
        .def(py::init([](const std::vector<py::object>& elements,
                        PyPerfectHashBuilder& builder) {
            return ApproxMap8(elements.begin(), elements.end(), builder);
        }), py::arg("elements"), py::arg("builder"),
            "Create 8-bit storage filter (FPR ≈ 0.004)")
        .def("__contains__", [](const ApproxMap8& self, py::object x) {
            return self(x);
        })
        .def("contains", [](const ApproxMap8& self, py::object x) {
            return self(x);
        })
        .def("storage_bytes", &ApproxMap8::storage_bytes,
             "Get storage size in bytes")
        .def("false_negative_rate", &ApproxMap8::fnr,
             "Get false negative rate")
        .def_property_readonly("fpr", []() { return 1.0/256; },
             "Get false positive rate (≈1/256)")
        .def("__repr__", [](const ApproxMap8& self) {
            return "<ApproxFilter8 storage=" + 
                   std::to_string(self.storage_bytes()) + " bytes>";
        });

    // 16-bit storage (FPR ≈ 1/65536)
    py::class_<ApproxMap16>(m, "ApproxFilter16")
        .def(py::init([](const std::vector<py::object>& elements,
                        PyPerfectHashBuilder& builder) {
            return ApproxMap16(elements.begin(), elements.end(), builder);
        }), py::arg("elements"), py::arg("builder"),
            "Create 16-bit storage filter (FPR ≈ 0.000015)")
        .def("__contains__", [](const ApproxMap16& self, py::object x) {
            return self(x);
        })
        .def("contains", [](const ApproxMap16& self, py::object x) {
            return self(x);
        })
        .def("storage_bytes", &ApproxMap16::storage_bytes,
             "Get storage size in bytes")
        .def("false_negative_rate", &ApproxMap16::fnr,
             "Get false negative rate")
        .def_property_readonly("fpr", []() { return 1.0/65536; },
             "Get false positive rate (≈1/65536)")
        .def("__repr__", [](const ApproxMap16& self) {
            return "<ApproxFilter16 storage=" + 
                   std::to_string(self.storage_bytes()) + " bytes>";
        });

    // 32-bit storage (FPR ≈ 1/2^32)
    py::class_<ApproxMap32>(m, "ApproxFilter32")
        .def(py::init([](const std::vector<py::object>& elements,
                        PyPerfectHashBuilder& builder) {
            return ApproxMap32(elements.begin(), elements.end(), builder);
        }), py::arg("elements"), py::arg("builder"),
            "Create 32-bit storage filter (FPR ≈ 2.3e-10)")
        .def("__contains__", [](const ApproxMap32& self, py::object x) {
            return self(x);
        })
        .def("contains", [](const ApproxMap32& self, py::object x) {
            return self(x);
        })
        .def("storage_bytes", &ApproxMap32::storage_bytes,
             "Get storage size in bytes")
        .def("false_negative_rate", &ApproxMap32::fnr,
             "Get false negative rate")
        .def_property_readonly("fpr", []() { return 1.0/4294967296.0; },
             "Get false positive rate (≈1/2^32)")
        .def("__repr__", [](const ApproxMap32& self) {
            return "<ApproxFilter32 storage=" + 
                   std::to_string(self.storage_bytes()) + " bytes>";
        });

    // 64-bit storage (FPR ≈ 1/2^64)
    py::class_<ApproxMap64>(m, "ApproxFilter64")
        .def(py::init([](const std::vector<py::object>& elements,
                        PyPerfectHashBuilder& builder) {
            return ApproxMap64(elements.begin(), elements.end(), builder);
        }), py::arg("elements"), py::arg("builder"),
            "Create 64-bit storage filter (FPR ≈ 5.4e-20)")
        .def("__contains__", [](const ApproxMap64& self, py::object x) {
            return self(x);
        })
        .def("contains", [](const ApproxMap64& self, py::object x) {
            return self(x);
        })
        .def("storage_bytes", &ApproxMap64::storage_bytes,
             "Get storage size in bytes")
        .def("false_negative_rate", &ApproxMap64::fnr,
             "Get false negative rate")
        .def_property_readonly("fpr", []() { 
            return 1.0/18446744073709551616.0; 
        }, "Get false positive rate (≈1/2^64)")
        .def("__repr__", [](const ApproxMap64& self) {
            return "<ApproxFilter64 storage=" + 
                   std::to_string(self.storage_bytes()) + " bytes>";
        });

    // ========== Threshold Filters ==========
    
    py::class_<ThresholdMap8>(m, "ThresholdFilter8")
        .def(py::init([](const std::vector<py::object>& elements,
                        PyPerfectHashBuilder& builder,
                        double target_fpr) {
            std::uint8_t threshold = static_cast<std::uint8_t>(
                target_fpr * std::numeric_limits<std::uint8_t>::max()
            );
            ThresholdDecoder<std::uint8_t> decoder(threshold);
            
            auto encoder = [](py::object x) -> std::uint8_t {
                return static_cast<std::uint8_t>(py::hash(x) % 256);
            };
            
            return ThresholdMap8(elements.begin(), elements.end(),
                                builder, encoder, decoder);
        }), py::arg("elements"), py::arg("builder"), py::arg("target_fpr"),
            "Create threshold filter with 8-bit storage")
        .def("__contains__", [](const ThresholdMap8& self, py::object x) {
            return self(x);
        })
        .def("storage_bytes", &ThresholdMap8::storage_bytes)
        .def("__repr__", [](const ThresholdMap8& self) {
            return "<ThresholdFilter8 storage=" + 
                   std::to_string(self.storage_bytes()) + " bytes>";
        });

    py::class_<ThresholdMap32>(m, "ThresholdFilter32")
        .def(py::init([](const std::vector<py::object>& elements,
                        PyPerfectHashBuilder& builder,
                        double target_fpr) {
            std::uint32_t threshold = static_cast<std::uint32_t>(
                target_fpr * std::numeric_limits<std::uint32_t>::max()
            );
            ThresholdDecoder<std::uint32_t> decoder(threshold);
            
            auto encoder = [](py::object x) -> std::uint32_t {
                return static_cast<std::uint32_t>(py::hash(x));
            };
            
            return ThresholdMap32(elements.begin(), elements.end(),
                                 builder, encoder, decoder);
        }), py::arg("elements"), py::arg("builder"), py::arg("target_fpr"),
            "Create threshold filter with 32-bit storage")
        .def("__contains__", [](const ThresholdMap32& self, py::object x) {
            return self(x);
        })
        .def("storage_bytes", &ThresholdMap32::storage_bytes)
        .def("__repr__", [](const ThresholdMap32& self) {
            return "<ThresholdFilter32 storage=" + 
                   std::to_string(self.storage_bytes()) + " bytes>";
        });

    // ========== Identity Maps (Lookup Tables) ==========
    
    py::class_<IdentityMap8>(m, "CompactLookup8")
        .def(py::init([](const std::vector<py::object>& keys,
                        const std::vector<int>& values,
                        PyPerfectHashBuilder& builder) {
            if (keys.size() != values.size()) {
                throw std::invalid_argument("Keys and values must have same size");
            }
            
            auto encoder = [&values, &keys](py::object x) -> std::uint8_t {
                for (size_t i = 0; i < keys.size(); ++i) {
                    if (keys[i].equal(x)) {
                        return static_cast<std::uint8_t>(values[i]);
                    }
                }
                return 0;
            };
            
            IdentityDecoder<std::uint8_t> decoder;
            return IdentityMap8(keys.begin(), keys.end(), builder, encoder, decoder);
        }), py::arg("keys"), py::arg("values"), py::arg("builder"),
            "Create compact lookup table with 8-bit values")
        .def("__getitem__", [](const IdentityMap8& self, py::object x) {
            return self(x);
        })
        .def("get", [](const IdentityMap8& self, py::object x, int default_val) {
            int result = self(x);
            return result == 0 ? default_val : result;
        }, py::arg("key"), py::arg("default") = 0)
        .def("storage_bytes", &IdentityMap8::storage_bytes)
        .def("__repr__", [](const IdentityMap8& self) {
            return "<CompactLookup8 storage=" + 
                   std::to_string(self.storage_bytes()) + " bytes>";
        });

    py::class_<IdentityMap32>(m, "CompactLookup32")
        .def(py::init([](const std::vector<py::object>& keys,
                        const std::vector<int>& values,
                        PyPerfectHashBuilder& builder) {
            if (keys.size() != values.size()) {
                throw std::invalid_argument("Keys and values must have same size");
            }
            
            auto encoder = [&values, &keys](py::object x) -> std::uint32_t {
                for (size_t i = 0; i < keys.size(); ++i) {
                    if (keys[i].equal(x)) {
                        return static_cast<std::uint32_t>(values[i]);
                    }
                }
                return 0;
            };
            
            IdentityDecoder<std::uint32_t> decoder;
            return IdentityMap32(keys.begin(), keys.end(), builder, encoder, decoder);
        }), py::arg("keys"), py::arg("values"), py::arg("builder"),
            "Create compact lookup table with 32-bit values")
        .def("__getitem__", [](const IdentityMap32& self, py::object x) {
            return self(x);
        })
        .def("get", [](const IdentityMap32& self, py::object x, int default_val) {
            int result = self(x);
            return result == 0 ? default_val : result;
        }, py::arg("key"), py::arg("default") = 0)
        .def("storage_bytes", &IdentityMap32::storage_bytes)
        .def("__repr__", [](const IdentityMap32& self) {
            return "<CompactLookup32 storage=" + 
                   std::to_string(self.storage_bytes()) + " bytes>";
        });

    // ========== Builder Pattern ==========
    
    py::class_<PyApproxMapBuilder>(m, "ApproxMapBuilder")
        .def(py::init([](PyPerfectHashBuilder& ph_builder) {
            return PyApproxMapBuilder(
                [ph_builder](auto begin, auto end) {
                    return PyPerfectHash(begin, end, ph_builder.get_error_rate());
                }
            );
        }), py::arg("ph_builder"))
        .def("with_load_factor", &PyApproxMapBuilder::with_load_factor,
             py::arg("factor"),
             py::return_value_policy::reference_internal,
             "Set load factor (>1.0 for sparser storage)")
        .def("build_filter_8bit", [](const PyApproxMapBuilder& self,
                                     const std::vector<py::object>& elements) {
            return self.build_set_filter_8bit(elements.begin(), elements.end());
        }, py::arg("elements"), "Build 8-bit filter")
        .def("build_filter_16bit", [](const PyApproxMapBuilder& self,
                                      const std::vector<py::object>& elements) {
            return self.build_set_filter_16bit(elements.begin(), elements.end());
        }, py::arg("elements"), "Build 16-bit filter")
        .def("build_filter_32bit", [](const PyApproxMapBuilder& self,
                                      const std::vector<py::object>& elements) {
            return self.build_set_filter_32bit(elements.begin(), elements.end());
        }, py::arg("elements"), "Build 32-bit filter")
        .def("build_filter_64bit", [](const PyApproxMapBuilder& self,
                                      const std::vector<py::object>& elements) {
            return self.build_set_filter_64bit(elements.begin(), elements.end());
        }, py::arg("elements"), "Build 64-bit filter");

    // ========== Convenience Functions ==========
    
    m.def("create_filter", [](const std::vector<py::object>& elements,
                             int bits = 32, double error_rate = 0.0) {
        PyPerfectHashBuilder builder(error_rate);
        
        if (bits == 8) {
            return py::cast(ApproxMap8(elements.begin(), elements.end(), builder));
        } else if (bits == 16) {
            return py::cast(ApproxMap16(elements.begin(), elements.end(), builder));
        } else if (bits == 64) {
            return py::cast(ApproxMap64(elements.begin(), elements.end(), builder));
        } else {
            return py::cast(ApproxMap32(elements.begin(), elements.end(), builder));
        }
    }, py::arg("elements"), py::arg("bits") = 32, py::arg("error_rate") = 0.0,
       R"pbdoc(
       Create an approximate membership filter.

       Parameters
       ----------
       elements : list
           Elements to include in the filter
       bits : int (8, 16, 32, or 64)
           Storage size in bits per element
       error_rate : float
           Perfect hash error rate (false negative rate)

       Returns
       -------
       Filter object with specified storage size

       Examples
       --------
       >>> # 8-bit filter (256x space savings, ~0.4% FPR)
       >>> filter8 = create_filter([1, 2, 3], bits=8)
       >>> 
       >>> # 32-bit filter (near-perfect accuracy)
       >>> filter32 = create_filter(['a', 'b', 'c'], bits=32)
       )pbdoc");

    m.def("create_threshold_filter",
          [](const std::vector<py::object>& elements,
             double target_fpr, int bits = 32, double error_rate = 0.0) {
        PyPerfectHashBuilder builder(error_rate);
        
        if (bits == 8) {
            std::uint8_t threshold = static_cast<std::uint8_t>(
                target_fpr * std::numeric_limits<std::uint8_t>::max()
            );
            ThresholdDecoder<std::uint8_t> decoder(threshold);
            auto encoder = [](py::object x) -> std::uint8_t {
                return static_cast<std::uint8_t>(py::hash(x) % 256);
            };
            return py::cast(ThresholdMap8(elements.begin(), elements.end(),
                                         builder, encoder, decoder));
        } else {
            std::uint32_t threshold = static_cast<std::uint32_t>(
                target_fpr * std::numeric_limits<std::uint32_t>::max()
            );
            ThresholdDecoder<std::uint32_t> decoder(threshold);
            auto encoder = [](py::object x) -> std::uint32_t {
                return static_cast<std::uint32_t>(py::hash(x));
            };
            return py::cast(ThresholdMap32(elements.begin(), elements.end(),
                                          builder, encoder, decoder));
        }
    }, py::arg("elements"), py::arg("target_fpr"),
       py::arg("bits") = 32, py::arg("error_rate") = 0.0,
       "Create a threshold filter with tunable false positive rate");

    m.def("create_lookup",
          [](const std::vector<py::object>& keys,
             const std::vector<int>& values,
             int bits = 32, double error_rate = 0.0) {
        if (keys.size() != values.size()) {
            throw std::invalid_argument("Keys and values must have same size");
        }
        
        PyPerfectHashBuilder builder(error_rate);
        
        if (bits == 8) {
            auto encoder = [&values, &keys](py::object x) -> std::uint8_t {
                for (size_t i = 0; i < keys.size(); ++i) {
                    if (keys[i].equal(x)) {
                        return static_cast<std::uint8_t>(values[i]);
                    }
                }
                return 0;
            };
            IdentityDecoder<std::uint8_t> decoder;
            return py::cast(IdentityMap8(keys.begin(), keys.end(),
                                        builder, encoder, decoder));
        } else {
            auto encoder = [&values, &keys](py::object x) -> std::uint32_t {
                for (size_t i = 0; i < keys.size(); ++i) {
                    if (keys[i].equal(x)) {
                        return static_cast<std::uint32_t>(values[i]);
                    }
                }
                return 0;
            };
            IdentityDecoder<std::uint32_t> decoder;
            return py::cast(IdentityMap32(keys.begin(), keys.end(),
                                         builder, encoder, decoder));
        }
    }, py::arg("keys"), py::arg("values"),
       py::arg("bits") = 32, py::arg("error_rate") = 0.0,
       "Create a compact lookup table");

    // ========== Module Attributes ==========
    
    m.attr("__version__") = "2.0.0";
    m.attr("FPR_8BIT") = 1.0/256;
    m.attr("FPR_16BIT") = 1.0/65536;
    m.attr("FPR_32BIT") = 1.0/4294967296.0;
    m.attr("FPR_64BIT") = 1.0/18446744073709551616.0;
}