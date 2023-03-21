
#include <vector>
#include <optional>
#include "VLLT.h"


using it = int_type<uint32_t, struct P1, std::numeric_limits<uint32_t>::max() >;
void f( it t) {
	return;
}

int main() {
	f(it(10));
	it val = it(20);
	val = 10;
	val = it(20);


	using types = vtll::tl<int, double, float, std::atomic<bool>, char>;
	vllt::VlltStack<types> stack;

	stack.push_back(0, 0.3, 1.4f, true, 'A');
	stack.push_back(1, 0.4, 2.4f, true, 'B');

	auto i = stack.get<int>(0);

	auto tup1 = stack.get_tuple(0);
	std::get<0>(tup1) = 2;
	auto tup2 = stack.get_tuple(0);

	stack.swap(0,1);

	auto tup3 = stack.get_tuple(0);
	auto tup4 = stack.get_tuple(1);

	auto data = stack.pop_back();
	bool hv = data.has_value();

	vllt::VlltFIFOQueue<types> queue;

	queue.push_back(0, 0.3, 1.4f, true, 'C');
	queue.push_back(1, 0.4, 2.4f, true, 'D');

	auto fdata = queue.pop_front();
	

	return 0;
}
