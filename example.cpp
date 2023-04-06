
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


	using types = vtll::tl<size_t, double, float, std::atomic<bool>, char>;
	vllt::VlltStack<types> stack;

	using table_index_t = decltype(stack)::table_index_t;

	const size_t MAX = 1024*16;
	for (table_index_t i = table_index_t{ 0 }; i < MAX; ++i) {
		stack.push_back(static_cast<size_t>(i.value()), 2.0 * i, 3.0f * i, true, 'A');
	}

	stack.push_back(MAX, 2.0 * MAX, 3.0f * MAX, true, 'A');


	for (table_index_t i = table_index_t{ 0 }; i < stack.size(); ++i) {
		auto v = stack.get<size_t>(i);
		assert(v == i);
	}
	auto i = stack.size();
	auto data = stack.pop_back();
	while (data.has_value()) {
		--i;
		assert( std::get<size_t>( data.value() ) == i);
		data = stack.pop_back();
	}



	/*auto data = stack.pop_back();
	bool hv = data.has_value();


	stack.swap(table_index_t{ 0 }, table_index_t{ 1 });

	auto tup3 = stack.get_tuple(table_index_t{ 0 });
	auto tup4 = stack.get_tuple(table_index_t{ 1 });

	auto data = stack.pop_back();
	bool hv = data.has_value();

	data = stack.pop_back();
	hv = data.has_value();

	data = stack.pop_back();
	hv = data.has_value();
	*/

	/*
	vllt::VlltFIFOQueue<types> queue;

	queue.push_back(0ull, 0.3, 1.4f, true, 'C');
	queue.push_back(1ull, 0.4, 2.4f, true, 'D');

	auto fdata = queue.pop_front();
	fdata = queue.pop_front();
	fdata = queue.pop_front();
	bool hv1 = fdata.has_value();
	*/
	return 0;
}
