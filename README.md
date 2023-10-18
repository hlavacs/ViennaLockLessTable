# Vienna Lock Less Table (VLLT)

The Vienna Lock Less Table (VLLT) defines C++20 containers that like std::vector allow to store arbitrary sets of data. However, they are meant as 2-dimensional tables, i.e., each row can store *N* independent data items. The unique features of VLLT are:
* Cache optimal data layout. Data is stored in independent segments, resulting in a consecutive layout.
* The data layout can be row-oriented or column-oriented. So depending on your average access patterns, you can select the layout with best cache usage.
* Allows operations for multithreaded access. Tables can grow seamlessly without stopping operations.
* Allows the use of PMR allocators optimized for segment allocation and deallocation.
* Components can be copyable, movable or be even atomics. In the latter case, only values can be copied, not the atomics themselves.
* Lockless operations. VLLT does not use mutexes, only compare and swap (CAS).
* VlltStack (i.e., LIFO queue): lockless push back and pop back.
* VlltFIFOQueue: lockless push back and pop front.

VLLT uses other projects:
* Vienna Strong Type (VSTY), see https://github.com/hlavacs/ViennaStrongType.git
* Vienna Type List Library (VTLL), see https://github.com/hlavacs/ViennaTypeListLibrary.
You have to clone them next to the ViennaLockLessTable directory, see below.

VLLT's base class VlltTable is meant as a building block to create more complex containers for multithreaded access. Examples are given by VlltStack and VlltFIFOQueue.


## Cloning VLLT

For cloning VLLT, do the following:

```
  git clone https://github.com/hlavacs/ViennaTypeListLibrary.git
  git clone https://github.com/hlavacs/ViennaStrongType.git
  git clone https://github.com/hlavacs/ViennaLockLessTable.git
  cd ViennaLockLessTable
  msvc.bat   //or run cmake
  cmake --build .
```

For making sure that the submodules contain the latest version, cd into them and pull:

```
  cd ViennaStrongType
  git pull origin main
  cd ..\ViennaTypeListLibrary
  git pull origin main
```

## Using VLLT

VLLT is a header-only library. Include ViennaStrongType/VSTY.h, ViennaTypeListLibrary/VLLT.h and VLLT.h into your project.


## The VLLT API

VLLT's main base class is VlltTable. The class depends on the following template parameters:
* DATA: a type list containing the column types that are stored in the table.
* N0: the minimal *size* of a segment. VTLL stores its data in segments of size *N*. Here *N* is the smallest power of 2 that is equal or larger than *N0*. So if *N0* is not a power of 2, VTLL will chose the next larger power of 2 as size. The default value is 1024.
* ROW: a boolean determining the data layout. If true, the layout is row-oriented. If false, it is column-oriented. The default value is false.
* SLOTS: the initial *number* of segments that can be stored. If more are needed, then this is gradually doubled.

VlltTable offers a slim API only.
* Get address of the i'th component of row n. This must be synchronized externally, e.g., for writing.
* Insert a new row to the end of the table. This is synchronized internally, the table can grow as long as there is memory.

VlltTable can be used as a basis for deriving more complex behavior. The main functionality of VLLT is provided by the classes VlltStack and VlltFIFOQueue.

## VlltStack
VlltStack is a growable stack that offers the following API:
* push_back: add a new row to the stack. Internally synchronized.
* pop_back: remove the last row from the stack and copy/move values to a tuple. Internally synchronized.
* clear: remove all rows from the stack. Internally synchronized.
* get: get reference to the I'th component of row n. Externally synchronized.
* get: get reference to the component of type C of row n. Only available if component types are unique. Externally synchronized.
* get_tuple: get a tuple with references to all components of row n. Externally synchronized.
* swap: swap values of two rows. Externally synchronized.
* size: number of elements in the stack.

An example for setting up a stack is

```c
    #include <vector>
    #include <optional>
    #include "VLLT.h"
    using types = vtll::tl<size_t, double, float, std::atomic<bool>, char>;
    vllt::VlltStack<types> stack;
    using stack_index_t = decltype(stack)::table_index_t; //default is size_t

    stack.push_back(static_cast<size_t>(1ull, 2.0, 3.0f, true, 'A');
    stack.push_back(static_cast<size_t>(2ull, 4.0, 6.0f, true, 'A');
    stack.push_back(static_cast<size_t>(3ull, 6.0, 9.0f, true, 'A');

    stack.swap(stack_index_t{ 0 }, stack_index_t{ 1 }); //swap first two rows

    auto tup = stack.get_tuple(stack_index_t{0}); //references to the components
    assert(std::get<0>(tup) == 1);  //first component is size_t
    stack.swap(stack_index_t{ 0 }, stack_index_t{ 1 }); //swap back
    assert(std::get<0>(tup) == 0);

    auto data = stack.pop_back(); //std::optional tuple holding all the values
    if(data.has_value()) {
      assert( std::get<size_t>( data.value() ) == 3); //select by type possible here
    }
```
The stack uses the strong type stack_index_t as index, which as default uses 32 bits. This means that the stack can hold max. 2^32 elements, but synchronization is lockless. If you need larger sizes then redefine it for 64 bits, which means that synchronization most likely uses locks. This is due to the fact that the stack state uses two such values in a single atomic, and lockless usage is guaranteed only for up to 64 bits inside an atomic.

## VlltFIFOQueue
VlltFIFOQueue is a fifo queue that can grow as demand dictates. It offers the following API:
* push_back: Put a new row to the end of the queue.
* pop_front: Get the first element from the queue.
* size: Get nuber of elements in the queue.
* clear: Remove all elements from the queue.

```c
  vllt::VlltFIFOQueue<types, 1 << 10,true,16,size_t> queue;

  queue.push_back(1, 2.0, 3.0f, true, 'A'); //first
  queue.push_back(2, 4.0, 6.0f, true, 'A'); //second
  queue.push_back(3, 6.0, 9.0f, true, 'A'); //third

  auto v = queue.pop_front(); //get first element
  if(v.has_value()) {
    assert(std::get<0>(v.value()) == 1);
  }
```
VlltFIFOQueue uses four atomics holding 64 bits as state variables, each of which can only grow monotonically. Operations are always lockless and the queue has no limits on the number of elements it can hold.
