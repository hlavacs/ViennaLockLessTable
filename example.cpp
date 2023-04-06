
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

	stack.swap(idx_stack_t{ 0 }, idx_stack_t{ 1 });
	auto tup = stack.get_tuple(idx_stack_t{0});
	assert(std::get<0>(tup) == 1);
	stack.swap(idx_stack_t{ 0 }, idx_stack_t{ 1 });
	assert(std::get<0>(tup) == 0);

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

	stack.compress();

	for (idx_stack_t i = idx_stack_t{ 0 }; i < MAX; ++i) {
		stack.push_back(static_cast<size_t>(i.value()), 2.0 * i, 3.0f * i, true, 'A');
	}

	stack.clear();
	stack.compress();

	//----------------------------------------------------------------------------


	vllt::VlltFIFOQueue<types, 1 << 10,true,16,size_t> queue;
	using idx_queue_t = decltype(queue)::table_index_t;

	for (size_t i = 0; i < MAX; ++i) {
		queue.push_back(i, 2.0 * i, 3.0f * i, true, 'A');
	}

	auto v = queue.pop_front();
	size_t j = 0;
	while (v.has_value()) {
		assert(std::get<0>(v.value()) == j);
		j++;
		v = queue.pop_front();
	}

	for (size_t i = 0; i < MAX; ++i) {
		queue.push_back(10*i, 20.0 * i, 30.0f * i, true, 'A');
	}

	for (size_t j = 0; j < MAX / 2; ++j) {
		auto v = queue.pop_front();
		assert(std::get<0>(v.value()) == 10*j);
	}

	queue.clear();
	assert(queue.size() == 0);
	return 0;
}
