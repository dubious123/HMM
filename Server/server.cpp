#include "pch.h"
#include "server.h"
#include <concurrent_queue.h>
#include <concurrent_vector.h>

#define RECV_THREAD_COUNT 1

struct c_session
{
	std::array<char, 1024> recv_buf;
	WSABUF				   wsa_buf;
	uint32				   recv_len;
	uint32				   recv_flag;
	WSAOVERLAPPED		   wsa_overlapped;
	sockaddr_in6		   client_send_addr;
	int32				   client_addr_size;

	c_session() : recv_len(0), recv_flag(0), client_addr_size(sizeof(client_send_addr))
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
	auto server_addr_info = sockaddr_in6 {};

	char host_name[NI_MAXHOST];
	auto send_msg = std::span("hi im server");

	auto send_thread = std::thread();

	auto sending = true;

	auto send_queue = concurrency::concurrent_queue<std::tuple<c_session*, packet>>();

	auto h_iocp = HANDLE {};

	auto recv_thread_arr = std::array<std::thread, RECV_THREAD_COUNT> {};
	// auto listen_thread_arr = std::array<std::thread, LISTEN_THREAD_COUNT> {};
	auto sessions		   = std::array<c_session, RECV_THREAD_COUNT> {};
	auto client_socket_vec = concurrency::concurrent_vector<SOCKET> {};
}	 // namespace

namespace
{
	void _send_loop()
	{
		auto seq_num = 0;
		auto tpl	 = std::tuple<c_session*, packet>();
		while (sending)
		{
			if (send_queue.try_pop(tpl) is_false)
			{
				continue;
			}

			auto&& [p_session, send_packet] = tpl;

			auto client_recv_addr	   = p_session->client_send_addr;
			client_recv_addr.sin6_port = ::htons(PORT_CLIENT);


			send_packet.time_server_send = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			logger::info("server : sending {}", send_packet.seq_num);
			if (::sendto(send_socket, (char*)&send_packet, sizeof(packet), 0, (sockaddr*)&client_recv_addr, sizeof(sockaddr_in6)) == SOCKET_ERROR)
			{
				err_msg("sendto() failed");
			}
			else
			{
				// logger::info("sendto successed");
			}
		}
	}

	void _iocp_recv_loop()
	{
		while (true)
		{
			auto  recv_len	= 0;
			auto* p_session = (c_session*)nullptr;
			auto* p_wol		= (WSAOVERLAPPED*)nullptr;
			auto  res		= ::GetQueuedCompletionStatus(h_iocp, (LPDWORD)&recv_len, (PULONG_PTR)&p_session, &p_wol, INFINITE);

			if (res is_false)
			{
				if (p_wol is_nullptr)
				{
					continue;	 //?
				}

				err_msg("GetQueuedCompletionStatus failed");
				// free(p_wol);
				continue;
			}

			logger::info("================");
			logger::info("recv_len : {}", recv_len);
			logger::info("p_session : {}", (uint64)p_session);


			auto* p_recv			 = (packet*)p_session->recv_buf.data();
			p_recv->time_server_recv = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			auto duration			 = std::chrono::nanoseconds(p_recv->time_server_recv - p_recv->time_client_send);
			logger::info("[server] : seq num : {}, client->server duration : {}ns, thread_id : {}, key : {}", p_recv->seq_num, duration.count(), std::this_thread::get_id()._Get_underlying_id(), (uint64)p_session);
			logger::info("p_recv->time_server_recv : {}ns, p_recv->time_client_send : {}ns", p_recv->time_server_recv, p_recv->time_client_send);

			send_queue.push({ p_session, *p_recv });

			memset(p_session->recv_buf.data(), 0, p_session->recv_buf.size());
			res = ::WSARecvFrom(listen_socket,
								&p_session->wsa_buf,
								1,
								/*(LPDWORD)&p_session->recv_len*/ nullptr,
								(LPDWORD)&p_session->recv_flag,
								(sockaddr*)&p_session->client_send_addr,
								&p_session->client_addr_size,
								&p_session->wsa_overlapped,
								nullptr);
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
	}
}	 // namespace

bool server::init()
{
	logger::init("server_log.txt");

	auto wsa_data = WSADATA {};

	ZeroMemory(&server_addr_info, sizeof(server_addr_info));
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

	h_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (h_iocp is_nullptr)
	{
		err_msg("iocp creation failed");
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

	// auto client_addr_info	   = server_addr_info;
	// client_addr_info.sin6_port = ::htons(PORT_CLIENT);

	// if (::connect(send_socket, (sockaddr*)&client_addr_info, sizeof(sockaddr_in6)) == SOCKET_ERROR)
	//{
	//	err_msg("connect() failed");
	//	goto failed;
	// }

	for (auto idx : std::views::iota(0, RECV_THREAD_COUNT))
	{
		::CreateIoCompletionPort((HANDLE)listen_socket, h_iocp, (ULONG_PTR)&sessions[idx], 0);
		recv_thread_arr[idx] = std::thread(_iocp_recv_loop);

		auto res = ::WSARecvFrom(listen_socket,
								 &sessions[idx].wsa_buf,
								 1,
								 /*(LPDWORD)&sessions[idx].recv_len*/ nullptr,
								 (LPDWORD)&sessions[idx].recv_flag,
								 (sockaddr*)&sessions[idx].client_send_addr,
								 &sessions[idx].client_addr_size,
								 &sessions[idx].wsa_overlapped,
								 nullptr);

		if (res == SOCKET_ERROR)
		{
			auto err = ::WSAGetLastError();
			if (err != WSA_IO_PENDING)
			{
				err_msg("wsarecv failed");
				continue;
			}
			else
			{
				// todo
			}
		}
	}

	// for (auto idx : std::views::iota(0, LISTEN_THREAD_COUNT))
	//{
	//	// listen_thread_arr[idx] = std::thread(_iocp_listen_loop);
	//	auto& session = sessions[idx];

	//	::CreateIoCompletionPort((HANDLE)listen_socket, h_iocp, (ULONG_PTR)&session, 0);
	//	auto res = ::WSARecvFrom(
	//		listen_socket,
	//		&session.wsa_buf,
	//		1,
	//		(LPDWORD)&session.recv_len,
	//		(LPDWORD)&session.recv_flag,
	//		(sockaddr*)&session.client_send_addr,
	//		&session.client_addr_size,
	//		&session.wsa_overlapped,
	//		nullptr);
	//	if (res == SOCKET_ERROR)
	//	{
	//		auto err = ::WSAGetLastError();
	//		if (err != WSA_IO_PENDING)
	//		{
	//			err_msg("WSARecv failed");
	//			continue;
	//		}
	//	}
	//}

	return true;
failed:
	::WSACleanup();
	::CloseHandle(h_iocp);
	return false;
}

void server::run()
{
	send_thread = std::thread(_send_loop);
	// recv_thread = std::thread(_recv_loop);

	send_thread.join();
	// recv_thread.join();

	// getchar();
}

void server::deinit()
{
	logger::clear();
	::WSACleanup();
}