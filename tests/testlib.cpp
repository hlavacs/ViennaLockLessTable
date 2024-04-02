


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
	vllt::VlltStaticTable<types> table;

	table.push_back(std::nullopt, 1.0, 2.0f, 3, 'a', std::string("Hello"));

	auto view = table.view<vtll::tl<double,float>, vtll::tl<int, char, std::string>>();

}



int main() {
	std::cout << std::thread::hardware_concurrency() << " Threads\n";
	functional_test();
	return 0;
}


