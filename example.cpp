
#include <vector>
#include <optional>
#include "VLLT.h"




int main() {

	using types = vtll::tl<int, double, float, bool, int>;
	vllt::VlltStack<types> table;

	table.push_back(0, 0.3, 1.4f, true, 3);
	table.push_back(1, 0.4, 2.4f, true, 4);

	vtll::to_tuple<types> data;

	table.pop_back(&data);

	vllt::VlltFIFOQueue<types> queue;

	queue.push_back(0, 0.3, 1.4f, true, 3);
	queue.push_back(1, 0.4, 2.4f, true, 4);

	table.pop_back(&data);

	return 0;
}
