#include "pch.h"
#include "client.h"
#include <concurrent_queue.h>

namespace
{
	constexpr auto server_addr = "2001:2d8:2029:f0cd:d3ca:dc7e:ed66:8e31";

	auto send_socket	  = SOCKET {};
	auto listen_socket	  = SOCKET {};
	auto server_addr_info = sockaddr_in6 {};

	char recv_buffer[1024];
	auto send_msg = std::span("hi im client");

	auto send_thread  = std::thread();
	auto recv_thread  = std::thread();
	auto delay_thread = std::thread();

	auto sending = true;
	auto recving = true;

	auto id = (uint32)0;

	auto client_name = std::string("JH_computer");

	auto seq_num = (uint32)0;
}	 // namespace

namespace
{
	auto send_queue = concurrency::concurrent_queue<std::tuple<std::function<std::tuple<void*, size_t>()>, std::function<void()>>>();

	void _send_loop()
	{
		auto func_tpl = std::tuple<std::function<std::tuple<void*, size_t>()>, std::function<void()>>();
		while (sending)
		{
			if (send_queue.try_pop(func_tpl) is_false)
			{
				continue;
			}
			auto&& [packet_func, callback_func] = func_tpl;

			auto&& [p_packet, len] = packet_func();
			logger::info("sending packet {}", *(uint16*)p_packet);
			// p.time_client_send = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			if (sendto(send_socket, (char*)p_packet, len, 0, (sockaddr*)&server_addr_info, sizeof(sockaddr_in6)) == SOCKET_ERROR)
			{
				err_msg("sendto() failed");
			}
			else
			{
				// logger::info("send_to successed");
			}

			if (callback_func)
			{
				callback_func();
			}
			// todo
			free(p_packet);
		}
	}

	void _recv_loop()
	{
		while (recving)
		{
			auto recv_len = ::recvfrom(listen_socket, recv_buffer, sizeof(recv_buffer), 0, nullptr, nullptr);
			// assert(recv_len >= sizeof(uint16));

			// auto* p_recv = (packet*)recv_buffer;
			//  p_recv->time_client_recv = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			client::handle_packet(recv_buffer, recv_len);

			// auto duration = std::chrono::nanoseconds(p_recv->time_client_recv - p_recv->time_server_send);
			// logger::info("[client] : seq num : {}, server->client duration : {}ns", p_recv->seq_num, duration.count());
		}
	}

	void _delay_loop()
	{
		// todo
		while (true)
		{
			send_queue.push({ []() {
								 auto* p_packet = (packet_3*)malloc(sizeof(packet_3));
								 assert(p_packet != nullptr);
								 {
									 p_packet->type				= 3;
									 p_packet->client_id		= id;
									 p_packet->seq_num			= seq_num;
									 p_packet->time_client_send = utils::time_now();
									 p_packet->time_server_recv = 0;
									 p_packet->time_server_send = 0;
									 p_packet->time_client_recv = 0;
								 }

								 return std::tuple { (void*)p_packet, sizeof(packet_3) };
							 },
							  []() {
								  logger::info("client: sending packet_type {}, seq_num : {}", 3, seq_num);
								  ++seq_num;
							  } });
			Sleep(1000);
		}
	}
}	 // namespace

bool client::init()
{
	logger::init("client_log.txt");

	auto wsa_data		  = WSADATA {};
	auto client_addr_info = sockaddr_in6 {};

	ZeroMemory(&server_addr_info, sizeof(server_addr_info));
	ZeroMemory(recv_buffer, sizeof(recv_buffer));

	if (::WSAStartup(MAKEWORD(2, 2), &wsa_data) != S_OK)
	{
		// std::print(std::format("{} {}", "WSA startup failed", "with error code {} : {}").c_str(), ::WSAGetLastError(), print_err(::WSAGetLastError()));
		err_msg("WSA startup failed");
		// std::print(, ::WSAGetLastError(), print_err(::WSAGetLastError()));
		goto failed;
	}

	send_socket	  = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	listen_socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

	if (send_socket == INVALID_SOCKET or listen_socket == INVALID_SOCKET)
	{
		err_msg("socket creation failed");
		goto failed;
	}

	server_addr_info.sin6_family = AF_INET6;
	server_addr_info.sin6_port	 = ::htons(PORT_SERVER);
	if (inet_pton(AF_INET6, server_addr, &server_addr_info.sin6_addr) != 1)
	{
		err_msg("inet_pton() failed");
		goto failed;
	}

	if (net_core::bind(listen_socket, &client_addr_info, PORT_CLIENT) is_false)
	{
		err_msg("bind() failed");
		goto failed;
	}

	send_queue.push({ []() {
						 char* p_packet = (char*)malloc(sizeof(uint16) + sizeof(uint16) + sizeof(char) * client_name.size());
						 assert(p_packet != nullptr);
						 {
							 *(uint16*)p_packet					   = 0;
							 *(uint16*)(p_packet + sizeof(uint16)) = (uint16)client_name.size();
							 memcpy(p_packet + sizeof(uint16) * 2, client_name.c_str(), client_name.size());
						 }
						 return std::tuple { (void*)p_packet, sizeof(uint16) * 2 + sizeof(char) * client_name.size() };
					 },
					  nullptr });
	// if (::connect(send_socket, (sockaddr*)&server_addr_info, sizeof(sockaddr_in6)) == SOCKET_ERROR)
	//{
	//	err_msg("connect() failed");
	//	goto failed;
	// }

	return true;
failed:
	::WSACleanup();
	return false;
}

void client::run()
{
	send_thread = std::thread(_send_loop);
	recv_thread = std::thread(_recv_loop);

	send_thread.join();
	recv_thread.join();
}

void client::deinit()
{
	logger::clear();
	::WSACleanup();
}

void client::handle_packet(void* p_mem, int32 recv_len)
{
	if (recv_len < sizeof(uint16))
	{
		logger::error("invalid packet, recv_len is {}", recv_len);
		return;
	}

	auto packet_type = *(uint16*)p_mem;

	switch (packet_type)
	{
	case 1:
	{
		if (recv_len != sizeof(packet_1))
		{
			logger::error("invalid packet, packet type : {} but recv_len is {}", packet_type, recv_len);
		}

		auto* p_packet = (packet_1*)p_mem;

		if (p_packet->res != 0)
		{
			logger::error("connect failed, error code : {}", p_packet->res);
		}

		id = p_packet->client_id;

		delay_thread = std::thread(_delay_loop);
		break;
	}
	case 3:
	{
		if (recv_len != sizeof(packet_3))
		{
			logger::error("invalid packet, packet type : {} but recv_len is {}", packet_type, recv_len);
		}

		auto* p_packet = (packet_3*)p_mem;

		p_packet->time_client_recv = utils::time_now();

		auto delay = (p_packet->time_client_recv - p_packet->time_client_send) - (p_packet->time_server_send - p_packet->time_server_send);
		logger::info("seq num : {}, delay : {}", p_packet->seq_num, delay);

		send_queue.push([delay]() {
			auto* p_packet = (packet_6*)malloc(sizeof(packet_6));
			assert(p_packet != nullptr);
			{
				p_packet->type		= 3;
				p_packet->client_id = id;
				p_packet->seq_num	= seq_num;
				p_packet->delay		= delay;
			}

			return std::tuple { (void*)p_packet, sizeof(packet_6) };
		},
						nullptr);
		break;
	}
	default:
		break;
	}
}
