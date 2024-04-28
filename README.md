# Vienna Lock Less Table (VLLT)

The Vienna Lock Less Table (VLLT) defines C++20 containers that like std::vector allow to store arbitrary sets of data. However, they are meant as 2-dimensional tables, i.e., each row can store *N* independent data items. The unique features of VLLT are:
* Cache optimal data layout. Data is stored in independent segments, resulting in a consecutive layout.
* The data layout can be row-oriented or column-oriented. So depending on your average access patterns, you can select the layout with best cache usage.
* Allows operations for multithreaded access. Tables can grow seamlessly without locking.
* Allows the use of PMR allocators optimized for segment allocation and deallocation.
* Components can be copyable, movable or be even atomics. In the latter case, only values can be copied, not the atomics themselves.
* Optimized for use as stand alone table, or as basis for entity component systems.


VLLT uses other projects:
* Vienna Strong Type (VSTY), see https://github.com/hlavacs/ViennaStrongType.git
* Vienna Type List Library (VTLL), see https://github.com/hlavacs/ViennaTypeListLibrary.

Both are contained in single header files that are automatically copied to the project when cmake configures the project. You do not have to do anything here.

## Cloning VLLT

For cloning VLLT, do the following:

```
git clone https://github.com/hlavacs/ViennaTypeListLibrary.git
cd ViennaLockLessTable
```

VLLT has been developed with MS Visual Code and cmake extensions. In MS Visual Code, select View/Command Palette/CMake: Configure and View/Command Palette/CMake: Build . VLLT has been tested on Windows and Linux with MSCV and Clang compiler.


## Using VLLT

VLLT is a header-only C++ library. Simply include VLLT.h into your C++ project. It uses strong types from VSTY mainly for indexing into the table (vsty::table_index_t, vsty::table_diff_t) and type lists (vtll::tl<>) and selected compile time type list algorithms from VTLL. 


## The VLLT API

VLLT's main base class is VlltStaticTable. The class depends on the following template parameters:
* DATA: a type list containing the column types that are stored in the table.
* SYNC: Value of type sync_t, defining how the table can be accessed from multiple threads.
* N0: the minimal *size* of a block. VTLL stores its data in blocks of size *N*. Here *N* is the smallest power of 2 that is equal or larger than *N0*. So if *N0* is not a power of 2, VTLL will chose the next larger power of 2 as size. The default value is 32.
* ROW: a boolean determining the data layout. If true, the layout is row-oriented. If false, it is column-oriented. The default value is false.
* SLOTS: the initial *number* of blocks that can be stored. If more are needed, then this is gradually doubled. Increasing this number means lokcing the *increase* for a short time, normal operations are not disturbed.
* FAIR: If true, the table tries to balance pushes and pulls. This should be used only for stacks. It also means an increased time spending using atomics.

You create a table using a list of column types. Types must be unique!
```c
using types = vtll::tl<double, float, int, char, std::string>; //typelist contaning the table column types (must be unique)
vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_PUSHBACK, 1 << 5> table;
```

VlltStaticTable offers a slim API only:
```
size(): return the number of rows in the table.
view(): create a view to the table.
push_bak(): insert a new row to the end of the table.
```

## VlltStaticTableView

Table views are he main way to interact with a table, following a data access object (DAO) pattern. Threads can interact with a table through a view, e.g., reading, writing values or inserting new rows etc. When creating views, the columns to read and write must be specified. Creating a view may also entail enforcing parallel access restrictions. In this context, a *push-back-only* view is a view that can only push back new rows or return the size of the table, no more. Which restrictions apply is specified by the SYNC option:

### VLLT_SYNC_EXTERNAL
In this mode there are no restrictions. Synchronization is done externally, you can create views with full access capabilities any time. In game engines, external synchronizaiton can be enforced, e.g., by a directed acyclic graph that manages access from different game systems. The details are:
* Use case single threaded access.
* No internal syncing.
* No atomics used.
* No push-back-only views allowed. 
* Adding new rows by: owning thread.

