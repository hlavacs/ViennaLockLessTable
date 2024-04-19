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

VLLT has been developed with MS Visual Code and cmake extensions. In MS Visual Code, select View/Command Palette/CMake: Configure and View/Command Palette/CMake: Build .


## Using VLLT

VLLT is a header-only C++ library. Simply include VLLT.h into your C++ project.


## The VLLT API

VLLT's main base class is VlltStaticTable. The class depends on the following template parameters:
* DATA: a type list containing the column types that are stored in the table.
* SYNC: Value of type sync_t, defining how the table can be accessed from multiple threads.
* N0: the minimal *size* of a block. VTLL stores its data in blocks of size *N*. Here *N* is the smallest power of 2 that is equal or larger than *N0*. So if *N0* is not a power of 2, VTLL will chose the next larger power of 2 as size. The default value is 32.
* ROW: a boolean determining the data layout. If true, the layout is row-oriented. If false, it is column-oriented. The default value is false.
* SLOTS: the initial *number* of segments that can be stored. If more are needed, then this is gradually doubled.
* FAIR: If true, the table tries to balance pushes and pulls. This should be used only for stacks.

You create a table using a list of column types. Types must be unique!
```c
using types = vtll::tl<double, float, int, char, std::string>;
vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_RELAXED, 1 << 5> table;
```

VlltStaticTable offers a slim API only:
```
size(): return the number of rows in the table.
view(): create a view to the table.
stack(): create a stackl from the table.
push_back(): insert a new row to the end of the table. 
```


## VlltStaticTableView


```c
using types = vtll::tl<double, float, int, char, std::string>;
vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_RELAXED, 1 << 5> table;

for( int i = 0; i < 10000; i++ ) {
	table.push_back((double)i, (float)i, i, 'a', std::string("Hello"));
}

{
	auto view1  = table.view<double, float, int, char, std::string>();
	auto view2  = table.view<double, float, int, char, std::string>();

	for( int i = 0; i < table.size(); i++ ) {	
		auto data = view1.get( vllt::table_index_t{i} );
		assert( std::get<0>(data) == (double)i && std::get<1>(data) == (float)i ); 
		assert( std::get<2>(data) == i && std::get<3>(data) == 'a' && std::get<4>(data) == "Hello" );
	}
}

{
	auto view = table.view< double, vllt::VlltWrite, float, int, char, std::string>();
	for( decltype(auto) el : view ) {
		auto d = std::get<const double&>(el);
		std::get<float&>(el) = 0.0f;
		std::get<int&>(el) = 1;
		std::get<char&>(el) = 'b';
		std::get<std::string&>(el) = "0.0f";
	}
}
```

When creating views, the columns to read and write must be specified. Creating a view may also entail enforcing parallel access restrictions. Which restrictions apply is specified by the SYNC option:
* VLLT_SYNC_EXTERNAL: there are no restrictions. Synchronization is done externally, you can create any view any time. In game engines this can be enforced e.g. by a directed acyclic graph that manages access from different game systems.
* VLLT_SYNC_INTERNAL: when creating a view, all columns are locked using read and write locks. Reading columns can be done by arbitrary readers, but if a view wants to write to a column, then there can be no other readers or writers to this column. In conflict, the construction of views is blocked by spin locks until the conflict is removed. Also, the view can push back new rows only if it is the current owner, i.e. it is allowed to write to all columns.
* VLLT_SYNC_INTERNAL_RELAXED: Like VLLT_SYNC_INTERNAL, but irrespective of ownership, the view can push back a new row any time. This does not influence other readers and writers. 
* VLLT_SYNC_DEBUG: Like VLLT_SYNC_INTERNAL, but instead of blocked wait the construction of a view results in a failed assertion. This is meant to catch all violations during debugging, but afterwards switch it of at shipping.
* VLLT_SYNC_DEBUG_RELAXED: Like VLLT_SYNC_INTERNAL_RELAXED, but allows creation of new rows any time.


## VlltStaticStack

VlltStack is a growable stack that offers the following API:
* push_back: add a new row to the stack. Internally synchronized.
* pop_back: remove the last row from the stack and copy/move values to a tuple. Internally synchronized.


An example for setting up a stack is

```c
using types = vtll::tl<double, float, int, char, std::string>;
vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_RELAXED, 1 << 5> table;

auto stack = table.stack();

for( int i = 0; i < 10000; i++ ) {
	stack.push_back((double)i, (float)i, i, 'a', std::string("Hello"));
}




```


