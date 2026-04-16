#include "kv.hpp"
#include <cassert>

int main()
{
	using AllEntries =
		TypeList<Entry<0, int>, Entry<1, char>, Entry<2, char>, Entry<3, short>,
				 Entry<4, char[10]>, Entry<5, char[10]>, Entry<6, int>>;

	Datatable<AllEntries> datatb;
	std::string_view expected_value = "hello";
	char value[10]{};
	assert(!datatb.get_data(4, value));
	assert(datatb.set_data(4, expected_value.data(), expected_value.length()));
	assert(datatb.get_data(4, value));
	assert(expected_value == value);
}