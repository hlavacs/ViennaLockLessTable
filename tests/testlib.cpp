


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
		auto view = table.view<vtll::tl<double,float>, vtll::tl<int, char, std::string>>();

		for( int i = 0; i < table.size(); i++ ) {	
			auto data = view.get( vllt::table_index_t{i} ).value();
			assert( std::get<0>(data) == (double)i && std::get<1>(data) == (float)i );
			assert( std::get<2>(data) == i && std::get<3>(data) == 'a' && std::get<4>(data) == "Hello" );
		}
	}

	{
		auto view2 = table.view< vtll::tl<>, vtll::tl<double,float, int, char, std::string>>();
		auto last = view2.pop_back();
		view2.clear();
	}

}



int main() {
	std::cout << std::thread::hardware_concurrency() << " Threads\n";
	functional_test();
	return 0;
}


