# Vienna Lock Less Table (VLLT)

The Vienna Lock Less Table (VLLT) defines C++20 containers that like std::vector allow to store arbitrary sets of data. However, they are meant as 2-dimensional tables, i.e., each row can store *N* independent data items. The unique features of VLLT are:
* Cache optimal data layout. Data is stored in independent segments, resulting in a consecutive layout.
* The data layout can be row-oriented or column-oriented. So depending on your average access patterns, you can select the layout with best cache usage.
* Allows operations for multithreaded access. Tables can grow seamlessly without stopping operations.
* Allows the use of PMR allocators optimized for segment allocation and deallocation.
* Components can be copyable, movable or be even atomics. In the latter case, only values can be copied, not the atomics themselves.
* Lockless operations. VLLT does not use mutexes, only compare and swap (CAS).
* VlltStack: lockless push and pop.
* VlltFifoQueue: lockless push and pop.

VLLT uses other projects:
* Vienna Strong Type (VSTY), see https://github.com/hlavacs/ViennaStrongType.git
* Vienna Type List Library (VTLL), see https://github.com/hlavacs/ViennaTypeListLibrary.
The are included as Git submodules! So you must first init and udate them - see below!

VLLT's base class VlltTable is meant as a building block to create more complex containers for multithreaded access. Examples are given by VlltStack and VlltFifoQueue.


## Cloning VLLT

For cloning VLLT, do the following:

git clone https://github.com/hlavacs/ViennaLockLessTable.git
cd ViennaLockLessTable
git submodule init
git submodule update

For making sure that the submodules contain the latest version, cd into them and pull:
cd ViennaStrongType
git pull origin main
cd ..\ViennaLockLessTable
git pull origin main


## Using VLLT

VLLT is a header-only library. Include ViennaStrongType/VSTY.h, ViennaTypeListLibrary/VLLT.h and VLLT.h into your project.


## The VLLT API

VLLT's main base class is VlltTable. The class depends on the following template parameters:
* DATA: a type list containing the column types that are stored in the table.
* N0: the minimal *size* of a segment. VTLL stores its data in segments of size *N*. Here *N* is the smallest power of 2 that is equal or larger than *N0*. So if *N0* is not a power of 2, VTLL will chose the next larger power of 2 as size. The default value is 1024.
* ROW: a boolean determining the data layout. If true, the layout is row-oriented. If false, it is column-oriented. The default value is false.
* SLOTS: the initial *number* of segments that can be stored. If more are needed, then this is gradually doubled.
* table_index_t: a data type that is used for indexing the rows of the table. Its default is *size_t*, but you can use your own strong types if you want.

VlltTable offers a slim API only.
* Get address of the I'th component of row n. This must be synchronized externally, e.g. for writing.
* Insert a new row to the end of the table. This is synchronized internally, the table can grow as long as there is memory.

VlltTable casn be used as a basis for deriving more complex behavior. The main functionality of VLLT is provided by the classes VlltStack and VlltFifoQueue.

VlltStack is a growable stack that offers the following API:
* push_back: add a new row to the stack. Internally synchronized.
* pop_back: remove the last row from the stack and copy/move values to a tuple. Internally synchronized.
* clear: remove all rows from the stack. Internally synchronized.
* compress: deallocate unused segments. Internally synchronized.
* get: get reference to the I'th component of row n. Externally synchronized.
* get: get reference to the component of type C of row n. Only available if component types are unique. Externally synchronized.
* get_tuple: get a tuple with references to all components of row n. Externally synchronized.
* swap: swap values of two rows. Externally synchronized.
* size: number of rows in the stack.

An example for setting up a stack is

```c
    #include <vector>
    #include <optional>
    #include "VLLT.h"
    using types = vtll::tl<size_t, double, float, std::atomic<bool>, char>;
    vllt::VlltStack<types> stack;
    using idx_stack_t = decltype(stack)::table_index_t;
```

This way you can always get access to the index type used in the stack, which by default is size_t, but might be any strong type.

VlltFifoQueue is a fifo queue that can grow as demand dictates. It offers the following API:
* push_back:
* pop_front:
* size:
* clear:
