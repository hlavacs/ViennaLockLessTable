
#include <vector>
#include <optional>
#include <thread>
#include "VLLT.h"


void functional_test() {

	using types = vtll::tl<uint32_t, size_t, double, float, bool, char>;

	const size_t MAX = 1024*16*10;

	{
		std::cout << "STACK\n";

		vllt::VlltStack<types> stack;

		for (size_t i = 0ul; i < MAX; ++i) {
			stack.push_back((uint32_t)i, i, 2.0 * i, 3.0f * i, true, 'A');
		}

		stack.swap(vllt::stack_index_t{0}, vllt::stack_index_t{1});
		auto d1 = stack.get<0>(vllt::stack_index_t{0});
		auto d2 = stack.get<double>(vllt::stack_index_t{ 0 });
		auto tup = stack.get_tuple(vllt::stack_index_t{0});

		assert(std::get<0>(tup.value()) == 1);
		stack.swap(vllt::stack_index_t{ 0 }, vllt::stack_index_t{ 1 });
		assert(std::get<0>(tup.value()) == 0);

		for (vllt::stack_index_t i = vllt::stack_index_t{ 0 }; i < stack.size(); ++i) {
			auto v = stack.get<size_t>(i).value();
			assert(v == i);
		}

		auto i = stack.size();
		auto data = stack.pop_back();
		while (data.has_value()) {
			--i;
			//assert( std::get<size_t>( data.value() ) == i);
			data = stack.pop_back();
		}

		for (size_t i = 0ul; i < MAX; ++i) {
			stack.push_back(static_cast<uint32_t>(i), i, 2.0 * i, 3.0f * i, true, 'A');
		}
		stack.clear();


		auto push = [&](size_t start, size_t max, size_t f = 1) {
			for (size_t i = start; i <= max; ++i) {
				stack.push_back((uint32_t)i, f, (double) f * i, f * 2.0f * i, true, 'A');
			}
		};

		auto pull = [&](size_t in, size_t out) {
			std::array<size_t, 10> counter{in,in,in,in,in,in,in,in,in,in};

			size_t v = in;
			for (size_t i = 1; i <= out; ++i) {
				auto v = stack.pop_back();
				if (v.has_value()) {
					auto value = v.value();
					assert(counter[std::get<1>(value)]-- == (size_t)std::get<0>(value));
				}
			}
		};

		auto pull2 = [&](size_t in, size_t out) {
			for (size_t i = 1; i <= out; ++i) {
				auto v = stack.pop_back();
				if (v.has_value()) {
					auto value = v.value();
					assert( std::get<3>(value) == 2*std::get<2>(value));
				}
			}
		};


		auto par = [&]() {
			size_t in = 1000, out = 1000;
			{
				std::cout << 1 << " ";
				std::jthread thread1([&]() { push(1, in, 1); });
				std::jthread thread2([&]() { push(1, in, 2); });
				std::jthread thread3([&]() { push(1, in, 3); });
				std::jthread thread4([&]() { push(1, in, 4); });
			}
			assert( stack.size() == 4*in);

			{
				std::jthread t1([&]() { pull(in, 4*in); });
			}
			assert(stack.size() == 0);

			{
				std::cout << 2 << " ";

				std::jthread thread1([&]() { push(0, in, 1); });
				std::jthread t1([&]() { pull2(in, out); });

				std::jthread thread2([&]() { push(0, in, 2); });
				std::jthread t2([&]() { pull2(in, out); });

				std::jthread thread3([&]() { push(0, in, 3); });
				std::jthread t3([&]() { pull2(in, out); });

				std::jthread thread4([&]() { push(0, in, 4); });
				std::jthread t4([&]() { pull2(in, out); });

				/*std::jthread thread5([&]() { push(0, in, 5); });
				std::jthread t5([&]() { pull2(in, out); });

				std::jthread thread6([&]() { push(0, in, 6); });
				std::jthread t6([&]() { pull2(in, out); });

				std::jthread thread7([&]() { push(0, in, 7); });
				std::jthread t7([&]() { pull2(in, out); });

				std::jthread t8([&]() { pull2(in, out); });
				std::jthread t9([&]() { pull2(in, out); });*/
			}
			//assert(queue.size() == 7 * in - 9 * out);

			std::cout << 3 << " ";
			stack.clear();
			std::cout << 4 << "\n";
		};

		for (size_t i = 0; i < 500; ++i) {
			std::cout << "Loop " << i << " ";
			par();
		}
	}


	//----------------------------------------------------------------------------

	{
		std::cout << "QUEUE\n";

		vllt::VlltFIFOQueue<types, 1 << 8, true, 16> queue;

		auto push = [&](size_t start, size_t max, size_t f = 1) {
			for (size_t i = start; i <= max; ++i) {
				queue.push_back((uint32_t)i, f, (double)f * i, f * 2.0f * i, true, 'A');
			}
		};

		auto pull = [&](size_t start, size_t out) {
			std::array<size_t, 10> counter{start, start, start, start, start, start, start, start, start, start};

			size_t v = start;
			for (size_t i = 1; i <= out; ++i) {
				auto v = queue.pop_front();
				if (v.has_value()) {
					auto value = v.value();
					assert(counter[std::get<1>(value)]++ == (size_t)std::get<0>(value));
				}
			}
		};

		auto pull2 = [&](size_t n) {
			for (size_t i = 1; i <= n; ++i) {
				auto v = queue.pop_front();
				if (v.has_value()) {
					auto value = v.value();
				}
			}
		};

		push(1, MAX);
		pull2(MAX);

		push(1, MAX, 10);
		pull2(MAX);

		queue.clear();
		pull2(MAX);
		assert(queue.size() == 0);

		push(1, MAX, 10);
		pull2(MAX / 2);
		pull2(MAX / 2);
		assert(queue.size() == 0);
		queue.clear();

		auto par = [&]() {
			size_t in = 15000, out = 15000;
			{
				std::cout << 1 << " ";
				std::jthread thread1([&]() { push(1, in, 1); });
				std::jthread thread2([&]() { push(1, in, 2); });
				std::jthread thread3([&]() { push(1, in, 3); });
				std::jthread thread4([&]() { push(1, in, 4); });
			}
			assert(queue.size() == 4 * in);

			{
				std::jthread t1([&]() { pull(1, 4 * in); });
			}
			assert(queue.size() == 0);

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

				/*std::jthread thread6([&]() { push(0, in, 3); });
				std::jthread t6([&]() { pull2(out); });

				std::jthread thread7([&]() { push(0, in, 3); });
				std::jthread t7([&]() { pull2(out); });

				std::jthread t8([&]() { pull2(out); });
				std::jthread t9([&]() { pull2(out); });*/
			}
			//assert(queue.size() == 7 * in - 9 * out);

			std::cout << 3 << " ";
			queue.clear();
			std::cout << 4 << "\n";
		};

		for (size_t i = 0; i < 500; ++i) {
			std::cout << "Loop " << i << " ";
			par();
		}
	}

	return;
}


