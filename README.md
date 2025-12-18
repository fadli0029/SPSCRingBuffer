# SPSCRingBuffer
A fast lock-free single-producer single-consumer ring buffer in modern C++.

## Usage

```cpp
#include <fadli/SPSCRingBuffer.hpp>
#include <thread>

fadli::SPSCRingBuffer<int> q(1024);

// Producer
std::thread producer([&] {
    for (int i = 0; i < 100; ++i) {
        while (!q.try_push(i));
    }
});

// Consumer
std::thread consumer([&] {
    for (int i = 0; i < 100; ++i) {
        while (auto* val = q.front()) {
            process(*val);
            q.pop();
        }
    }
});
```

# TODO's
- [ ] Custom allocator support
- [ ] Write tests
- [ ] Intense stress test
- [ ] Benchmark against Erik Rigtorp's, Facebook's Folly, Boost's, moodycamel's, Drogalis's SPSC-Queue implementations

# References/Inspirations
- [Erik Rigtorp's SPSC Queue](https://github.com/rigtorp/SPSCQueue/)
- [Optimizing a ring buffer for throughput, Erik Rigtorp](https://rigtorp.se/ringbuffer/)
- [Drogalis's SPSC Queue](https://github.com/drogalis/SPSC-Queue)
