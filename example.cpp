
#include <vector>
#include <optional>
#include "VLLT.h"


using it = vsty::strong_type_t<uint32_t, vsty::counter<> >;
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

	using table_index_t = vllt::VlltStack<types>::table_index_t;

	stack.push_back(0, 0.3, 1.4f, true, 'A');
	stack.push_back(1, 0.4, 2.4f, true, 'B');

	auto i = stack.get<int>(table_index_t{ 0 });

	auto tup1 = stack.get_tuple(table_index_t{ 0 });
	std::get<0>(tup1) = 2;
	auto tup2 = stack.get_tuple(table_index_t{ 0 });

	stack.swap(table_index_t{ 0 }, table_index_t{ 1 });

	auto tup3 = stack.get_tuple(table_index_t{ 0 });
	auto tup4 = stack.get_tuple(table_index_t{ 1 });

	auto data = stack.pop_back();
	bool hv = data.has_value();

	data = stack.pop_back();
	hv = data.has_value();

	data = stack.pop_back();
	hv = data.has_value();

	vllt::VlltFIFOQueue<types> queue;

	queue.push_back(0, 0.3, 1.4f, true, 'C');
	queue.push_back(1, 0.4, 2.4f, true, 'D');

	auto fdata = queue.pop_front();
	fdata = queue.pop_front();
	fdata = queue.pop_front();
	bool hv1 = fdata.has_value();

	return 0;
}
