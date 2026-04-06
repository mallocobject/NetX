<div align="center">
  <pre>
███╗   ██╗███████╗████████╗██╗  ██╗
████╗  ██║██╔════╝╚══██╔══╝╚██╗██╔╝
██╔██╗ ██║█████╗     ██║    ╚███╔╝ 
██║╚██╗██║██╔══╝     ██║    ██╔██╗ 
██║ ╚████║███████╗   ██║   ██║  ██║
╚═╝  ╚═══╝╚══════╝   ╚═╝   ╚═╝  ╚═╝
  </pre>

  <p align="center">
    <img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="C++20">
    <img src="https://img.shields.io/badge/platform-Linux-red.svg" alt="Platform">
    <img src="https://img.shields.io/badge/build-CMake-blueviolet.svg" alt="CMake">
    <img src="https://img.shields.io/badge/status-Developing-green.svg" alt="Developing">
  </p>

  <p align="center"> <b>NETX - High Performance Network Framework</b></p>
</div>

---

##  HTTP Server Setup Example

```cpp
#include "netx/http/response.hpp"
#include "netx/http/server.hpp"
#include <utility>

using namespace netx::http;
using namespace netx::net;
using namespace netx::async;
using namespace std::chrono_literals;

int main()
{
	HttpServer::server()
		.listen("127.0.0.1", 8080)
		.route("GET", "/",
			   [](HttpRequest* req) -> Task<HttpResponse>
			   {
				   co_return std::move(HttpResponse{}
										   .status(200)
										   .content_type("text/html")
										   .body("<h1>Hello Netx</h1>"));
			   })
		.timeout(3s)
		.loop(8)
		.start();
}
```

### Settings

- `.listen("127.0.0.1", 8080)` binds the HTTP server to the local address and port.
- `.route("GET", "/", ...)` registers a handler for `GET /`.
- `.timeout(3s)` closes or times out idle connections after `3s`.
- `.loop(8)` starts `8` worker event loops for request processing.
- `.start()` launches the server.

## Logging

You can configure logging with environment variables before starting the executable.

```bash
ELOG_PATH=/absolute/log/dir ELOG_LEVEL=INFO ./build/test/test1
```

- `ELOG_PATH={dir}` sets the log output directory. The directory must already exist.
- `ELOG_LEVEL={TRACE, DEBUG, INFO, WARN, ERROR, FATAL}` filters terminal log output only.
- Async file logging is still written in full and is not affected by `ELOG_LEVEL`.