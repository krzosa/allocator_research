Trying out an allocator idea. Arena pool for dynamic arrays / hash tables.

Each thread has a bunch of thread local arena(Arena pool). Arrays request personal arenas from the pool. When they are done with their work they return the arena to the pool.

Arenas reserve big chunks of virtual memory. 
Commit only when needed in adjustable chunks. 
When array is done it returns the arena to the pool.
Released arena decommits it's memory but keeps the reserved region. 

There can be potentially a lot of optimizations that can happen here. 
One might for example create:

* *hot* arenas that don't decommit memory, cause they are reused often. 
* *big* arenas for special cases of ultra big arrays etc.
* Arenas with different lifetime constrains, for example scoped scratch allocations

pros:

* Infinite arrays that don't have to ever relocate.
* Faster then general purpose allocators (windows HeapAlloc, malloc), even in the best case where there are no other objects on the heap.
* Since it's a memory pool there can be no fragmentation in the classic sense. Fixing memory wastage requires fitting parameters to the scenario at hand.
* The only slow code that can happen is calling Virtual calls.
* Can be easily customized for specific needs.

unknowns:

* Hard to tell what sort of upkeeping costs, reserving lots of virtual memory has.

cons:

* Amount of arrays with personalized arenas is bounded but adjustable. 