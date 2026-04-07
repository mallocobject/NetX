#include "elog/logger.hpp"
#include "netx/async/sleep.hpp"
#include "netx/async/when_any.hpp"
#include "netx/http/response.hpp"
#include "netx/http/server.hpp"
#include "netx/ws/frame.hpp"
#include <string>
#include <utility>

std::string html_text = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NetX WebSocket 调试器</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f4f7f6; margin: 0; padding: 20px; display: flex; flex-direction: column; height: 95vh; }
        .container { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); display: flex; flex-direction: column; flex-grow: 1; }
        .controls { display: flex; gap: 10px; margin-bottom: 20px; flex-wrap: wrap; }
        input[type="text"] { flex-grow: 1; padding: 10px; border: 1px solid #ddd; border-radius: 4px; outline: none; }
        button { padding: 10px 20px; cursor: pointer; border: none; border-radius: 4px; transition: background 0.3s; color: white; font-weight: bold; }
        .btn-connect { background-color: #28a745; }
        .btn-connect:hover { background-color: #218838; }
        .btn-disconnect { background-color: #dc3545; }
        .btn-disconnect:hover { background-color: #c82333; }
        .btn-send { background-color: #007bff; }
        .btn-send:hover { background-color: #0069d9; }
        button:disabled { background-color: #ccc; cursor: not-allowed; }
        #log { flex-grow: 1; border: 1px solid #eee; background: #fafafa; padding: 15px; overflow-y: auto; border-radius: 4px; font-family: 'Courier New', Courier, monospace; font-size: 14px; margin-bottom: 20px; }
        .msg { margin-bottom: 8px; border-bottom: 1px solid #f0f0f0; padding-bottom: 4px; }
        .msg.sent { color: #0056b3; }
        .msg.received { color: #218838; }
        .msg.system { color: #666; font-style: italic; }
        .status { font-weight: bold; margin-bottom: 10px; }
        .status.online { color: #28a745; }
        .status.offline { color: #dc3545; }
    </style>
</head>
<body>

<div class="container">
    <h2>NetX WebSocket Client</h2>
    
    <div class="status offline" id="statusLabel">状态: 未连接</div>

    <div class="controls">
        <input type="text" id="urlInput" value="ws://127.0.0.1:8080/chat/room1" placeholder="ws://127.0.0.1:8080/path">
        <button class="btn-connect" id="connectBtn">连接</button>
        <button class="btn-disconnect" id="disconnectBtn" disabled>断开</button>
    </div>

    <div id="log"></div>

    <div class="controls">
        <input type="text" id="messageInput" placeholder="输入消息..." disabled>
        <button class="btn-send" id="sendBtn" disabled>发送消息</button>
    </div>
</div>

<script>
    let socket = null;
    const urlInput = document.getElementById('urlInput');
    const connectBtn = document.getElementById('connectBtn');
    const disconnectBtn = document.getElementById('disconnectBtn');
    const messageInput = document.getElementById('messageInput');
    const sendBtn = document.getElementById('sendBtn');
    const log = document.getElementById('log');
    const statusLabel = document.getElementById('statusLabel');

    function appendLog(type, message) {
        const div = document.createElement('div');
        div.className = `msg ${type}`;
        const time = new Date().toLocaleTimeString();
        div.innerText = `[${time}] ${type === 'sent' ? '发送 ->' : type === 'received' ? '接收 <-' : '系统:'} ${message}`;
        log.appendChild(div);
        log.scrollTop = log.scrollHeight;
    }

    function updateUI(connected) {
        connectBtn.disabled = connected;
        disconnectBtn.disabled = !connected;
        messageInput.disabled = !connected;
        sendBtn.disabled = !connected;
        
        if (connected) {
            statusLabel.innerText = "状态: 已连接";
            statusLabel.className = "status online";
        } else {
            statusLabel.innerText = "状态: 未连接";
            statusLabel.className = "status offline";
        }
    }

    connectBtn.onclick = () => {
        const url = urlInput.value;
        try {
            socket = new WebSocket(url);
            
            socket.onopen = () => {
                appendLog('system', `成功连接至 ${url}`);
                updateUI(true);
            };

            socket.onmessage = (event) => {
                appendLog('received', event.data);
            };

            socket.onclose = (event) => {
                appendLog('system', `连接已关闭 (代码: ${event.code})`);
                updateUI(false);
                socket = null;
            };

            socket.onerror = (error) => {
                appendLog('system', `发生错误: 无法连接或连接异常`);
                console.error(error);
            };

        } catch (e) {
            appendLog('system', `错误: ${e.message}`);
        }
    };

    disconnectBtn.onclick = () => {
        if (socket) {
            socket.close();
        }
    };

    function sendMessage() {
        const msg = messageInput.value;
        if (socket && socket.readyState === WebSocket.OPEN && msg) {
            socket.send(msg);
            appendLog('sent', msg);
            messageInput.value = '';
        }
    }

    sendBtn.onclick = sendMessage;
    messageInput.onkeypress = (e) => { if (e.key === 'Enter') sendMessage(); };

</script>
</body>
</html>)HTML";

using namespace netx::http;
using namespace netx::net;
using namespace netx::async;
using namespace netx::ws;
using namespace elog;
using namespace std::chrono_literals;

int main()
{
	HttpServer::server()
		.listen("127.0.0.1", 8080)
		.route("GET", "/",
			   [](HttpRequest* req) -> Task<HttpResponse>
			   {
				   co_return std::move(
					   HttpResponse{}
						   .status(200)
						   .content_type("text/html; charset=utf-8")
						   .body(html_text));
			   })
		.ws("/chat/:id",
			[](WSConnection* ws) -> Task<>
			{
				bool waiting_pong = false;
				bool expecting_continuation = false;
				WSOpcode currrent_opcode;
				std::string current_message;

				while (true)
				{
					auto var = co_await when_any(ws->receive(), sleep(10s));
					if (var.index() == 1)
					{
						if (std::exchange(waiting_pong, true))
						{
							co_await ws->send_close("");
							break;
						}
						co_await ws->send_ping("");
						continue;
					}

					auto frame = std::move(std::get<0>(var));
					if (!frame)
					{
						break;
					}

					if (frame->opcode == WSOpcode::kClose)
					{
						LOG_INFO(
							"WS received Close Frame. Sending response...");
						co_await ws->send_close("");
						break;
					}
					else if (frame->opcode == WSOpcode::kPong)
					{
						LOG_INFO("WS received Pong Frame");
						waiting_pong = false;
					}
					else if (frame->opcode == WSOpcode::kText ||
							 frame->opcode == WSOpcode::kBinary)
					{
						currrent_opcode = frame->opcode;
						if (frame->fin)
						{
							if (frame->opcode == WSOpcode::kText)
							{
								co_await ws->send_text("Echo: " +
													   frame->payload);
							}
							else if (frame->opcode == WSOpcode::kBinary)
							{
								co_await ws->send_binary(frame->payload);
							}
						}
						else
						{
							current_message += frame->payload;
						}
					}
					else if (frame->opcode == WSOpcode::kContinuation)
					{
						if (frame->fin)
						{
							if (currrent_opcode == WSOpcode::kText)
							{
								co_await ws->send_text("Echo: " +
													   current_message);
							}
							else if (currrent_opcode == WSOpcode::kBinary)
							{
								co_await ws->send_binary(current_message);
							}
						}
						else
						{
							current_message += frame->payload;
						}
					}
				}
			})
		.loop(8)
		.start();
}