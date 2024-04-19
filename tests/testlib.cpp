#include "VLLT.h"

using namespace std::chrono; 


/// @brief 
void functional_test() {
	using types = vtll::tl<double, float, int, char, std::string>;
	vllt::VlltStaticTable<types, vllt::sync_t::VLLT_SYNC_DEBUG_RELAXED, 1 << 5> table;

	{
		auto view = table.view<double, float, int, char, std::string>();
		for( int i = 0; i < 100; i++ ) {
			view.push_back((double)i, (float)i, i, 'a', std::string("Hello"));
		}
	}

	{
		auto view1  = table.view<double, float, int, char, std::string>();
		auto view2  = table.view<double, float, int, char, std::string>();

		for( uint64_t i = 0; i < table.size(); i++ ) {	
			auto data = view1.get( vllt::table_index_t{(uint64_t)i} );
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
		for( decltype(auto) el : view ) {
			auto d = std::get<const double&>(el);
			std::get<float&>(el) = 0.0f;
			std::get<int&>(el) = 1;
			std::get<char&>(el) = 'b';
			std::get<std::string&>(el) = "0.0f";
		}
	}

	{
		auto view = table.view< vllt::VlltWrite, double, float, int, char, std::string>();
		auto it = view.begin();
		for( int64_t i=0; (uint64_t)i < view.size(); ++i) {
			std::get<double&>( it[ vllt::table_diff_t{i} ] ) = (double)i;
			std::cout << "Data2: " << view.size() << " " << std::get<double&>( it[ vllt::table_diff_t{i} ] ) << std::endl;
		}

	}

	{
		auto view = table.view< vllt::VlltWrite, double,float, int, char, std::string>();
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


