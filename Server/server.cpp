#include "pch.h"
#include "server.h"
#include <concurrent_queue.h>
#include <concurrent_vector.h>

#define RECV_THREAD_COUNT 2

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

struct iocp_key_wsa_recv
{
};

struct recv_io_data
{
	WSAOVERLAPPED		   wsa_overlapped;	  // do not move
	WSABUF				   wsa_buf;
	std::array<char, 1024> recv_buf;
	sockaddr_in6		   client_addr;
	int32				   client_addr_size;
	uint32				   recv_len;
	uint32				   io_flag;

	recv_io_data() : io_flag(0), client_addr_size(sizeof(client_addr))
	{
		assert((uint64)this == (uint64)&wsa_overlapped);
		ZeroMemory(&wsa_overlapped, sizeof(WSAOVERLAPPED));

		wsa_buf.len = recv_buf.size();
		wsa_buf.buf = recv_buf.data();
	}
};

struct send_io_data
{
	WSAOVERLAPPED		   wsa_overlapped;	  // do not move
	WSABUF				   wsa_buf;
	std::array<char, 1024> recv_buf;
	sockaddr_in6		   client_addr;
	int32				   client_addr_size;
	uint32				   recv_len;
	uint32				   io_flag;

	send_io_data() : io_flag(0), client_addr_size(sizeof(client_addr))
	{
		assert((uint64)this == (uint64)&wsa_overlapped);
		ZeroMemory(&wsa_overlapped, sizeof(WSAOVERLAPPED));

		wsa_buf.len = recv_buf.size();
		wsa_buf.buf = recv_buf.data();
	}
};

namespace
{
	auto send_socket	  = SOCKET {};
	auto listen_socket	  = SOCKET {};
	auto server_addr_info = sockaddr_in6 {};

	auto send_thread = std::thread();

	auto sending = true;


	auto h_iocp = HANDLE {};

	auto recv_thread_arr = std::array<std::thread, RECV_THREAD_COUNT> {};
	// auto listen_thread_arr = std::array<std::thread, LISTEN_THREAD_COUNT> {};
	// auto sessions		   = std::array<c_session, RECV_THREAD_COUNT> {};
	auto recv_io_datas = std::array<recv_io_data, RECV_THREAD_COUNT> {};

	auto iocp_key_recv = iocp_key_wsa_recv {};
}	 // namespace

struct memory_buffer
{
	//[size (2bytes)][data]
	using buf_size_t = uint16;

	static constinit const uint32 BUF_SIZE = (1ull << 16);

	std::array<char, BUF_SIZE> buf;
	uint32					   begin = 0;
	uint32					   end	 = 0;

	// 0 begin end 1023
	//	0 begin end end + write 1023
	//	0 end + write begin end 1023
	//   0 begin end + write end 1023 (x)
	// 0 end begin 1023
	// 0 end end + write begin 2023
	// 0 end begin end + write 2023 (x)
	// 0 end + write end begin 2023 (x)
	void clean()
	{
		auto size = end - begin;
		memcpy(&buf[0], &buf[begin], size);
		begin -= size;
		end	  -= size;
	}

	void write(uint16 amount, void (*write_func)(char*))
	{
		if (end + amount > BUF_SIZE)
		{
			clean();
		}

		assert(end + amount <= BUF_SIZE);

		write_func(&buf[end]);
		end += amount;
	}

	void* read()
	{
		assert(begin + sizeof(buf_size_t) <= end);
		auto  p_size  = (buf_size_t*)(&buf[begin]);
		auto* p_res	  = &buf[begin + sizeof(buf_size_t)];
		begin		 += (sizeof(buf_size_t) + *p_size);

		if (begin == end)
		{
			begin = 0;
			end	  = 0;
		}

		return p_res;
	}
};

namespace
{
	auto send_queue = concurrency::concurrent_queue<std::tuple<sockaddr_in6, packet>>();

	void _send_loop()
	{
		auto seq_num = 0;
		auto tpl	 = std::tuple<sockaddr_in6, packet>();
		while (sending)
		{
			if (send_queue.try_pop(tpl) is_false)
			{
				continue;
			}

			auto&& [addr, send_packet] = tpl;

			addr.sin6_port = ::htons(PORT_CLIENT);


			send_packet.time_server_send = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			logger::info("server : sending {}", send_packet.seq_num);
			if (::sendto(send_socket, (char*)&send_packet, sizeof(packet), 0, (sockaddr*)&addr, sizeof(sockaddr_in6)) == SOCKET_ERROR)
			{
				err_msg("sendto() failed");
			}
			else
			{
				// logger::info("sendto successed");
			}
		}
	}

	// auto send_queue2 = concurrency::concurrent_queue<std::tuple<sockaddr_in6, memory_buffer::buf_size_t, void (*)(char*)>>();

