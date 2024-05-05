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

Both are contained in single header files that are automatically copied to the project directory extern when cmake configures the project. Make sure to include their directories in your include path.

## Cloning VLLT

For cloning VLLT, do the following:

```
git clone https://github.com/hlavacs/ViennaTypeListLibrary.git
cd ViennaLockLessTable
```

VLLT has been developed with MS Visual Code and cmake extensions. In MS Visual Code, select View/Command Palette/CMake: Configure and View/Command Palette/CMake: Build . VLLT has been tested on Windows and Linux with MSCV and Clang compiler.
For configurng and building either run build.cmd on Windows, or use 


## Using VLLT

VLLT is a header-only C++ library. Simply include VLLT.h into your C++ project. It uses strong types from VSTY mainly for indexing into the table (*vsty::table_index_t*, *vsty::table_diff_t*) and type lists (*vtll::tl<>*) and selected compile time type list algorithms from VTLL. You can change the global constant *VLLT_MAX_NUMBER_OF_COLUMNS* by using *#define* as shown below. This should be set to the maximum number of columns that you use in a table. The default value is 16, but you can override it as shown. If you use tables with larger numbers of columns, then each table with a larger number will output a warning, and the table will use a *std::vector* instead of a pre-allocated *std::array*, requiring heap allocation and being thus less efficient.
```c
#define VLLT_MAX_NUMBER_OF_COLUMNS 32 //default is 16
#include <VLLT.h>
```


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
push_back(): insert a new row to the end of the table.
```

## VlltStaticTableView

Table views are he main way to interact with a table, following a data access object (DAO) pattern. Threads can interact with a table through a view, e.g., reading, writing values or inserting new rows etc. When creating views, the columns to read and write must be specified. Creating a view may also entail enforcing parallel access restrictions. In this context, a *push-back-only* view is a view that can only push back new rows or return the size of the table, no more. Which restrictions apply is specified by the SYNC option:

### VLLT_SYNC_EXTERNAL
In this mode there are no restrictions. Synchronization is done externally, you can create views with full access capabilities any time. In game engines, external synchronizaiton can be enforced, e.g., by a directed acyclic graph that manages access from different game systems. The details are:
* Use case single threaded access. An external mechanism ensures single threaded access.
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

It must be noted that irrespective of the sync mode, adding new rows at the end of the table will not interfere with normal table operations, be it reading, writing, adding, swapping, removing, etc. A view can add new rows in the following situations:
* It is the sole owner of the table, i.e., it has write access to all columns, or
* the table sync mode includes PUSHBACK and the view is a pushback-only view.
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
auto view = table.view<int, vllt::VlltWrite, float, std::string>(); //read to the first two, write to the last
```
In the above example, *view* has read access to the *int* type, and write access to *float* and *std::string*. It does not have access to *double* and *char*, so any other thread can concurrently create any view that either also reads from *int*, or even writes to *double* and *char* without interference with this thread. The view in the following example creates read access to all columns, which is compatible with any other reader, but which blocks (or should not be mixed with) write accesses.
```c
auto view = table.view<std::string, int, char, double, float>(); //read access to all columns, ordering irrelevant
```
The ordering of the type list does not matter for a view. It only matters of you insert a new row to the table. 
Accessing the columns is done by calling *get_ref_tuple()* on the view. This results in a tuple holding const references to all columns where the view has read accesses, and non-const references where the column has write access:
```c
using types = vtll::tl<double, float, int, char, std::string>;
vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_PUSHBACK, 1 << 5> table;
auto view = table.view<double, char, vllt::VlltWrite, int, float>(); //read to the first two, write to the last
auto data = view.get_ref_tuple( vllt::table_index_t{0} ); //returns std::tuple<const double&, const char&, int&, float&>
```
In the above code the result variable data is a tuple of type *std::tuple<const double&, const char&, int&, float&>*. Accessing the single components can be done with the *vllt::get<>()* function. The template argument is either an integer number or the correct types:
```c
std::cout << "Data: " << view.size() << " " << vllt::get<0>(data) << " " << vllt::get<const char&>(data) 
		  << " " << vllt::get<2>(data) << " " << vllt::get<float&>(data) << " " << std::endl;
```
A pushback-only view enables non-owning systems of the game engine to add new objects anyway. This option must be enabled by choosing a sync mode with PUSHBACK in its name. If this is not done, then trying to create a pushback-only view results in a compile error. 
You can create a pushback-only view by using *vllt::VlltWrite* as sole template parameter. If you call any other function than *push_back()* on this view, the result is a compile error. 
```c
using types = vtll::tl<double, float, int, char, std::string>;
vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_PUSHBACK, 1 << 5> table;
auto view1 = table.view(); //view owning the table
view1.push_back(0.0, 0.0f, 0, 'a', std::string("Hello1")); //type ordering of table types!

auto view2 = table.view<vllt::VlltWrite>(); //pushback-only view does not interfere with others
view2.push_back(1.0, 1.0f, 1, 'b', std::string("Hello2")); //type ordering of table types!

auto data1 = view1.get_ref_tuple( vllt::table_index_t{0} ); //returns std::tuple<double&, float, int&, char&, std::string&>
//auto data2 = view2.get_ref_tuple( vllt::table_index_t{0} ); //compile error
```
Care must be taken when accessing the data. Using only auto generats the base type, and copying it creates a copy of the data. The new copy can be changed irrespective of whether the reference was const or not. 
Using *decltype(auto)* creates a copy of the *reference*, and also a const qualifier with it if there is one!
```c
auto d0 = vllt::get<0>(data); //d0 is a copy, changing its value does not affect the table.
d0 = 3.0; //change the copy, not the table

decltype(auto) d1 = vllt::get<1>(data); // is the same const reference as in the tuple.
//d1 = 3; // compile error since the reference is const!

decltype(auto) d2 = vllt::get<2>(data); // is the same reference as in the tuple.
d2 = 3.0f; // this is a reference, so this changes the value in the table!

auto d3 = vllt::get<3>(data); //copy of the data (int)
d3 = 3;	//change the copy, not the value in the table
```
You can use iterators and range based for loops. Care must be taken to use decltype(auto), since using auto alone results in copies of the data instead of references!
```c
{
	auto it = view.begin(); //iterator to the first row
	auto view = table.view< float, vllt::VlltWrite, double>();
	auto it = view.begin();
	for( auto it = view.begin(); it != view.end(); ++it) {
		std::cout << "Data: " << vllt::get<double &>(*it) << std::endl;
	}
}
{
	auto view = table.view< double, vllt::VlltWrite, float, int, char, std::string>();
	for( decltype(auto) el : view ) {   //need to use decltype(auto) to get the references right
		auto d = vllt::get<const double&>(el); //read only, get a copy
		vllt::get<float&>(el) = 0.0f;
		vllt::get<int&>(el) = 1;
		vllt::get<char&>(el) = 'b';
		vllt::get<std::string&>(el) = "0.0f";
	}
}
```
## Dynamic Polymorphism and *get()*
If you want to combine multiple static tables to achieve dynamic polymorphism, e.g., for an entity component system, you can call *get* instead of *get_ref_tuple()*. This results in a *vllt::ptr_array_t* holding non-const or const pointers to the components of a row. VLLT offers three functions to get the component (*vllt::get< T >()*), the number of pointers (*vllt::get_size()*), and the type of a pointer (*vllt::get_any*). *vllt::get_any* returns a *std::any* storing the pointer, which you can also ask for the pointer type. The type T that is specified in *vllt::get< T >()* can be a pointer, a reference or a value. It is important to specify const if the component is a read only component, failing so result in a runtime error.

