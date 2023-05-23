
#include <vector>
#include <optional>
#include <thread>
#include "VLLT.h"


int main() {

	using types = vtll::tl<size_t, double, float, bool, char>;


	const size_t MAX = 1024*16*10;

	//vllt::VlltStack<types> stack;

	/*for (idx_stack_t i = idx_stack_t{0}; i < MAX; ++i) {
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
	*/

	//----------------------------------------------------------------------------


	vllt::VlltFIFOQueue<types, 1 << 3,true,16> queue;

	auto push = [&](size_t start, size_t max, size_t f = 1 ) {
		for (size_t i = start; i < max; ++i) {
			queue.push_back(f*i, f*2.0 * i, f*3.0f * i, true, 'A');
		}
	};

	auto pull = [&](size_t start = 0, int64_t n = -1ll, size_t f = 1) {
		if (n < 0) {
			auto v = queue.pop_front();
			size_t j = start;
			while (v.has_value()) {
				//assert(std::get<0>(v.value()) == f*j);
				j++;
				v = queue.pop_front();
			}
		} 
		else {
			for (size_t j = start; j < (size_t)n; ++j) {
				auto v = queue.pop_front();
				//assert(std::get<0>(v.value()) == f*j);
			}
		}
	};

	push(0, MAX);
	pull();

	push(0, MAX, 10);
	pull(0, MAX / 2, 10);

	queue.clear();
	pull();
	assert(queue.size() == 0);

	push(MAX/2, MAX, 100);
	pull(MAX/2, MAX, 100);
	assert(queue.size() == 0);
	queue.clear();

	auto pull2 = [&](size_t n) {
		for (size_t i = 1; i < n; ++i) {
			auto v = queue.pop_front();
			//do {
			//} while (!v.has_value());
		}
	};

	auto par = [&]() {
		size_t in = 5000, out = 5000;
		{
			std::cout << 1 << " ";
			//std::jthread thread1([&]() { push(0, in, 1); });
			//std::jthread thread2([&]() { push(0, in, 2); });
			//std::jthread thread3([&]() { push(0, in, 3); });
		}

		{
			std::cout << 2 << " ";

			std::jthread thread1([&]() { push(0, in, 1); });
			std::jthread t1([&]() { pull2(out); });

			std::jthread thread2([&]() { push(0, in, 2); });
			std::jthread t2([&]() { pull2(out); });

			std::jthread thread3([&]() { push(0, in, 3); });
			std::jthread t3([&]() { pull2(out); });

			std::jthread thread4([&]() { push(0, in, 1); });
			std::jthread t4([&]() { pull2(out); });

			std::jthread thread5([&]() { push(0, in, 2); });
			std::jthread t5([&]() { pull2(out); });

			std::jthread thread6([&]() { push(0, in, 3); });
			std::jthread t6([&]() { pull2(out); });

			std::jthread t7([&]() { pull2(out); });
			std::jthread t8([&]() { pull2(out); });

			std::jthread t9([&]() { pull2(out); });
		}
		//assert(queue.size() == 1 * in - 1 * out);

		std::cout << 3 << " ";
		queue.clear();
		std::cout << 4 << "\n";
	};

	for (size_t i = 0; i < 500; ++i) {
		std::cout << "Loop " << i << " ";
		par();
	}

	return 0;
}
