#include "netx/meta/intrusive_list.hpp"
#include <iostream>

using namespace netx::meta;

struct Test : public Intrusive_list<Test>::Node
{
	~Test()
	{
		std::cout << "~" << std::endl;
	}
};

int main()
{
	Test* t1 = new Test;
	Test* t2 = new Test;
	Test* t3 = new Test;

	Intrusive_list<Test> list;

	std::cout << list.size() << std::endl;

	list.insert(*t1);
	list.insert(*t2);
	list.insert(*t3);

	std::cout << list.size() << std::endl;

	// list.erase(*t2);

	// std::cout << list.size() << std::endl;

	delete t2;

	std::cout << list.size() << std::endl;
}