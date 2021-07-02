
#include <vector>
#include <optional>
#include "VLLT.h"




int main() {

	using types = vtll::tl<int, double, float, bool, int>;
	vllt::VlltTable<types> table;

	table.push_back(0, 0.3, 1.4f, true, 3);
	table.push_back(1, 0.4, 2.4f, true, 4);

	return 0;
}