	// void _send_loop2()
	//{
	//	// static auto send_buf = memory_buffer();
	//	auto seq_num = 0;
	//	auto tpl	 = std::tuple<sockaddr_in6, void*, uint32>();
	//	while (sending)
	//	{
	//		auto queue_size = send_queue.unsafe_size();

	//		for (auto _ : std::views::iota(0) | std::views::take(queue_size))
	//		{
	//			send_queue.try_pop(tpl);
	//			auto&& [addr, packet_size, p_packet_factory_func] = tpl;

	//			// send_buf.write(packet_size, p_packet_factory_func);

	//			addr.sin6_port = ::htons(PORT_CLIENT);
	//			auto res	   = WSASendTo(
	//				  /*[in] SOCKET							  */ send_socket,
	//				  /*[in] LPWSABUF							  */ lpBuffers,
	//				  /*[in] DWORD							  */ 1,
	//				  /*[out] LPDWORD							  */ nullptr,
	//				  /*[in] DWORD							  */ 0,
	//				  /*[in] const sockaddr*					  */ (sockaddr*)&addr,
	//				  /*[in] int								  */ sizeof(addr),
	//				  /*[in] LPWSAOVERLAPPED					  */ lpOverlapped,
	//				  /*[in] LPWSAOVERLAPPED_COMPLETION_ROUTINE */ nullptr);
	//		}


	//		send_packet.time_server_send = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	//		logger::info("server : sending {}", send_packet.seq_num);


	//		if (::sendto(send_socket, (char*)&send_packet, sizeof(packet), 0, (sockaddr*)&addr, sizeof(sockaddr_in6)) == SOCKET_ERROR)
	//		{
	//			err_msg("sendto() failed");
	//		}
	//		else
	//		{
	//			// logger::info("sendto successed");
	//		}
	//	}
	//}

	void _iocp_recv_loop()
	{
		while (true)
		{
			auto  recv_len		 = 0;
			auto* p_iocp_key	 = (iocp_key_wsa_recv*)nullptr;
			auto* p_recv_io_data = (recv_io_data*)nullptr;
			auto  res			 = ::GetQueuedCompletionStatus(h_iocp, (LPDWORD)&recv_len, (PULONG_PTR)&p_iocp_key, (WSAOVERLAPPED**)&p_recv_io_data, INFINITE);
			auto* p_packet		 = (packet*)(p_recv_io_data->recv_buf.data());

			if (res is_false)
			{
				if (p_recv_io_data is_nullptr)
				{
					continue;	 //?
				}

				err_msg("GetQueuedCompletionStatus failed");
				continue;
			}

			p_packet->time_server_recv = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			auto duration			   = std::chrono::nanoseconds(p_packet->time_server_recv - p_packet->time_client_send);
			logger::info("[server] : seq num : {}, client->server duration : {}ns, thread_id : {}, key : {}", p_packet->seq_num, duration.count(), std::this_thread::get_id()._Get_underlying_id(), (uint64)p_iocp_key);
			logger::info("p_recv->time_server_recv : {}ns, p_recv->time_client_send : {}ns", p_packet->time_server_recv, p_packet->time_client_send);

			send_queue.push({ p_recv_io_data->client_addr, *p_packet });

			// memset(p_session->recv_buf.data(), 0, p_session->recv_buf.size());
			res = ::WSARecvFrom(listen_socket,
								&p_recv_io_data->wsa_buf,
								1,
								/*(LPDWORD)&sessions[idx].recv_len*/ nullptr,
								(LPDWORD)&p_recv_io_data->io_flag,
								(sockaddr*)&p_recv_io_data->client_addr,
								&p_recv_io_data->client_addr_size,
								&p_recv_io_data->wsa_overlapped,
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
	// ZeroMemory(host_name, sizeof(host_name));

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

	h_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, RECV_THREAD_COUNT);
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

	if (::CreateIoCompletionPort((HANDLE)listen_socket, h_iocp, (ULONG_PTR)&iocp_key_recv, 0) == nullptr)
	{
		err_msg("CreateIoCompletionPort() failed");
		goto failed;
	}

	for (auto idx : std::views::iota(0, RECV_THREAD_COUNT))
	{
		while (true)
		{
			auto res = ::WSARecvFrom(listen_socket,
									 &recv_io_datas[idx].wsa_buf,
									 1,
									 /*(LPDWORD)&sessions[idx].recv_len*/ nullptr,
									 (LPDWORD)&recv_io_datas[idx].io_flag,
									 (sockaddr*)&recv_io_datas[idx].client_addr,
									 &recv_io_datas[idx].client_addr_size,
									 &recv_io_datas[idx].wsa_overlapped,
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
					break;
				}
			}
			else
			{
				assert(res == 0);
			}
		}


		recv_thread_arr[idx] = std::thread(_iocp_recv_loop);
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