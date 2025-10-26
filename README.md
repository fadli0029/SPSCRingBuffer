# SPSCRingBuffer
This is a fast lock-free spsc ring buffer/circular queue, implemented in modern C++.

# TODO's
- [ ] Custom allocator support
- [ ] Write tests
- [ ] Intense stress test
- [ ] Benchmark against Erik Rigtorp's, Facebook's Folly, Boost's, moodycamel's, Drogalis's SPSC-Queue implementations

# References/Inspirations
- [Erik Rigtorp's SPSC Queue](https://github.com/rigtorp/SPSCQueue/)
- [Optimizing a ring buffer for throughput, Erik Rigtorp](https://rigtorp.se/ringbuffer/)
- [Drogalis's SPSC Queue](https://github.com/drogalis/SPSC-Queue)
