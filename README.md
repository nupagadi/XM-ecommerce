# XM-ecommerce

Unfortunately, I had only one day for the assignment due to some personal reasons. I didn't have a chance to do profiling and elaborate testing. So probably there are bugs for some edge cases.

## Assignment
The assignment description is in CppEcommerce.pdf.

## Build and Run
g++ -g0 -O3 test.cpp -o ecommerce && ./ecommerce

## Implementation
CachingDatabase class gets the DB by the interface using the Dependecy Injection technique. It implements the same interface as the DB does (IDatabase). It owns a CachedData object. That is a class templated with the Data and the ID types.

The cache is implemented using two unordered_map's: one is by an ID, another is by cache order. So the complexity of the fetch operation is O(1).

First it tries to find an element in the cache by ID. If it is present, the result is returned and the element order is updated.

If it is not present, an "empty" element and a "promising" elements are created. The former contains a shared_future from the latter. The "empty" element also tracks its order (in case other threads will request it, before fulfilling the promise). The elements are initialized with the highest order. And in case the cache max capacity exceded, the element with the lowest order is removed from both unordered_map's. Then the current thread gets the data from the DB, fulfills the promise and returns the result.

If it is not present, but an "empty" element is present instead, it creates a "waiting" element that contains a shared_future, waits for the promise fulfillment and returns the result. It allows to avoid unnecessary fetches from the DB. The element's order is updated, but nothing is removed in this case.

If it is not present, but an "empty" element is present and it is fulfilled, then it replaces it with a "filled" element and return the result. The element's order is updated, but nothing is removed in this case.

All the necessary operations are protected by a mutex. Fetches from the DB and waiting on a future are performed outside of the mutex.
std::share_future allows other threads to continue in case the element is removed by the other thread.

## Testing
Everything related to testing is located in test.cpp.

For the DB I used a stub, that is filled with generated data. The slowness is emulated by std::sleep_for(). The lag can be controlled by the parameter DbLagMs.

To emulate "hot" products I used the Gaussian distribution. It can be tuned by the distribution parameter.

The number of threads and their number of fetches are also can be configured. And the cache size too. Please, see test.cpp.

Compilation of other types for ID and Data is checked. The ID type requires "==" operation.