### VLLT_SYNC_EXTERNAL_PUSHBACK
This is like VLLT_SYNC_EXTERNAL, but additionally *push-back-only* views can be created calling *view<VlltWrite>()* on the table. Using this mechanism, the game engine can allow parallel row creation to a system even if the system currently is not the owner of the table, by passing  push-back-only views of a table to the system. Creating new rows is lock less and does not affect other operations of a table. The details are:
* Use case multithreaded access.
* No internal syncing.
* Atomics are used.
* Push-back-only views allowed. 
* Adding new rows by: owning view and push-back only views.

### VLLT_SYNC_INTERNAL
When creating a view, all columns are locked using read and write locks. Reading columns can be done by arbitrary readers, but if a view wants to write to a column, then there can be no other readers or writers to this column. Also, the view can insert new rows only if it is the current owner, i.e. it is allowed to write to all columns. This mode can be used if there is no external synchronization method, and the tables are rarely written to, but mostly read from. It also allows to add new rows only if the new information depends on the existing information (e.g., avoid duplicates), and the inserting thread can make sure of this before hand using locks. The details are:
* Use case multithreaded access.
* Internal syncing with mutexes.
* Atomics are used.
* No push-back only view allowed.
* Adding new rows by: owning view.

### VLLT_SYNC_INTERNAL_PUSHBACK
Like VLLT_SYNC_INTERNAL, but push-back only views are allowed and can insert a new row any time. Since threads can do this in parallel, this might cause inconsistent rows, e.g., duplicates. The details are:
* Use case multithreaded access.
* Internal syncing with mutexes.
* No atomics used.
* Push-back only view allowed.
* Adding new rows by: owning view and push-back only views.

### VLLT_SYNC_DEBUG
Like VLLT_SYNC_INTERNAL, but instead of blocked wait the construction of a view results in a failed assertion. Use this mode for debugging if the intended final use is VLLT_SYNC_EXTERNAL and you want to make sure that forbidden concurrent operations never happen. This is meant to catch all violations during debugging, but afterwards switch VLLT_SYNC_EXTERNAL for shipping. The details are:
* Use case single threaded access. Fails if multithreaded access is detected.
* Internal syncing with mutexes. Assert fails if conflict.
* Atomics are used.
* No push-back only view allowed.
* Adding new rows by: owning view.

### VLLT_SYNC_DEBUG_PUSHBACK
Like VLLT_SYNC_INTERNAL_PUSHBACK, but allows push-back-only views. The details are:
* Use case single threaded access. Fails if multithreaded access is detected.
* Internal syncing with mutexes. Assert fails if conflict.
* Atomics are used.
* Push-back only view allowed.
* Adding new rows by: owning view and push-back only views.

It must be noted that irrespective of the sync mode, adding new rows at the end of the table will never interfere with normal table operations, be it reading, writing, erasing etc. A view can add new rows in the following situations:
* Its table uses sync modes VLLT_SYNC_EXTERNAL, VLLT_SYNC_INTERNAL_PUSHBACK, VLLT_SYNC_DEBUG_PUSHBACK
* It is the sole owner of the table, i.e., it has write access to all columns.
This enables game systems to produce new items any time without interfering with other systems. 