std::mutex g_mutex;

using namespace std::chrono;

void performance_test() {
	using types = vtll::tl<uint32_t, size_t, double, float, bool, char>;

	{
		std::cout << "QUEUE\n";

		vllt::VlltFIFOQueue<types, 1 << 10, true, 16> queue;

		auto par = [&]( bool lck1 ) {
			auto push = [&](size_t start, size_t max, size_t f, bool lck) {
				for (size_t i = start; i <= max; ++i) {
					if (lck) g_mutex.lock();
					queue.push_back((uint32_t)i, f, (double)f * i, f * 2.0f * i, true, 'A');
					if (lck) g_mutex.unlock();
				}
			};

			auto pull = [&](size_t n, bool lck) {
				for (size_t i = 1; i <= n; ++i) {
					if (lck) g_mutex.lock();
					auto v = queue.pop_front();
					if (v.has_value()) {
						auto value = v.value();
					}
					if (lck) g_mutex.unlock();
				}
			};

			size_t in = 200000, out = 200000;

			std::cout << 1 << " ";
			auto T1 = std::chrono::high_resolution_clock::now();
			{
				std::jthread thread1([&]() { push(1, in, 1, lck1); });
				std::jthread thread2([&]() { push(1, in, 2, lck1); });
				std::jthread thread3([&]() { push(1, in, 3, lck1); });
				std::jthread thread4([&]() { push(1, in, 4, lck1); });
				std::jthread thread5([&]() { push(1, in, 5, lck1); });
				std::jthread thread6([&]() { push(1, in, 6, lck1); });
			}
			auto T2 = std::chrono::high_resolution_clock::now();
			{
				std::jthread t1([&]() { pull(in, lck1); });
				std::jthread t2([&]() { pull(in, lck1); });
				std::jthread t3([&]() { pull(in, lck1); });
				std::jthread t4([&]() { pull(in, lck1); });
				std::jthread t5([&]() { pull(in, lck1); });
				std::jthread t6([&]() { pull(in, lck1); });
			}
			auto T3 = high_resolution_clock::now();
			{
				std::jthread thread1([&]() { push(1, in, 1, lck1); });
				std::jthread thread2([&]() { push(1, in, 2, lck1); });
				std::jthread t1([&]() { pull(in, lck1); });
				std::jthread t2([&]() { pull(in, lck1); });
				std::jthread thread3([&]() { push(1, in, 3, lck1); });
				std::jthread t3([&]() { pull(in, lck1); });
				std::jthread thread4([&]() { push(1, in, 4, lck1); });
				std::jthread t4([&]() { pull(in, lck1); });
				std::jthread thread5([&]() { push(1, in, 5, lck1); });
				std::jthread t5([&]() { pull(in, lck1); });
				//std::jthread thread6([&]() { push(1, in, 6, lck1); });
				//std::jthread t6([&]() { pull(in, lck1); });
			}
			auto T4 = std::chrono::high_resolution_clock::now();

			auto dt1 = duration_cast<duration<double>>(T2 - T1).count();
			auto dt2 = duration_cast<duration<double>>(T3 - T2).count();
			auto dt3 = duration_cast<duration<double>>(T4 - T3).count();

			//std::cout << dt1 << " " << dt2 << " " << dt3 << " ";

			return std::make_tuple(dt1, dt2, dt3);
		};

		double SLO1 = 0.0, SLO2 = 0.0, SLO3 = 0.0;
		double SLL1 = 0.0, SLL2 = 0.0, SLL3 = 0.0;
		for (size_t i = 1; i <= 100; ++i) {
			std::cout << "Loop " << i << " ";
			auto TLO = par(true);
			if (i >= 4) {
				auto j = i - 3 ;
				SLO1 += std::get<0>(TLO);
				SLO2 += std::get<1>(TLO);
				SLO3 += std::get<2>(TLO);
				std::cout << SLO1 / j << " " << SLO2 / j << " " << SLO3 / j << " ";

				auto TLL = par(false);
				SLL1 += std::get<0>(TLL);
				SLL2 += std::get<1>(TLL);
				SLL3 += std::get<2>(TLL);
				std::cout << SLL1 / j << " " << SLL2 / j << " " << SLL3 / j << " ";
			}
			std::cout << 3 << "\n";
		}

	}

}


int main() {
	std::cout << std::thread::hardware_concurrency() << " Threads\n";
	//functional_test();
	performance_test();
}


