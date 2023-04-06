
#include <vector>
#include <optional>
#include "VLLT.h"


int main() {

	using types = vtll::tl<size_t, double, float, std::atomic<bool>, char>;
	vllt::VlltStack<types> stack;

	using idx_stack_t = decltype(stack)::table_index_t;

	const size_t MAX = 1024*16*10;
	for (idx_stack_t i = idx_stack_t{ 0 }; i < MAX; ++i) {
		stack.push_back(static_cast<size_t>(i.value()), 2.0 * i, 3.0f * i, true, 'A');
	}

	auto tup = stack.get_tuple(idx_stack_t{0});

	for (idx_stack_t i = idx_stack_t{ 0 }; i < stack.size(); ++i) {
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

	//----------------------------------------------------------------------------


	vllt::VlltFIFOQueue<types> queue;

	queue.push_back(0ull, 0.3, 1.4f, true, 'C');
	queue.push_back(1ull, 0.4, 2.4f, true, 'D');

	auto fdata = queue.pop_front();
	fdata = queue.pop_front();
	fdata = queue.pop_front();
	bool hv1 = fdata.has_value();
	return 0;
}
