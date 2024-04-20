#include "VLLT.h"

using namespace std::chrono; 


/// @brief 
void functional_test() {
	using types = vtll::tl<double, float, int, char, std::string>;
	vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_RELAXED, 1 << 5> table;

	{
		auto view = table.view<char, std::string, double, int, float >(); //swap types to test
		for( int i = 0; i < 100; i++ ) {
			view.push_back((double)i, (float)i, i, 'a', std::string("Hello")); //inserting new rows always in order of the table types!
		}
	}

	{
		auto view = table.view<double, char, vllt::VlltWrite, int, float>();
		auto data = view.get( vllt::table_index_t{0} );

		std::cout << "Data: " << std::get<const double&>(data) << " " << std::get<const char&>(data) 
				  << " " << std::get<int&>(data) << " " << std::get<float&>(data) << " " << std::endl;

		auto d0 = std::get<0>(data); //copy of the data (double), not a reference
		d0 = 3.0; //change the copy, not the value in the table

		decltype(auto) d1 = std::get<1>(data); //const reference!
		//d1 = 'U';  //compile error

		decltype(auto) d2 = std::get<2>(data); //reference
		d2 = 3.0f;	//change the value in the table

		auto d3 = std::get<3>(data); //copy of the data (int)
		d3 = 3;	//change the copy, not the value in the table

		std::cout << "Data: " << std::get<0>(data) //unchanged
				  << " " << std::get<1>(data)
				  << " " << std::get<2>(data) //changed to new value
				  << " " << std::get<3>(data) << " "  << std::endl; //unchanged

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
			auto data = view1.get( vllt::table_index_t{(uint64_t)i} );
			auto data2 = view2.get( vllt::table_index_t{(uint64_t)i} );
			assert( std::get<0>(data) == (double)i && std::get<1>(data) == (float)i ); 
			assert( std::get<2>(data) == i && std::get<3>(data) == 'a' && std::get<4>(data) == "Hello" );
		}
	}


	{
		auto view  = table.view();
		for( int i = 0; i < 10; i++ ) {	
			auto data = view.get( vllt::table_index_t{0} );
			std::cout << "Data: " << view.size() << " " << std::get<0>(data) << " " << std::get<1>(data) << " " << std::get<2>(data) << " " << std::get<3>(data) << " " << std::get<4>(data) << std::endl;
			view.erase( vllt::table_index_t{0} );
		}

		//auto view2  = table.view(); //assert fails for DEBUG or DEBUG_RELAXED
	}

	{
		auto view = table.view< double, vllt::VlltWrite, float, int, char, std::string>();
		for( decltype(auto) el : view ) { //need to use decltype(auto) to get the references right
			auto d = std::get<const double&>(el);
			std::get<float&>(el) = 0.0f;
			std::get<int&>(el) = 1;
			std::get<char&>(el) = 'b';
			std::get<std::string&>(el) = "0.0f";
		}
	}

	{
		auto view = table.view< float, vllt::VlltWrite, double>();
		auto it = view.begin();
		for( auto it = view.begin(); it != view.end(); ++it) {
			std::cout << "Data: " << std::get<double &>(*it) << std::endl;
		}
	}

	{
		auto view = table.view< float, vllt::VlltWrite, double>();
		auto it = view.begin();
		for( int64_t i=0; (uint64_t)i < view.size(); ++i) {
			std::get<double&>( it[ vllt::table_diff_t{i} ] ) = (double)i;
			std::cout << "Data2: " << view.size() << " " << std::get<double&>( it[ vllt::table_diff_t{i} ] ) << std::endl;
		}
	}

	{
		auto view = table.view();
		for( int i=0; i<10; ++i) {
			auto last = view.pop_back();
			std::cout << "Pop: " << std::get<double>(last) << std::endl;
		}
		view.clear();
		std::cout << "Size: " << view.size() << std::endl;
	}

	{
		auto stack = table.stack();
		
		for( int i = 0; i < 10; i++ ) {
			stack.push_back((double)i, (float)i, i, 'a', std::string("Hello"));
		}

		auto ret = stack.pop_back();
		while( ret.has_value() ) {
			std::cout << "Stack Size: " << stack.size() << std::endl;
			ret = stack.pop_back();
		}
	}

}


int main() {
	std::cout << std::thread::hardware_concurrency() << " Threads\n";
	functional_test();
	return 0;
}


