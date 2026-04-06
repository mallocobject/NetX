#include "netx/json/deserialize.hpp"
#include "netx/json/object.hpp"
#include "netx/json/serialize.hpp"
#include <iostream>
#include <string>
#include <string_view>

using namespace netx::json;

struct Point
{
	int x, y;
};
REFLECT_TYPE(Point, x, y)

struct Scene
{
	std::string name;
	std::vector<Point> points;
};
REFLECT_TYPE(Scene, name, points)

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
		std::cout << res.first.to_styled_string() << std::endl;
	}

	Scene s = {"Test", {{1, 2}, {3, 4}}};
	auto res2 = serialize(s);
	std::cout << res2 << std::endl;
}
