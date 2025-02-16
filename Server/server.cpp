#include "pch.h"
#include "server.h"
#include <concurrent_queue.h>
#include <concurrent_vector.h>

#define RECV_THREAD_COUNT	8
#define LISTEN_THREAD_COUNT 2

struct c_session
{
	std::array<char, 1024> recv_buf;
	WSABUF				   wsa_buf;
	uint32				   recv_len;
	uint32				   recv_flag;
	WSAOVERLAPPED		   wsa_overlapped;
	sockaddr_in6		   client_addr;

	c_session() : recv_len(0), recv_flag(0)
	{
		ZeroMemory(&wsa_buf, sizeof(wsa_buf));
		ZeroMemory(&wsa_overlapped, sizeof(wsa_overlapped));

		wsa_buf.buf = recv_buf.data();
		wsa_buf.len = recv_buf.size();
	}
};

namespace
{
	auto send_socket	  = SOCKET {};
	auto listen_socket	  = SOCKET {};
	auto client_addr_info = sockaddr_in6 {};
	auto server_addr_info = sockaddr_in6 {};

	char recv_buffer[1024];
	char host_name[NI_MAXHOST];
	auto send_msg = std::span("hi im server");

	auto send_thread = std::thread();
	auto recv_thread = std::thread();

	auto sending = true;
	auto recving = true;

	auto send_queue = concurrency::concurrent_queue<packet>();

	auto h_iocp = HANDLE {};

	auto recv_thread_arr   = std::array<std::thread, RECV_THREAD_COUNT> {};
	auto listen_thread_arr = std::array<std::thread, LISTEN_THREAD_COUNT> {};
	auto sessions		   = std::array<c_session, LISTEN_THREAD_COUNT> {};
	auto client_socket_vec = concurrency::concurrent_vector<SOCKET> {};
}	 // namespace

namespace
{
	void _send_loop()
	{
		auto seq_num	 = 0;
		auto send_packet = packet();
		while (sending)
		{
			if (send_queue.try_pop(send_packet) is_false)
			{
				continue;
			}

			send_packet.t_s_send = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			if (sendto(send_socket, (char*)&send_packet, sizeof(packet), 0, (sockaddr*)&client_addr_info, sizeof(sockaddr_in6)) == SOCKET_ERROR)
			{
				err_msg("sendto() failed");
			}
		}
	}

	void _recv_loop()
	{
		while (recving)
		{
			auto recv_len = ::recvfrom(listen_socket, recv_buffer, sizeof(recv_buffer), 0, nullptr, nullptr);
			assert(recv_len == sizeof(packet));

			auto* p_recv	 = (packet*)recv_buffer;
			p_recv->t_s_recv = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

			auto duration = std::chrono::nanoseconds(p_recv->t_s_recv - p_recv->t_c_send);
			logger::info("[server] : seq num : {}, client->server duration : {}ns", p_recv->seq_num, duration.count());

			send_queue.push(*p_recv);
		}
	}

	void _iocp_recv_loop()
	{
	}

	void _iocap_listen_loop()
	{
	}
}	 // namespace

bool server::init()
{
	logger::init("server_log.txt");

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

	h_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (h_iocp is_nullptr)
	{
		err_msg("iocp creation failed");
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

	if (::bind(listen_socket, (sockaddr*)&server_addr_info, sizeof(server_addr_info)) == SOCKET_ERROR)
	{
		err_msg("bind() failed");
		goto failed;
	}

	client_addr_info		   = server_addr_info;
	client_addr_info.sin6_port = ::htons(PORT_CLIENT);

	if (::connect(send_socket, (sockaddr*)&client_addr_info, sizeof(sockaddr_in6)) == SOCKET_ERROR)
	{
		err_msg("connect() failed");
		goto failed;
	}

	for (auto idx : std::views::iota(0, RECV_THREAD_COUNT))
	{
		recv_thread_arr[idx] = std::thread(_iocp_recv_loop);
	}

	for (auto idx : std::views::iota(0, LISTEN_THREAD_COUNT))
	{
		// listen_thread_arr[idx] = std::thread(_iocp_listen_loop);
		auto& session = sessions[idx];
		auto  res	  = ::WSARecvFrom(listen_socket, &session.wsa_buf, 1, (LPDWORD)&session.recv_len, (LPDWORD)&session.recv_flag, &session.wsa_overlapped, nullptr);
		if (res == SOCKET_ERROR)
		{
			auto err = ::WSAGetLastError();
			if (err != WSA_IO_PENDING)
			{
				err_msg("WSARecv failed");
				continue;
			}
		}
	}

	return true;
failed:
	::WSACleanup();
	::CloseHandle(h_iocp);
	return false;
}

void server::run()
{
	send_thread = std::thread(_send_loop);
	recv_thread = std::thread(_recv_loop);

	send_thread.join();
	recv_thread.join();

	// getchar();
}

void server::deinit()
{
	logger::clear();
	::WSACleanup();
}