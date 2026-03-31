#include "netx/json/deserialize.hpp"
#include <iostream>
#include <string_view>

using namespace netx::json;

int main()
{
	std::string_view json_str = R"JSON({
        "project": "netx",
        "version": 1.0,
        "active": true,
        "stats": {
            "downloads": 1024,
            "rating": 4.8
        },
        "contributors": ["Alice", "Bob", "Charlie"],
        "config": {
            "debug": false,
            "timeout": 3000,
            "metadata": null
        }
    })JSON";

	auto res = parse(json_str);
	if (res.second > 0)
	{
		dump_json(res.first);
	}

	std::cout << std::endl;
}
