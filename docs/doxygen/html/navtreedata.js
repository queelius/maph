/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "maph", "index.html", [
    [ "Optimized TCP, TLS, QUIC & HTTP3 transports", "index.html", "index" ],
    [ "libusockets.h", "d5/d6a/md_integrations_2rest__api_2__deps_2usockets-src_2misc_2manual.html", [
      [ "A quick note on compilation", "d5/d6a/md_integrations_2rest__api_2__deps_2usockets-src_2misc_2manual.html#autotoc_md1", null ],
      [ "Cross-platform benchmarks", "d5/d6a/md_integrations_2rest__api_2__deps_2usockets-src_2misc_2manual.html#autotoc_md2", null ],
      [ "us_loop_t - The root per-thread resource and callback emitter", "d5/d6a/md_integrations_2rest__api_2__deps_2usockets-src_2misc_2manual.html#autotoc_md3", null ],
      [ "us_socket_context_t - The per-behavior group of networking sockets", "d5/d6a/md_integrations_2rest__api_2__deps_2usockets-src_2misc_2manual.html#autotoc_md4", null ],
      [ "us_socket_t - The network connection (SSL or non-SSL)", "d5/d6a/md_integrations_2rest__api_2__deps_2usockets-src_2misc_2manual.html#autotoc_md5", null ],
      [ "Low level components", "d5/d6a/md_integrations_2rest__api_2__deps_2usockets-src_2misc_2manual.html#autotoc_md6", [
        [ "us_timer_t - High cost (very expensive resource) timers", "d5/d6a/md_integrations_2rest__api_2__deps_2usockets-src_2misc_2manual.html#autotoc_md7", null ],
        [ "us_poll_t - The eventing foundation of a socket or anything that has a file descriptor", "d5/d6a/md_integrations_2rest__api_2__deps_2usockets-src_2misc_2manual.html#autotoc_md8", null ]
      ] ]
    ] ],
    [ "Maph REST API Documentation", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html", [
      [ "Overview", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md14", null ],
      [ "Quick Start", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md15", [
        [ "Starting the Server", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md16", null ],
        [ "Basic Usage", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md17", null ]
      ] ],
      [ "API Reference", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md18", [
        [ "Base URL", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md19", null ],
        [ "Authentication", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md20", null ],
        [ "Content Types", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md21", null ],
        [ "Common Response Codes", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md22", null ]
      ] ],
      [ "Endpoints", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md23", [
        [ "Key-Value Operations", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md24", [
          [ "GET /api/kv/{key}", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md25", null ],
          [ "PUT /api/kv/{key}", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md26", null ],
          [ "DELETE /api/kv/{key}", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md27", null ],
          [ "HEAD /api/kv/{key}", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md28", null ]
        ] ],
        [ "Batch Operations", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md29", [
          [ "POST /api/batch/get", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md30", null ],
          [ "POST /api/batch/set", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md31", null ],
          [ "POST /api/batch/delete", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md32", null ]
        ] ],
        [ "Query Operations", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md33", [
          [ "GET /api/scan", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md34", null ],
          [ "POST /api/search", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md35", null ]
        ] ],
        [ "Database Operations", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md36", [
          [ "GET /api/stats", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md37", null ],
          [ "POST /api/compact", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md38", null ],
          [ "POST /api/sync", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md39", null ],
          [ "GET /api/info", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md40", null ]
        ] ],
        [ "Import/Export", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md41", [
          [ "POST /api/import", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md42", null ],
          [ "GET /api/export", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md43", null ]
        ] ]
      ] ],
      [ "Web Interface", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md44", null ],
      [ "WebSocket API", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md45", null ],
      [ "Client Libraries", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md46", [
        [ "JavaScript/Node.js", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md47", null ],
        [ "Python", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md48", null ],
        [ "Go", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md49", null ]
      ] ],
      [ "Performance Considerations", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md50", [
        [ "Connection Pooling", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md51", null ],
        [ "Batch vs Individual Operations", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md52", null ],
        [ "Compression", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md53", null ],
        [ "Rate Limiting", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md54", null ]
      ] ],
      [ "Error Handling", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md55", [
        [ "Error Response Format", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md56", null ],
        [ "Error Codes", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md57", null ]
      ] ],
      [ "Security", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md58", [
        [ "Best Practices", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md59", null ],
        [ "CORS Configuration", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md60", null ]
      ] ],
      [ "Monitoring", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md61", [
        [ "Health Check", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md62", null ],
        [ "Metrics Endpoint", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md63", null ]
      ] ],
      [ "Configuration", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md64", [
        [ "Environment Variables", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md65", null ],
        [ "Configuration File", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md66", null ]
      ] ],
      [ "Docker Deployment", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md67", [
        [ "Dockerfile", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md68", null ],
        [ "Docker Compose", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md69", null ],
        [ "Kubernetes Deployment", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md70", null ]
      ] ],
      [ "Examples", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md71", [
        [ "Session Store", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md72", null ],
        [ "Cache Layer", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md73", null ],
        [ "Real-time Dashboard", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md74", null ]
      ] ],
      [ "Troubleshooting", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md75", [
        [ "Connection Refused", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md76", null ],
        [ "Slow Performance", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md77", null ],
        [ "Data Corruption", "d3/d1d/md_integrations_2rest__api_2_a_p_i.html#autotoc_md78", null ]
      ] ]
    ] ],
    [ "RD-PH Filter API Documentation", "d2/dd5/md_docs_2_a_p_i.html", [
      [ "C++ API", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md107", [
        [ "Core Classes", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md108", [
          [ "<tt>approximate_map<PH, StorageType, Decoder, OutputType></tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md109", null ],
          [ "<tt>ApproxMapBuilder<PH></tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md110", null ]
        ] ],
        [ "Decoders", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md111", [
          [ "<tt>SetMembershipDecoder<StorageType, H></tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md112", null ],
          [ "<tt>ThresholdDecoder<StorageType, H></tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md113", null ],
          [ "<tt>IdentityDecoder<StorageType, H></tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md114", null ]
        ] ],
        [ "Lazy Iterators", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md115", [
          [ "<tt>lazy_generator_iterator<Generator></tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md116", null ],
          [ "<tt>filter_iterator<Iterator, Predicate></tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md117", null ],
          [ "<tt>transform_iterator<Iterator, Transform></tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md118", null ]
        ] ],
        [ "Convenience Functions", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md119", null ]
      ] ],
      [ "Python API", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md120", [
        [ "Module: <tt>approximate_filters</tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md121", [
          [ "Filter Classes", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md122", [
            [ "<tt>ApproxFilter8</tt> / <tt>ApproxFilter16</tt> / <tt>ApproxFilter32</tt> / <tt>ApproxFilter64</tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md123", null ],
            [ "<tt>ThresholdFilter8</tt> / <tt>ThresholdFilter16</tt> / <tt>ThresholdFilter32</tt> / <tt>ThresholdFilter64</tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md124", null ],
            [ "<tt>CompactLookup8</tt> / <tt>CompactLookup16</tt> / <tt>CompactLookup32</tt> / <tt>CompactLookup64</tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md125", null ]
          ] ],
          [ "Builder Classes", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md126", [
            [ "<tt>PerfectHashBuilder</tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md127", null ],
            [ "<tt>ApproxMapBuilder</tt>", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md128", null ]
          ] ],
          [ "Convenience Functions", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md129", null ],
          [ "Constants", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md130", null ]
        ] ]
      ] ],
      [ "Usage Examples", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md131", [
        [ "C++ Examples", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md132", [
          [ "Basic Set Membership", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md133", null ],
          [ "Threshold Filter", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md134", null ],
          [ "Compact Lookup", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md135", null ],
          [ "Lazy Iteration", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md136", null ]
        ] ],
        [ "Python Examples", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md137", [
          [ "Basic Usage", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md138", null ],
          [ "Threshold Filter", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md139", null ],
          [ "Compact Lookup", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md140", null ],
          [ "Builder Pattern", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md141", null ]
        ] ]
      ] ],
      [ "Error Handling", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md142", [
        [ "C++ Exceptions", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md143", null ],
        [ "Python Exceptions", "d2/dd5/md_docs_2_a_p_i.html#autotoc_md144", null ]
      ] ]
    ] ],
    [ "Maph Architecture Documentation", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html", [
      [ "Table of Contents", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md146", null ],
      [ "Overview", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md147", [
        [ "Key Design Goals", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md148", null ]
      ] ],
      [ "System Design", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md149", [
        [ "Architecture Layers", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md150", null ],
        [ "Component Interactions", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md151", null ]
      ] ],
      [ "Memory Layout", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md152", [
        [ "Header Structure (512 bytes)", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md153", null ],
        [ "Slot Structure (512 bytes)", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md154", null ],
        [ "Memory Alignment", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md155", null ]
      ] ],
      [ "Perfect Hash Function", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md156", [
        [ "FNV-1a Algorithm", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md157", null ],
        [ "Hash Properties", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md158", null ],
        [ "Slot Index Calculation", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md159", null ]
      ] ],
      [ "Collision Handling", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md160", [
        [ "Static Slots (Default: 80% of total)", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md161", null ],
        [ "Dynamic Slots (Default: 20% of total)", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md162", null ],
        [ "Probing Strategy", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md163", null ]
      ] ],
      [ "Concurrency Model", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md164", [
        [ "Thread Safety Guarantees", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md165", null ],
        [ "Atomic Operations", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md166", null ],
        [ "Lock-Free Reads", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md167", null ]
      ] ],
      [ "Performance Optimizations", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md168", [
        [ "CPU Cache Optimization", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md169", null ],
        [ "SIMD Acceleration", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md170", null ],
        [ "Memory Access Patterns", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md171", null ],
        [ "Zero-Copy Design", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md172", null ]
      ] ],
      [ "Storage Tiers", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md173", [
        [ "Tier 1: CPU Cache (L1/L2/L3)", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md174", null ],
        [ "Tier 2: RAM (Page Cache)", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md175", null ],
        [ "Tier 3: Disk (SSD/HDD)", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md176", null ]
      ] ],
      [ "Durability and Persistence", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md177", [
        [ "Automatic Persistence", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md178", null ],
        [ "Explicit Durability Options", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md179", null ],
        [ "Recovery Semantics", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md180", null ],
        [ "Best Practices", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md181", null ]
      ] ],
      [ "Design Trade-offs", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md182", [
        [ "Advantages", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md183", null ],
        [ "Limitations", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md184", null ],
        [ "When to Use Maph", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md185", null ]
      ] ],
      [ "Future Enhancements", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md186", [
        [ "Planned Improvements", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md187", null ],
        [ "Research Areas", "d6/d41/md_docs_2_a_r_c_h_i_t_e_c_t_u_r_e.html#autotoc_md188", null ]
      ] ]
    ] ],
    [ "Maph CLI Documentation", "d8/d91/md_docs_2_c_l_i.html", [
      [ "Overview", "d8/d91/md_docs_2_c_l_i.html#autotoc_md190", null ],
      [ "Installation", "d8/d91/md_docs_2_c_l_i.html#autotoc_md191", null ],
      [ "Command Reference", "d8/d91/md_docs_2_c_l_i.html#autotoc_md192", [
        [ "Global Synopsis", "d8/d91/md_docs_2_c_l_i.html#autotoc_md193", null ],
        [ "Commands", "d8/d91/md_docs_2_c_l_i.html#autotoc_md194", [
          [ "create - Create a new database", "d8/d91/md_docs_2_c_l_i.html#autotoc_md195", null ],
          [ "set - Store a key-value pair", "d8/d91/md_docs_2_c_l_i.html#autotoc_md196", null ],
          [ "get - Retrieve a value", "d8/d91/md_docs_2_c_l_i.html#autotoc_md197", null ],
          [ "remove - Delete a key", "d8/d91/md_docs_2_c_l_i.html#autotoc_md198", null ],
          [ "stats - Show database statistics", "d8/d91/md_docs_2_c_l_i.html#autotoc_md199", null ],
          [ "bench - Run single-threaded benchmark", "d8/d91/md_docs_2_c_l_i.html#autotoc_md200", null ],
          [ "bench_parallel - Run multi-threaded benchmark", "d8/d91/md_docs_2_c_l_i.html#autotoc_md201", null ],
          [ "load_bulk - Import JSONL file", "d8/d91/md_docs_2_c_l_i.html#autotoc_md202", null ],
          [ "mget - Get multiple keys", "d8/d91/md_docs_2_c_l_i.html#autotoc_md203", null ],
          [ "mset - Set multiple key-value pairs", "d8/d91/md_docs_2_c_l_i.html#autotoc_md204", null ]
        ] ],
        [ "Options", "d8/d91/md_docs_2_c_l_i.html#autotoc_md205", [
          [ "Global Options", "d8/d91/md_docs_2_c_l_i.html#autotoc_md206", [
            [ "–threads <n>", "d8/d91/md_docs_2_c_l_i.html#autotoc_md207", null ],
            [ "–durability <ms>", "d8/d91/md_docs_2_c_l_i.html#autotoc_md208", null ],
            [ "–verbose", "d8/d91/md_docs_2_c_l_i.html#autotoc_md209", null ],
            [ "–help", "d8/d91/md_docs_2_c_l_i.html#autotoc_md210", null ]
          ] ]
        ] ]
      ] ],
      [ "Usage Patterns", "d8/d91/md_docs_2_c_l_i.html#autotoc_md211", [
        [ "Database Management", "d8/d91/md_docs_2_c_l_i.html#autotoc_md212", null ],
        [ "Data Import/Export", "d8/d91/md_docs_2_c_l_i.html#autotoc_md213", null ],
        [ "Performance Testing", "d8/d91/md_docs_2_c_l_i.html#autotoc_md214", null ],
        [ "Scripting Examples", "d8/d91/md_docs_2_c_l_i.html#autotoc_md215", [
          [ "Health Check Script", "d8/d91/md_docs_2_c_l_i.html#autotoc_md216", null ],
          [ "Batch Update Script", "d8/d91/md_docs_2_c_l_i.html#autotoc_md217", null ],
          [ "Cache Warmer", "d8/d91/md_docs_2_c_l_i.html#autotoc_md218", null ]
        ] ]
      ] ],
      [ "Error Handling", "d8/d91/md_docs_2_c_l_i.html#autotoc_md219", [
        [ "Common Errors", "d8/d91/md_docs_2_c_l_i.html#autotoc_md220", [
          [ "File Not Found", "d8/d91/md_docs_2_c_l_i.html#autotoc_md221", null ],
          [ "Value Too Large", "d8/d91/md_docs_2_c_l_i.html#autotoc_md222", null ],
          [ "Database Full", "d8/d91/md_docs_2_c_l_i.html#autotoc_md223", null ],
          [ "Permission Denied", "d8/d91/md_docs_2_c_l_i.html#autotoc_md224", null ]
        ] ],
        [ "Exit Codes", "d8/d91/md_docs_2_c_l_i.html#autotoc_md225", null ]
      ] ],
      [ "Performance Tips", "d8/d91/md_docs_2_c_l_i.html#autotoc_md226", null ],
      [ "Integration Examples", "d8/d91/md_docs_2_c_l_i.html#autotoc_md227", [
        [ "Systemd Service", "d8/d91/md_docs_2_c_l_i.html#autotoc_md228", null ],
        [ "Cron Job", "d8/d91/md_docs_2_c_l_i.html#autotoc_md229", null ],
        [ "Docker Integration", "d8/d91/md_docs_2_c_l_i.html#autotoc_md230", null ]
      ] ],
      [ "Troubleshooting", "d8/d91/md_docs_2_c_l_i.html#autotoc_md231", [
        [ "Debug Mode", "d8/d91/md_docs_2_c_l_i.html#autotoc_md232", null ],
        [ "Verify Database Integrity", "d8/d91/md_docs_2_c_l_i.html#autotoc_md233", null ],
        [ "Performance Analysis", "d8/d91/md_docs_2_c_l_i.html#autotoc_md234", null ]
      ] ],
      [ "See Also", "d8/d91/md_docs_2_c_l_i.html#autotoc_md235", null ]
    ] ],
    [ "maph Documentation", "d3/d4c/md_docs_2index.html", [
      [ "🚀 What is maph?", "d3/d4c/md_docs_2index.html#autotoc_md237", null ],
      [ "📚 Documentation Structure", "d3/d4c/md_docs_2index.html#autotoc_md238", [
        [ "Core Documentation", "d3/d4c/md_docs_2index.html#autotoc_md239", null ],
        [ "Tools & Integrations", "d3/d4c/md_docs_2index.html#autotoc_md240", null ],
        [ "Development", "d3/d4c/md_docs_2index.html#autotoc_md241", null ]
      ] ],
      [ "⚡ Performance Highlights", "d3/d4c/md_docs_2index.html#autotoc_md242", null ],
      [ "🎯 Use Cases", "d3/d4c/md_docs_2index.html#autotoc_md243", null ],
      [ "🔧 Quick Example", "d3/d4c/md_docs_2index.html#autotoc_md244", null ],
      [ "📊 Comparison with Alternatives", "d3/d4c/md_docs_2index.html#autotoc_md245", null ],
      [ "🛠️ Getting Started", "d3/d4c/md_docs_2index.html#autotoc_md246", [
        [ "Installation", "d3/d4c/md_docs_2index.html#autotoc_md247", null ],
        [ "Basic Usage", "d3/d4c/md_docs_2index.html#autotoc_md248", null ]
      ] ],
      [ "📈 Benchmarks", "d3/d4c/md_docs_2index.html#autotoc_md249", [
        [ "Single-threaded Performance", "d3/d4c/md_docs_2index.html#autotoc_md250", null ],
        [ "Multi-threaded Performance (16 threads)", "d3/d4c/md_docs_2index.html#autotoc_md251", null ]
      ] ],
      [ "🤝 Contributing", "d3/d4c/md_docs_2index.html#autotoc_md252", null ],
      [ "📄 License", "d3/d4c/md_docs_2index.html#autotoc_md253", null ],
      [ "🔗 Links", "d3/d4c/md_docs_2index.html#autotoc_md254", null ]
    ] ],
    [ "Maph User Guide", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html", [
      [ "Table of Contents", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md256", null ],
      [ "Introduction", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md257", [
        [ "When to Use Maph", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md258", null ],
        [ "Key Features", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md259", null ]
      ] ],
      [ "Installation", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md260", [
        [ "Building from Source", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md261", null ],
        [ "Using as a Header-Only Library", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md262", null ],
        [ "CMake Integration", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md263", null ]
      ] ],
      [ "Quick Start", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md264", [
        [ "Creating a Database", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md265", null ],
        [ "Command-Line Usage", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md266", null ]
      ] ],
      [ "Basic Operations", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md267", [
        [ "Opening a Database", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md268", null ],
        [ "Storing Data (set)", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md269", null ],
        [ "Retrieving Data (get)", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md270", null ],
        [ "Checking Existence", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md271", null ],
        [ "Removing Data", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md272", null ],
        [ "Getting Statistics", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md273", null ]
      ] ],
      [ "Advanced Features", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md274", [
        [ "Batch Operations", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md275", [
          [ "Multiple Gets (mget)", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md276", null ],
          [ "Multiple Sets (mset)", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md277", null ]
        ] ],
        [ "Parallel Operations", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md278", [
          [ "Parallel Get", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md279", null ],
          [ "Parallel Set", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md280", null ],
          [ "Parallel Scan", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md281", null ]
        ] ],
        [ "Durability Management", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md282", null ],
        [ "Database Scanning", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md283", null ]
      ] ],
      [ "Performance Tuning", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md284", [
        [ "Slot Configuration", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md285", null ],
        [ "Memory Prefetching", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md286", null ],
        [ "SIMD Optimization", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md287", null ],
        [ "Thread Count Selection", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md288", null ]
      ] ],
      [ "Best Practices", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md289", [
        [ "Key Design", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md290", null ],
        [ "Value Management", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md291", null ],
        [ "Concurrency Patterns", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md292", null ],
        [ "Error Handling", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md293", null ]
      ] ],
      [ "Troubleshooting", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md294", [
        [ "Common Issues", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md295", [
          [ "Database Won't Open", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md296", null ],
          [ "High Memory Usage", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md297", null ],
          [ "Slow Performance", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md298", null ],
          [ "Data Loss", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md299", null ]
        ] ],
        [ "Debugging", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md300", null ]
      ] ],
      [ "Examples", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md301", [
        [ "Session Store", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md302", null ],
        [ "Cache Layer", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md303", null ],
        [ "Bulk Import", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md304", null ],
        [ "Monitoring and Metrics", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md305", null ]
      ] ],
      [ "Migration Guide", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md306", [
        [ "From Redis", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md307", null ],
        [ "From memcached", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md308", null ]
      ] ],
      [ "Performance Benchmarks", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md309", null ],
      [ "Conclusion", "de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md310", null ]
    ] ],
    [ "Contributing to maph", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html", [
      [ "Code of Conduct", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md335", null ],
      [ "How to Contribute", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md336", [
        [ "Reporting Issues", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md337", null ],
        [ "Suggesting Features", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md338", null ],
        [ "Submitting Pull Requests", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md339", null ]
      ] ],
      [ "Coding Standards", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md340", [
        [ "C++ Style Guide", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md341", null ],
        [ "Example Code", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md342", null ]
      ] ],
      [ "Testing Guidelines", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md343", [
        [ "Test Structure", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md344", null ]
      ] ],
      [ "Documentation", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md345", null ],
      [ "Performance Considerations", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md346", null ],
      [ "Review Process", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md347", null ],
      [ "Development Setup", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md348", [
        [ "Prerequisites", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md349", null ],
        [ "Recommended Tools", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md350", null ],
        [ "Building with Sanitizers", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md351", null ]
      ] ],
      [ "Areas for Contribution", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md352", null ],
      [ "Questions?", "d6/dcd/md__c_o_n_t_r_i_b_u_t_i_n_g.html#autotoc_md353", null ]
    ] ],
    [ "Changelog", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html", [
      [ "<a href=\"https://github.com/yourusername/maph/compare/v1.0.0...HEAD\"", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md355", [
        [ "Added", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md356", null ],
        [ "Changed", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md357", null ],
        [ "Fixed", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md358", null ]
      ] ],
      [ "<a href=\"https://github.com/yourusername/maph/compare/v0.9.0...v1.0.0\" >1.0.0</a> - 2024-01-15", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md359", [
        [ "Added", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md360", null ],
        [ "Changed", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md361", null ],
        [ "Fixed", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md362", null ],
        [ "Performance", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md363", null ]
      ] ],
      [ "[0.9.0] - 2023-12-01", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md364", [
        [ "Added", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md365", null ],
        [ "Changed", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md366", null ],
        [ "Deprecated", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md367", null ],
        [ "Fixed", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md368", null ]
      ] ],
      [ "[0.8.0] - 2023-10-15", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md369", [
        [ "Added", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md370", null ],
        [ "Changed", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md371", null ],
        [ "Fixed", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md372", null ]
      ] ],
      [ "[0.7.0] - 2023-08-01", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md373", [
        [ "Added", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md374", null ],
        [ "Known Issues", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md375", null ]
      ] ],
      [ "Comparison with Alternatives", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md376", [
        [ "vs Redis", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md377", null ],
        [ "vs memcached", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md378", null ],
        [ "vs RocksDB", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md379", null ]
      ] ],
      [ "Upgrade Guide", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md380", [
        [ "From 0.x to 1.0.0", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md381", null ]
      ] ],
      [ "Roadmap", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md382", [
        [ "Version 1.1.0 (Q2 2024)", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md383", null ],
        [ "Version 1.2.0 (Q3 2024)", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md384", null ],
        [ "Version 2.0.0 (Q4 2024)", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md385", null ]
      ] ],
      [ "Contributing", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md386", null ],
      [ "License", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md387", null ],
      [ "Acknowledgments", "d1/d5b/md__c_h_a_n_g_e_l_o_g.html#autotoc_md388", null ]
    ] ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", null ],
        [ "Functions", "namespacemembers_func.html", null ],
        [ "Variables", "namespacemembers_vars.html", null ],
        [ "Typedefs", "namespacemembers_type.html", null ],
        [ "Enumerations", "namespacemembers_enum.html", null ],
        [ "Enumerator", "namespacemembers_eval.html", null ]
      ] ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", "functions_vars" ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Enumerations", "functions_enum.html", null ],
        [ "Related Symbols", "functions_rela.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", "globals_dup" ],
        [ "Functions", "globals_func.html", "globals_func" ],
        [ "Variables", "globals_vars.html", null ],
        [ "Typedefs", "globals_type.html", null ],
        [ "Enumerator", "globals_eval.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"annotated.html",
"d1/d87/classmaph_1_1_maph.html#a978b07cf927266378e68748e2e98aa30",
"d3/d82/structmaph_1_1_hybrid_slot.html#a84f9e397b42a9e011789e946773f2408",
"d5/d4e/structhttplib_1_1_response.html",
"d6/d41/maph_8hpp.html#a21c9d98fdf56e82d2ed821997da22799",
"d8/da7/namespacehttplib.html",
"d9/d50/classhttplib_1_1_task_queue.html#a1daf863086c844a78086d4ac04147ae8",
"dd/d0a/classhttplib_1_1_client.html",
"dd/d24/structus__internal__loop__data__t.html#aee21dcc945c88c5f146f165b15e4e912",
"dd/df9/classhttplib_1_1_client_impl.html#a67780ca04da39d934fd30d3a5ca25c42",
"de/d23/md_docs_2_u_s_e_r___g_u_i_d_e.html#autotoc_md297",
"df/dca/httplib_8h.html#ade1f5845ce6b8d8fdb3a56676db48fbaaa390275b35b09350f1fb7a9466879206"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';