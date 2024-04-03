


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
	vllt::VlltStaticTable<types, vllt::VLLT_SYNC_EXTERNAL> table;

	for( int i = 0; i < 100000; i++ ) {
		table.push_back(std::nullopt, (double)i, (float)i, i, 'a', std::string("Hello"));
	}

	{
		auto view = table.view<double, float, int, char, std::string>();

		for( int i = 0; i < table.size(); i++ ) {	
			auto data = view.get( vllt::table_index_t{i} ).value();
			assert( std::get<0>(data) == (double)i && std::get<1>(data) == (float)i );
			assert( std::get<2>(data) == i && std::get<3>(data) == 'a' && std::get<4>(data) == "Hello" );
		}
	}

	{
		auto view = table.view< vllt::VlltWrite, double, float, int, char, std::string>();
		for( decltype(auto) el : view ) {
			std::get<0>(el) = 0.0;
			std::get<1>(el) = 0.0f;
			std::get<2>(el) = 0;
			std::get<3>(el) = 'b';
			std::get<4>(el) = "0.0f";
		}

		
	}

	{
		auto view = table.view< vllt::VlltWrite, double,float, int, char, std::string>();
		auto last = view.pop_back();
		view.clear();
	}

}



int main() {
	std::cout << std::thread::hardware_concurrency() << " Threads\n";
	functional_test();
	return 0;
}


