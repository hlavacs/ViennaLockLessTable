
#include <vector>
#include <queue>
#include <stack>
#include <optional>
#include <thread>
#include <latch>
#include <numeric>
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

	using types = vtll::tl<uint32_t, size_t, double, float, bool, char>;

	const size_t MAX = 1024*16*10;

	{
		std::cout << "STACK\n";

		vllt::VlltStack<types> stack;

		for (size_t i = 0ul; i < MAX; ++i) {
			stack.push((uint32_t)i, i, 2.0 * i, 3.0f * i, true, 'A');
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
		auto data = stack.pop();
		while (data.has_value()) {
			--i;
			//assert( std::get<size_t>( data.value() ) == i);
			data = stack.pop();
		}

		for (size_t i = 0ul; i < MAX; ++i) {
			stack.push(static_cast<uint32_t>(i), i, 2.0 * i, 3.0f * i, true, 'A');
		}
		stack.clear();


		auto push = [&](size_t start, size_t max, size_t f = 1) {
			for (size_t i = start; i <= max; ++i) {
				stack.push((uint32_t)i, f, (double) f * i, f * 2.0f * i, true, 'A');
			}
		};

		auto pull = [&](size_t in, size_t out) {
			std::array<size_t, 10> counter{in,in,in,in,in,in,in,in,in,in};

			size_t v = in;
			for (size_t i = 1; i <= out; ++i) {
				auto v = stack.pop();
				if (v.has_value()) {
					auto value = v.value();
					assert(counter[std::get<1>(value)]-- == (size_t)std::get<0>(value));
				}
			}
		};

		auto pull2 = [&](size_t in, size_t out) {
			for (size_t i = 1; i <= out; ++i) {
				auto v = stack.pop();
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
				queue.push((uint32_t)i, f, (double)f * i, f * 2.0f * i, true, 'A');
			}
		};

		auto pull = [&](size_t start, size_t out) {
			std::array<size_t, 10> counter{start, start, start, start, start, start, start, start, start, start};

			size_t v = start;
			for (size_t i = 1; i <= out; ++i) {
				auto v = queue.pop();
				if (v.has_value()) {
					auto value = v.value();
					assert(counter[std::get<1>(value)]++ == (size_t)std::get<0>(value));
				}
			}
		};

		auto pull2 = [&](size_t n) {
			for (size_t i = 1; i <= n; ++i) {
				auto v = queue.pop();
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

		for (size_t i = 0; i < 50; ++i) {
			std::cout << "Loop " << i << " ";
			par();
		}
	}

	return;
}


std::mutex g_mutex;



void performance_queue() {

	struct comp {
		double m_div{1000.0};
		comp(){
			wait_for((rand() % 100) / m_div);
		};
		~comp() {
			wait_for((rand() % 100) / m_div);
		};
	};

	using types = vtll::tl<uint32_t, size_t, double, float, bool, char, comp >;

	{
		std::cout << "QUEUE\n";

		vllt::VlltFIFOQueue<types, 1 << 8, true, 32> queue;
		std::queue<vtll::to_tuple<types>> stdqueue;

		auto par = [&]( bool lck1 ) {
			auto& ref = stdqueue;

			std::vector<double> push_time(30, 0);
			std::vector<double> pull_time(30, 0);
			std::vector<size_t> push_num(30, 0);
			std::vector<size_t> pull_num(30, 0);

			auto push = [&](size_t id, size_t start, size_t max, size_t f, bool lck) {
				for (size_t i = start; i <= max; ++i) {
					auto T1 = std::chrono::high_resolution_clock::now();
					if (lck) {
						g_mutex.lock();
						ref.emplace(std::make_tuple((uint32_t)i, f, (double)f* i, f * 2.0f * i, true, 'A', comp{}));
						//ref.push((uint32_t)i, f, (double)f * i, f * 2.0f * i, true, 'A');
						g_mutex.unlock();
					}
					else {
						queue.push((uint32_t)i, f, (double)f* i, f * 2.0f * i, true, 'A', comp{});
					}

					push_time[id] += duration_cast<duration<double>>(high_resolution_clock::now() - T1).count();
					push_num[id]++;
					wait_for( (rand() % 100) / 1000.0);
				}
			};

			auto pull = [&](size_t id, size_t n, bool lck) {
				for (size_t i = 1; i <= n; ++i) {
					auto T1 = std::chrono::high_resolution_clock::now();
					if (lck) {
						g_mutex.lock();
						if (ref.size()) {
							vtll::to_tuple<types> v = ref.front();
							ref.pop();
						}
						g_mutex.unlock();
					}
					else {
						auto v = queue.pop();
					}
					pull_time[id] += duration_cast<duration<double>>(high_resolution_clock::now() - T1).count();
					pull_num[id]++;
					wait_for((rand() % 100) / 1000.0);
				}
			};

			size_t in = 10000, out = 10000;

			std::ptrdiff_t num = 5;
			std::cout << 1 << " ";
			{
				std::latch l{2*num};
				std::vector<std::jthread> threads;
				for (std::ptrdiff_t i = 1; i <= 2*num; ++i) {
					threads.emplace_back(std::move(std::jthread([&]() { l.arrive_and_wait(); push(i, 1, in, i, lck1); })));
				}
			}
			{
				std::latch l{2*num};
				std::vector<std::jthread> threads;
				for (std::ptrdiff_t i = 1; i <= 2*num; ++i) {
					threads.emplace_back(std::move(std::jthread([&]() { l.arrive_and_wait(); pull(i, in, lck1); })));
				}
			}
			{
				std::latch l{2*num};
				std::vector<std::jthread> threads;
				for (std::ptrdiff_t i = 1; i <= num; ++i) {
					threads.emplace_back(std::move(std::jthread([&]() { l.arrive_and_wait(); push(i, 1, in, i, lck1); })));
					threads.emplace_back(std::move(std::jthread([&]() { l.arrive_and_wait(); pull(i, in, lck1); })));
				}
			}

			return std::make_tuple(
				std::accumulate(push_time.begin(), push_time.end(), 0.0),
				std::accumulate(pull_time.begin(), pull_time.end(), 0.0),
				std::accumulate(push_time.begin(), push_time.end(), 0.0) + std::accumulate(pull_time.begin(), pull_time.end(), 0.0)
				);
		};

		double SLO1 = 0.0, SLO2 = 0.0, SLO3 = 0.0;
		double SLL1 = 0.0, SLL2 = 0.0, SLL3 = 0.0;
		for (size_t i = 1; i <= 20; ++i) {
			queue.clear();
			while (stdqueue.size()) stdqueue.pop();

			std::cout << "Loop " << i << " ";
			auto TLO = par(true);
			auto TLL = par(false);
			if (i >= 4) {
				double alpha = 0.0;
				SLO1 = alpha * SLO1 + (1.0 - alpha) * std::get<0>(TLO);
				SLO2 = alpha * SLO2 + (1.0 - alpha) * std::get<1>(TLO);
				SLO3 = alpha * SLO3 + (1.0 - alpha) * std::get<2>(TLO);
				std::cout << SLO1 << " " << SLO2 << " " << SLO3 << " ";

				SLL1 = alpha * SLL1 + (1.0 - alpha) * std::get<0>(TLL);
				SLL2 = alpha * SLL2 + (1.0 - alpha) * std::get<1>(TLL);
				SLL3 = alpha * SLL3 + (1.0 - alpha) * std::get<2>(TLL);
				std::cout << SLL1 << " " << SLL2 << " " << SLL3 << " ";
			}
			else {
				double j = 3.;
				SLO1 += std::get<0>(TLO) / j;
				SLO2 += std::get<1>(TLO) / j;
				SLO3 += std::get<2>(TLO) / j;

				SLL1 += std::get<0>(TLL) / j;
				SLL2 += std::get<1>(TLL) / j;
				SLL3 += std::get<2>(TLL) / j;
			}
			std::cout << 3 << "\n";
		}

	}

}


void performance_stack() {

	struct comp {
		double m_div{1000.0};
		comp() {
			wait_for((rand() % 100) / m_div);
		};
		~comp() {
			wait_for((rand() % 100) / m_div);
		};
	};

	using types = vtll::tl<uint32_t, size_t, double, float, bool, char, comp>;

	{
		std::cout << "STACK\n";

		vllt::VlltStack<types, 1 << 8, true, 32> stack;
		std::stack<vtll::to_tuple<types>> stdstack;

		auto par = [&](bool lck1) {
			auto& ref = stdstack;

			std::vector<double> push_time(30, 0);
			std::vector<double> pull_time(30, 0);
			std::vector<size_t> push_num(30, 0);
			std::vector<size_t> pull_num(30, 0);

			auto push = [&](size_t id, size_t start, size_t max, size_t f, bool lck) {
				for (size_t i = start; i <= max; ++i) {
					auto T1 = std::chrono::high_resolution_clock::now();
					if (lck) {
						g_mutex.lock();
						ref.emplace(std::make_tuple((uint32_t)i, f, (double)f* i, f * 2.0f * i, true, 'A', comp{}));
						//ref.push((uint32_t)i, f, (double)f * i, f * 2.0f * i, true, 'A');
						g_mutex.unlock();
					}
					else {
						stack.push((uint32_t)i, f, (double)f * i, f * 2.0f * i, true, 'A', comp{});
					}

					push_time[id] += duration_cast<duration<double>>(high_resolution_clock::now() - T1).count();
					push_num[id]++;
					wait_for((rand() % 100) / 1000.0);
				}
			};

			auto pull = [&](size_t id, size_t n, bool lck) {
				for (size_t i = 1; i <= n; ++i) {
					auto T1 = std::chrono::high_resolution_clock::now();
					if (lck) {
						g_mutex.lock();
						if (ref.size()) {
							vtll::to_tuple<types> v = ref.top();
							ref.pop();
						}
						g_mutex.unlock();
					}
					else {
						auto v = stack.pop();
					}
					pull_time[id] += duration_cast<duration<double>>(high_resolution_clock::now() - T1).count();
					pull_num[id]++;
					wait_for((rand() % 100) / 1000.0);
				}
			};

			size_t in = 10000, out = 10000;

			std::ptrdiff_t num = 5;
			std::cout << 1 << " ";
			{
				std::latch l{2*num};
				std::vector<std::jthread> threads;
				for (std::ptrdiff_t i = 1; i <= 2 * num; ++i) {
					threads.emplace_back(std::move(std::jthread([&]() { l.arrive_and_wait(); push(i, 1, in, i, lck1); })));
				}
			}
			{
				std::latch l{2 * num};
				std::vector<std::jthread> threads;
				for (std::ptrdiff_t i = 1; i <= 2 * num; ++i) {
					threads.emplace_back(std::move(std::jthread([&]() { l.arrive_and_wait(); pull(i, in, lck1); })));
				}
			}
			{
				std::latch l{2*num};
				std::vector<std::jthread> threads;
				for (std::ptrdiff_t i = 1; i <= num; ++i) {
					threads.emplace_back(std::move(std::jthread([&]() { l.arrive_and_wait(); push(i, 1, in, i, lck1); })));
					threads.emplace_back(std::move(std::jthread([&]() { l.arrive_and_wait(); pull(i, in, lck1); })));
				}
			}

			return std::make_tuple(
				std::accumulate(push_time.begin(), push_time.end(), 0.0),
				std::accumulate(pull_time.begin(), pull_time.end(), 0.0),
				std::accumulate(push_time.begin(), push_time.end(), 0.0) + std::accumulate(pull_time.begin(), pull_time.end(), 0.0)
			);
		};

		double SLO1 = 0.0, SLO2 = 0.0, SLO3 = 0.0;
		double SLL1 = 0.0, SLL2 = 0.0, SLL3 = 0.0;
		for (size_t i = 1; i <= 20; ++i) {
			stack.clear();
			while (stdstack.size()) stdstack.pop();

			std::cout << "Loop " << i << " ";
			auto TLO = par(true);
			auto TLL = par(false);
			if (i >= 4) {
				double alpha = 0.0;
				SLO1 = alpha * SLO1 + (1.0 - alpha) * std::get<0>(TLO);
				SLO2 = alpha * SLO2 + (1.0 - alpha) * std::get<1>(TLO);
				SLO3 = alpha * SLO3 + (1.0 - alpha) * std::get<2>(TLO);
				std::cout << SLO1 << " " << SLO2 << " " << SLO3 << " ";

				SLL1 = alpha * SLL1 + (1.0 - alpha) * std::get<0>(TLL);
				SLL2 = alpha * SLL2 + (1.0 - alpha) * std::get<1>(TLL);
				SLL3 = alpha * SLL3 + (1.0 - alpha) * std::get<2>(TLL);
				std::cout << SLL1 << " " << SLL2 << " " << SLL3 << " ";
			}
			else {
				double j = 3.;
				SLO1 += std::get<0>(TLO) / j;
				SLO2 += std::get<1>(TLO) / j;
				SLO3 += std::get<2>(TLO) / j;

				SLL1 += std::get<0>(TLL) / j;
				SLL2 += std::get<1>(TLL) / j;
				SLL3 += std::get<2>(TLL) / j;
			}
			std::cout << 3 << "\n";
		}

	}

}


int main() {
	std::cout << std::thread::hardware_concurrency() << " Threads\n";
	//functional_test();
	performance_stack();
	performance_queue();
}


