#include "pch.h"
#include "client.h"

namespace
{
	auto send_socket	  = SOCKET {};
	auto listen_socket	  = SOCKET {};
	auto server_addr_info = sockaddr_in6 {};
	auto client_addr_info = sockaddr_in6 {};

	char recv_buffer[1024];
	char host_name[NI_MAXHOST];
	auto send_msg = std::span("hi im client");

	auto send_thread = std::thread();
	auto recv_thread = std::thread();

	auto sending = true;
	auto recving = true;
}	 // namespace

namespace
{
	void _send_loop()
	{
		auto seq_num = 0;
		auto p		 = packet();
		while (sending)
		{
			p.seq_num = seq_num;
			++seq_num;

			Sleep(1000);	// 이것때문에 차이가 나는것 같음

			logger::info("sending seq {}", p.seq_num);
			p.time_client_send = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			if (sendto(send_socket, (char*)&p, sizeof(packet), 0, (sockaddr*)&server_addr_info, sizeof(sockaddr_in6)) == SOCKET_ERROR)
			{
				err_msg("sendto() failed");
			}
			else
			{
				// logger::info("send_to successed");
			}
		}
	}

	void _recv_loop()
	{
		while (recving)
		{
			auto recv_len = ::recvfrom(listen_socket, recv_buffer, sizeof(recv_buffer), 0, nullptr, nullptr);
			assert(recv_len == sizeof(packet));

			auto* p_recv			 = (packet*)recv_buffer;
			p_recv->time_client_recv = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

			auto duration = std::chrono::nanoseconds(p_recv->time_client_recv - p_recv->time_server_send);
			logger::info("[client] : seq num : {}, server->client duration : {}ns", p_recv->seq_num, duration.count());
		}
	}
}	 // namespace

bool client::init()
{
	logger::init("client_log.txt");

	auto wsa_data = WSADATA {};

	ZeroMemory(&server_addr_info, sizeof(server_addr_info));
	ZeroMemory(recv_buffer, sizeof(recv_buffer));
	ZeroMemory(host_name, sizeof(host_name));

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
	if (inet_pton(AF_INET6, "fe80::dffa:bfeb:7029:918d", &server_addr_info.sin6_addr) != 1)
	{
		err_msg("inet_pton() failed");
		goto failed;
	}

	client_addr_info		   = server_addr_info;
	client_addr_info.sin6_port = ::htons(PORT_CLIENT);

	if (::bind(listen_socket, (sockaddr*)&client_addr_info, sizeof(client_addr_info)) == SOCKET_ERROR)
	{
		err_msg("bind() failed");
		goto failed;
	}

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