#include "netx/meta/ordered_map.hpp"
#include <iostream>
#include <string>

using namespace netx::meta;

int main()
{
	OrderedMap<std::string, int> om;

	om.try_emplace("43", 34);
	om.try_emplace("1", 1);
	om.try_emplace("qwe", 5);
	om.try_emplace("845", 45);

	for (auto& it : om)
	{
		std::cout << it << std::endl;
	}
}