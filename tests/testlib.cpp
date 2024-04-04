#include "VLLT.h"

using namespace std::chrono; 

auto wait_for(double us) {
	auto T1 = high_resolution_clock::now();
	double dt, res = 0.0;
	do {
		dt = duration_cast<duration<double>>(high_resolution_clock::now() - T1).count();
		res += dt;
	} while ( 1'000'000.0*dt < us );
	return res;
}


void functional_test() {
	using types = vtll::tl<double, float, int, char, std::string>;
	vllt::VlltStaticTable<types, vllt::VLLT_SYNC_DEBUG> table;

	for( int i = 0; i < 100000; i++ ) {
		table.push_back(std::nullopt, (double)i, (float)i, i, 'a', std::string("Hello"));
	}

	{
		auto view1  = table.view<double, float, int, char, std::string>();
		auto view2  = table.view<double, float, int, char, std::string>();
		auto view3  = table.view<double, float, int, char, std::string>();

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
			std::get<int&>(el) = 0;
			std::get<char&>(el) = 'b';
			std::get<std::string&>(el) = "0.0f";
		}

		
	}

	{
		auto view = table.view< vllt::VlltWrite, double,float, int, char, std::string>();
		auto last = view.pop_back().value();
		view.clear();
		std::cout << "Size: " << view.size() << std::endl;
	}

}



int main() {
	std::cout << std::thread::hardware_concurrency() << " Threads\n";
	functional_test();
	return 0;
}