Dynamic polymorphism is achieved by using *vllt::VlltStaticTableViewBase* pointers, which denote the common base class for all views. The base class also emits generic iterators with its *begin()* and *end()* methods and can be used for range based loops:
```c
auto types = table.get_types();
auto view = table.view<double, float, vllt::VlltWrite, int, char, std::string>();
vllt::VlltStaticTableViewBase* view2 = &view;
auto p = view2->get(vllt::table_index_t{0}); //std::any container
		
std::cout << "Types:";
for( size_t i=0; i<vllt::get_size(p); ++i) {
	auto a = vllt::get_any(p, i);
	std::cout << " " << a.type().name(); //std::any can only store pointers, not references
}
std::cout << std::endl;

for( auto p : *view2 ) { //range based loop, returns std::vector<std::any> holding pointers!
	std::cout << "Data: " << vllt::get<double const&>(p) << " " << vllt::get<float const&>(p) << " " << vllt::get<int&>(p) << " " << vllt::get<char&>(p) << " " << vllt::get<std::string&>(p) << std::endl;
	*vllt::get<int*>(p) = vllt::get<int>(p) * 2; //get a pointer and a value
}
for( auto p : *view2 ) { //range based loop, returns std::vector<std::any> holding pointers!
	std::cout << "Data: " << vllt::get<double const&>(p) << " " << vllt::get<float const&>(p) << " " << vllt::get<int&>(p) << " " << vllt::get<char&>(p) << " " << vllt::get<std::string&>(p) << std::endl;
}
```
As an optimization, the return value *vllt::ptr_array_t* can hold pointers to REF_ARRAY_SIZE = 32 components locally. If you create tables with more columns, then *vllt::ptr_array_t* switches to a std::vector, which is less efficient since this needs heap allocation. If you have tables with more columns, increase the size of REF_ARRAY_SIZE accordingly.


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


