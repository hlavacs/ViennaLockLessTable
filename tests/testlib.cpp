#include <latch>
#include <set>	

#include "VLLT.h"

using namespace std::chrono; 


void functional_test() {


}




//------------------------------------------------------------------------------------------


/*
/// @brief 
void functional_test_static() {
	using types = vtll::tl<double, float, int, char, std::string>;
	vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_EXTERNAL_PUSHBACK, 1 << 5> table;

	{
		auto view = table.view<vllt::VlltWrite>();
		//auto ret = view.get( vllt::table_index_t{0} ); //compile error
		for( int i = 0; i < 100; i++ ) {
			view.push_back((double)i, (float)i, i, 'a', std::string("Hello")); //inserting new rows always in order of the table types!
		}
	}

	{
		auto view = table.view<double, char, vllt::VlltWrite, int, float>();
		auto data = view.get_ref_tuple( vllt::table_index_t{0} );

		std::cout << "Data: " << vllt::get<const double&>(data) << " " << vllt::get<const char&>(data) 
				  << " " << vllt::get<int&>(data) << " " << vllt::get<float&>(data) << " " << std::endl;

		auto d0 = vllt::get<0>(data); //copy of the data (double), not a reference
		d0 = 3.0; //change the copy, not the value in the table

		decltype(auto) d1 = vllt::get<1>(data); //const reference!
		//d1 = 'U';  //compile error

		decltype(auto) d2 = vllt::get<2>(data); //reference
		d2 = 3;	//change the value in the table

		auto d3 = vllt::get<3>(data); //copy of the data (int)
		d3 = 3.0f;	//change the copy, not the value in the table

		std::cout << "Data: " << vllt::get<0>(data) //unchanged
				  << " " << vllt::get<1>(data)
				  << " " << vllt::get<2>(data) //changed to new value
				  << " " << vllt::get<3>(data) << " "  << std::endl; //unchanged

	}

	{
		auto view = table.view();
		view.clear();
		for( int i = 0; i < 100; i++ ) {
			view.push_back((double)i, (float)i, i, 'a', std::string("Hello")); //inserting new rows always in order of the table types!
		}
	}

	{
		auto view1  = table.view<double, float, int, char, std::string>();
		auto view2  = table.view<double, int, float, std::string>();  //read only compatible

		for( uint64_t i = 0; i < table.size(); i++ ) {	
			auto data = view1.get_ref_tuple( vllt::table_index_t{(uint64_t)i} );
			auto data2 = view2.get_ref_tuple( vllt::table_index_t{(uint64_t)i} );
			assert( vllt::get<0>(data) == (double)i && vllt::get<1>(data) == (float)i ); 
			assert( vllt::get<2>(data) == i && vllt::get<3>(data) == 'a' && vllt::get<4>(data) == "Hello" );
		}
	}

	{
		auto view  = table.view();
		for( int i = 0; i < 10; i++ ) {	
			auto data = view.get_ref_tuple( vllt::table_index_t{0} );
			std::cout << "Data: " << view.size() << " " << vllt::get<0>(data) << " " << vllt::get<1>(data) << " " << vllt::get<2>(data) << " " << vllt::get<3>(data) << " " << vllt::get<4>(data) << std::endl;
			view.erase( vllt::table_index_t{0} );
		}

		//auto view2  = table.view(); //assert fails for DEBUG or DEBUG_RELAXED
	}

	{
		auto view = table.view< double, vllt::VlltWrite, float, int, char, std::string>();
		for( decltype(auto) el : view ) { //need to use decltype(auto) to get the references right
			auto d = vllt::get<const double&>(el);
			vllt::get<float&>(el) = 0.0f;
			vllt::get<int&>(el) = 1;
			vllt::get<char&>(el) = 'b';
			vllt::get<std::string&>(el) = "0.0f";
		}
	}

	{
		auto view = table.view< float, vllt::VlltWrite, double>();
		auto it = view.begin();
		for( auto it = view.begin(); it != view.end(); ++it) {
			std::cout << "Data: " << vllt::get<double &>(*it) << std::endl;
		}
	}

	{
		auto view = table.view< float, vllt::VlltWrite, double>();
		auto it = view.begin();
		for( int64_t i=0; (uint64_t)i < view.size(); ++i) {
			vllt::get<double&>( it[ vllt::table_diff_t{i} ] ) = (double)i;
			std::cout << "Data2: " << view.size() << " " << vllt::get<double&>( it[ vllt::table_diff_t{i} ] ) << std::endl;
		}
	}

	{
		auto view = table.view();
		view.clear();
		for( int i = 0; i < 10; i++ ) {
			view.push_back((double)i, (float)i, i, 'a', std::string("Hello")); //inserting new rows always in order of the table types!
		}
	}


	{
		auto types = table.get_types();
		auto view = table.view<double, float, vllt::VlltWrite, int, char, std::string>();
		auto q = view.get(vllt::table_index_t{0}); //std::any container
		auto ptr = vllt::get<double const*>(q);

		vllt::VlltStaticTableViewBase* view2 = &view;
		auto p = view2->get(vllt::table_index_t{0}); //std::any container
		
		std::cout << "Types:";
		for( size_t i=0; i<vllt::size(p); ++i) {
			auto a = vllt::any(p, i);
			std::cout << " " << a.type().name();
		}
		std::cout << std::endl;

		for( auto p : *view2 ) { //range based loop, returns std::vector<std::any> holding pointers!
			std::cout << "Data: " << vllt::get<double const&>(p) << " " << vllt::get<float const&>(p) << " " << vllt::get<int&>(p) << " " << vllt::get<char&>(p) << " " << vllt::get<std::string&>(p) << std::endl;
			*vllt::get<int*>(p) = vllt::get<double const>(p) * 2;
		}
		for( auto p : *view2 ) { //range based loop, returns std::vector<std::any> holding pointers!
			std::cout << "Data: " << vllt::get<double const&>(p) << " " << vllt::get<float const&>(p) << " " << vllt::get<int&>(p) << " " << vllt::get<char&>(p) << " " << vllt::get<std::string&>(p) << std::endl;
		}
	}

	{
		auto view = table.view();
		for( int i=0; i<10; ++i) {
			auto last = view.pop_back();
			std::cout << "Pop: " << vllt::get<double>(last) << std::endl;
		}
		view.clear();
		std::cout << "Size: " << view.size() << std::endl;
	}

	{
		vllt::VlltStack<double, 1 << 5> stack;
		
		for( int i = 0; i < 10; i++ ) {
			stack.push_back((double)i);
		}

		for( auto ret = stack.pop_back(); ret.has_value(); ret = stack.pop_back() ) {
			std::cout << "Stack Size: " << stack.size() << std::endl;			
		}
	}



}


template<vllt::sync_t SYNC>
void parallel_test_static(int num_threads = std::thread::hardware_concurrency() ) {
	using types = vtll::tl<double, float, int, char, std::string>;
	vllt::VlltStaticTable<types, SYNC, 1 << 5, false, 8> table;

	std::latch start_work{num_threads};
	std::latch start_read{num_threads};

	auto num = 10000;

	auto write = [&](int id){
		std::cout << "Write: ID " << id << std::endl;
		start_work.arrive_and_wait();

		auto view = table.template view<vllt::VlltWrite>();
		for( int i = 0; i < num; i++ ) {
			view.push_back((double)i, (float)i, id, 'a', std::string("Hello")); //inserting new rows always in order of the table types!
		}
	};

	auto read = [&](int id){
		std::cout << "Read: ID " << id << std::endl;
		start_read.arrive_and_wait();

		auto view = table.template view<double, float, int, char, std::string>();
		auto s = view.size();		
		assert( s == num*num_threads );
		for( int i = 0; i < s; i++ ) {
			auto data = view.get_ref_tuple( vllt::table_index_t{(uint64_t)i} );
			//std::cout << "Read: ID " << id << " " << vllt::get<0>(data) << " " << vllt::get<1>(data) << " " << vllt::get<2>(data) << " " << vllt::get<3>(data) << " " << vllt::get<4>(data) << std::endl;
		}
	};

	{
		std::vector<std::jthread> threads;
		for( int i = 0; i < num_threads; i++ ) {
			threads.emplace_back( write, i );
		}
	}

	std::cout << "Table size: " << table.size() << std::endl;

	{
		std::vector<std::jthread> threads;
		for( int i = 0; i < num_threads; i++ ) {
			threads.emplace_back( read, i );
		}
	}

	std::vector<std::set<double>> sizes;
	sizes.resize(num_threads);
	auto view = table.view();

	for( int i = 0; i < num_threads; i++ ) {
		for( int j=0; j < view.size(); j++ ) {
			auto data = view.get_ref_tuple( vllt::table_index_t{(uint64_t)j} );
			if( vllt::get<int&>(data) == i ) {
				//std::cout << "DATA " << view.size() << " " << vllt::get<0>(data) << " " << vllt::get<1>(data) << " " << vllt::get<2>(data) << " " << vllt::get<3>(data) << " " << vllt::get<4>(data) << std::endl;
				sizes[i].insert( vllt::get<0>(data) );
			}
		}
	}

	for( int i = 0; i < num_threads; i++ ) {		
		std::cout << sizes[i].size() << std::endl;
	}
}
*/


int main() {
	std::cout << std::thread::hardware_concurrency() << " Threads" << std::endl;
	//functional_test_static();
	//parallel_test_static<vllt::sync_t::VLLT_SYNC_DEBUG_PUSHBACK>( );
	return 0;
}