When creating a view, the columns this view wants to access, as well as the intended use (read only or read/write) must be specified. This is done using variadic type lists in the templated version of the view() function.
```c
using types = vtll::tl<double, float, int, char, std::string>;
vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_PUSHBACK, 1 << 5> table;
auto view1 = table.view<vllt::VlltWrite, double, float, int, char, std::string>(); //full ownership
auto view2 = table.view(); //alternative for ownership
```
The above code creates two views having full read/write ownership of all table columns. Notice the tag *vllt::VlltWrite*, which flags write accesses for the following types. An alternative is given by not specifying any type, which also grants write access to all table columns. You can mix read and write accesses by moving the tag to different places like so:
```c
using types = vtll::tl<double, float, int, char, std::string>;
vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_PUSHBACK, 1 << 5> table;
auto view = table.view<double, float, vllt::VlltWrite, std::string>(); //read to the first two, write to the last
```
In the above exaple, *view* has read access to the *double* and *float* type, and write access to *std::string*. It does not have access to *int* and *char*, so any other thread can concurrently create any view that either also reads from *double* and *float*, or even writes to *int* and *char* without interference with this thread. The view in the following example creates read access to all columns, which is compatible with any other reader, but which blocks (or should not be mixed with) write accesses.
```c
auto view = table.view<std::string, int, char, double, float>(); //read access to all columns, ordering irrelevant
```
The ordering of the type list does not matter for a view. It only matters of you insert a new row to the table. 
Accessing the columns is done by calling *get()* on the view. This results in a tuple holding const references to all columns where the view has read accesses, and plain references where the column has write access:
```c
using types = vtll::tl<double, float, int, char, std::string>;
vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_PUSHBACK, 1 << 5> table;
auto view = table.view<double, char, vllt::VlltWrite, int, float>(); //read to the first two, write to the last
//view can insert new data irrespective of rights for VLLT_SYNC_DEBUG_PUSHBACK
view.push_back(0.0, 1.0f, 2, 'a', std::string("Hello")); //type ordering of table types!
auto data = view.get( vllt::table_index_t{0} ); //returns std::tuple<const double&, const char&, int&, float&>
```
In the above code the result variable data is a tuple of type *std::tuple<const double&, const char&, int&, float&>*. Accessing the single components can be done with the *std::get<>()* function. The template argument is either an integer number or the correct types:
```c
std::cout << "Data: " << view.size() << " " << std::get<const double&>(data) << " " << std::get<const char&>(data) 
		  << " " << std::get<int&>(data) << " " << std::get<float&>(data) << " " << std::endl;
```
Care must be taken when accessing the data. Using only auto generats the base type, and copying it creates a copy of the data. The new copy can be changed irrespective of whether the reference was const or not. 
Using *decltype(auto)* creates a copy of the *reference*, and also a const qualifier with it if there is one!
```c
	auto d0 = std::get<0>(data); //d0 is a copy, changing its value does not affect the table.
	d0 = 3.0; //change the copy, not the table

	decltype(auto) d1 = std::get<1>(data); // is the same const reference as in the tuple.
	//d1 = 3; // compile error since the reference is const!

	decltype(auto) d2 = std::get<2>(data); // is the same reference as in the tuple.
	d2 = 3.0f; // this is a reference, so this changes the value in the table!

	auto d3 = std::get<3>(data); //copy of the data (int)
	d3 = 3;	//change the copy, not the value in the table
```
You can use iterators and range based for loops. Care must be taken to use decltype(auto), since using auto alone results in copies of the data instead of references!

```c
{
	auto it = view.begin(); //iterator to the first row
	auto view = table.view< float, vllt::VlltWrite, double>();
	auto it = view.begin();
	for( auto it = view.begin(); it != view.end(); ++it) {
		std::cout << "Data: " << std::get<double &>(*it) << std::endl;
	}
}
{
	auto view = table.view< double, vllt::VlltWrite, float, int, char, std::string>();
	for( decltype(auto) el : view ) {   //need to use decltype(auto) to get the references right
		auto d = std::get<const double&>(el); //read only, get a copy
		std::get<float&>(el) = 0.0f;
		std::get<int&>(el) = 1;
		std::get<char&>(el) = 'b';
		std::get<std::string&>(el) = "0.0f";
	}
}
```



## VlltStack

VlltStack is a growable stack which internally uses static tables. It offers the following API:
* push_back: add a new row to the stack. Internally synchronized.
* pop_back: remove the last row from the stack and copy/move values to an std::optional<T>. Internally synchronized.
Both operations are lockless. A lock is used though if the block mal has to be increased, in order to prevent unnecessary  memory allocations.

A stack has the following declaration:
```c
template<typename T, size_t N0 = 1 << 5, bool ROW = false, size_t MINSLOTS = 16, bool FAIR = false>
class VlltStack;
```
Here *T* is the type of the data the stack can store, the other parameters are equivalent to a static table. An example for setting up a stack is

```c
vllt::VlltStack<double, 1 << 5> stack;

for( int i = 0; i < 10; i++ ) {
	stack.push_back((double)i);
}

for( auto ret = stack.pop_back(); ret.has_value(); ret = stack.pop_back() ) {
	std::cout << "Stack Size: " << stack.size() << std::endl;			
}
```


