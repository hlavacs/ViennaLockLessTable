# Vienna Lock Less Table (VLLT)

The Vienna Lock Less Table (VLLT) is a C++20 container that like std::vector allows to store arbitrary sets of data. However, it is meant as 2-dimensional table, i.e., each row can store *N* independent data items. The unique features of VLLT are:
* Cache optimal data layout. Data is stored in independent segments, and there consecutively.
* The data layout can be row-oriented or column-oriented.
* Allows operations for multithreaded access: push_back, pop_back, compress. The table can grow seamlessly without stopping operations.
* Lockless operations. VLLT does not use mutexes, only compare and swap (CAS).
* Can be used as basis to create more advanced containers.

VLLT uses another project for setting its template parameters, the Vienna Type List Library (VTLL), see https://github.com/hlavacs/ViennaTypeListLibrary. Just put the header files into the same directory.

VLLT is meant as a building block to create more complex containers for multithreaded access. VLLT does not use mutexes and provides internal synchronization only if you use it as a stack-like container (push_back, pop_back). An example for such a more complex container is the partner project Vienna Entity Component System (VECS), see https://github.com/hlavacs/ViennaEntityComponentSystem. VECS uses an adaptor to additionally enable erasing arbitrary rows without using mutexes.

# Using VLLT

VLLT is a header-only library, just include it and make sure that VTLL.h can be found as well.


# The VLLT API

VLLT contains only one class, the VlltTable.
