#include "pch.h"
#include "client.h"
#include <concurrent_queue.h>

struct session
{
	using t_send_queue = concurrency::concurrent_queue<std::tuple<std::function<std::tuple<void*, size_t>()>, std::function<void()>>>;

	std::string	 name;
	uint32		 c_id;
	bool		 connected = false;
	SOCKET		 sock;
	t_send_queue send_queue;
	char		 recv_buffer[1024];
	uint32		 seq_num = 0;

	std::thread send_thread;
	std::thread recv_thread;
	std::thread delay_thread;

	session(SOCKET sock) : sock(sock), c_id(-1)
	{
	}
};

namespace
{
	// constexpr auto server_addr = "fe80::dffa:bfeb:7029:918d";
	constexpr auto server_addr = "172.30.1.3";
	// constexpr auto								   server_addr = "2001:2d8:2120:8c19:5b69:cd74:bc6d:466e";

	auto sessions		  = std::vector<session> {};
	auto server_addr_info = sockaddr_in {};

	auto sending = true;
	auto recving = true;

	const auto client_name = std::string("JH_computer");

}	 // namespace

namespace
{
	void _send_loop(uint32 idx)
	{
		auto* p_session	 = &sessions[idx];
		auto  sock		 = p_session->sock;
		auto& send_queue = p_session->send_queue;
		auto  func_tpl	 = std::tuple<std::function<std::tuple<void*, size_t>()>, std::function<void()>>();
		while (sending)
		{
			if (send_queue.try_pop(func_tpl) is_false)
			{
				continue;
			}
			auto&& [packet_func, callback_func] = func_tpl;

			auto&& [p_packet, len] = packet_func();

			// p.time_client_send = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			if (sendto(sock, (char*)p_packet, len, 0, (sockaddr*)&server_addr_info, sizeof(sockaddr_in6)) == SOCKET_ERROR)
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

	void _recv_loop(uint32 idx)
	{
		auto* p_session	  = &sessions[idx];
		auto  sock		  = p_session->sock;
		auto& recv_buffer = p_session->recv_buffer;

		logger::info("{} recv_loop begin", idx);
		while (recving)
		{
			// auto recv_len = ::recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0, (sockaddr*)&addr, &len);
			auto recv_len = ::recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0, nullptr, nullptr);
			// assert(recv_len >= sizeof(uint16));

			// auto* p_recv = (packet*)recv_buffer;
			//  p_recv->time_client_recv = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			client::handle_packet(idx, recv_buffer, recv_len);

			// auto duration = std::chrono::nanoseconds(p_recv->time_client_recv - p_recv->time_server_send);
			// logger::info("[client] : seq num : {}, server->client duration : {}ns", p_recv->seq_num, duration.count());
		}
	}

	void _delay_loop(uint32 idx)
	{
		// todo
		auto* p_session = &sessions[idx];
		while (true)
		{
			p_session->send_queue.push({ [p_session]() {
											auto* p_packet = (packet_3*)malloc(sizeof(packet_3));
											assert(p_packet != nullptr);
											{
												p_packet->type			   = 3;
												p_packet->client_id		   = p_session->c_id;
												p_packet->seq_num		   = p_session->seq_num;
												p_packet->time_client_send = utils::time_now();
												p_packet->time_server_recv = 0;
												p_packet->time_server_send = 0;
												p_packet->time_client_recv = 0;
											}

											return std::tuple { (void*)p_packet, sizeof(packet_3) };
										},
										 [p_session]() {
											 logger::info("[{}]: sending packet_type {}, seq_num : {}", p_session->name, 3, p_session->seq_num);
											 ++p_session->seq_num;
										 } });
			Sleep(1000);
		}
	}
}	 // namespace

bool client::init()
{
	logger::init("client_log.txt");

	auto wsa_data = WSADATA {};
	ZeroMemory(&server_addr_info, sizeof(server_addr_info));

	if (::WSAStartup(MAKEWORD(2, 2), &wsa_data) != S_OK)
	{
		// std::print(std::format("{} {}", "WSA startup failed", "with error code {} : {}").c_str(), ::WSAGetLastError(), print_err(::WSAGetLastError()));
		err_msg("WSA startup failed");
		// std::print(, ::WSAGetLastError(), print_err(::WSAGetLastError()));
		goto failed;
	}

	server_addr_info.sin_family = AF_INET;
	server_addr_info.sin_port	= ::htons(PORT_SERVER);
	if (inet_pton(AF_INET, server_addr, &server_addr_info.sin_addr) != 1)
	{
		err_msg("inet_pton() failed");
		goto failed;
	}

	sessions =
		net_core::get_binded_socks(PORT_CLIENT, { IF_TYPE_ETHERNET_CSMACD, IF_TYPE_IEEE80211 })
		| std::views::transform([](auto sock) { return session(sock); })
		| std::ranges::to<std::vector>();

	if (sessions.size() == 0)
	{
		err_msg("no binded sockets");
		goto failed;
	}

	return true;
failed:
	::WSACleanup();
	return false;
}

void client::run()
{
	for (auto idx : std::views::iota(0ull, sessions.size()))
	{
		sessions[idx].send_queue.push({ [idx]() {
										   sessions[idx].name = std::format("{}_{}", client_name, idx);
										   auto& sock_name	  = sessions[idx].name;
										   char* p_packet	  = (char*)malloc(sizeof(uint16) + sizeof(uint16) + sizeof(char) * sock_name.size());
										   assert(p_packet != nullptr);
										   {
											   *(uint16*)p_packet					 = 0;
											   *(uint16*)(p_packet + sizeof(uint16)) = (uint16)sock_name.size();
											   memcpy(p_packet + sizeof(uint16) * 2, sock_name.c_str(), sock_name.size());
										   }
										   return std::tuple { (void*)p_packet, sizeof(uint16) * 2 + sizeof(char) * sock_name.size() };
									   },
										nullptr });

		sessions[idx].send_thread = std::thread(_send_loop, idx);
		sessions[idx].recv_thread = std::thread(_recv_loop, idx);
	}

	for (auto idx : std::views::iota(0ull, sessions.size()))
	{
		sessions[idx].send_thread.join();
		sessions[idx].recv_thread.join();
	}
}

void client::deinit()
{
	logger::clear();
	::WSACleanup();
}

void client::handle_packet(uint32 session_idx, void* p_mem, int32 recv_len)
{
	if (recv_len < sizeof(uint16))
	{
		logger::error("invalid packet, recv_len is {}", recv_len);
		return;
	}

	auto  packet_type = *(uint16*)p_mem;
	auto* p_session	  = &sessions[session_idx];

	switch (packet_type)
	{
	case 1:
	{
		if (recv_len != sizeof(packet_1))
		{
			logger::error("session idx : [{}] invalid packet, packet type : {} but recv_len is {}", session_idx, packet_type, recv_len);
		}

		auto* p_packet = (packet_1*)p_mem;

		if (p_packet->res != 0)
		{
			logger::error("connect failed, error code : {}", p_packet->res);
		}
		// logger::info("session idx [{}]: handing packet type {}, client_id {}", session_idx, packet_type, p_packet->client_id);

		p_session->c_id = p_packet->client_id;

		p_session->send_queue.push({ [id = p_session->c_id]() {
										auto* p_packet = (packet_2*)malloc(sizeof(packet_2));
										assert(p_packet != nullptr);
										{
											p_packet->type		= 2;
											p_packet->res		= 0;
											p_packet->client_id = id;
										}

										return std::tuple { (void*)p_packet, sizeof(packet_2) };
									},
									 nullptr });
		p_session->delay_thread = std::thread(_delay_loop, session_idx);
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
		logger::info("[{}] seq num : {}, delay : {}", p_session->name, p_packet->seq_num, delay);

		p_session->send_queue.push({ [id = p_session->c_id, delay, seq_num = p_packet->seq_num]() {
										auto* p_packet = (packet_6*)malloc(sizeof(packet_6));
										assert(p_packet != nullptr);
										{
											p_packet->type		= 6;
											p_packet->client_id = id;
											p_packet->seq_num	= seq_num;
											p_packet->delay		= delay;
										}

										return std::tuple { (void*)p_packet, sizeof(packet_6) };
									},
									 nullptr });
		break;
	}
	default:
		break;
	}
}
