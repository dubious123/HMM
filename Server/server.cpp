#include "pch.h"
#include "server.h"
#include <concurrent_queue.h>
#include <concurrent_vector.h>

#define RECV_THREAD_COUNT 2

struct c_session
{
	std::string c_name;
	uint32		c_id;
	bool		connected = false;

	c_session(char* p_name, uint32 name_len, uint32 id) : c_name(p_name, name_len), c_id(id) { };
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
	auto recv_io_datas	 = std::array<recv_io_data, RECV_THREAD_COUNT> {};

	// std::vector<c_session> sessions;
	concurrency::concurrent_vector<c_session> sessions;

	/*auto iocp_key_recv = iocp_key_wsa_recv {};*/
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
	auto send_queue = concurrency::concurrent_queue<std::function<std::tuple<void*, size_t, sockaddr_in6>()>>();

	void _send_loop()
	{
		auto													 seq_num = 0;
		std::function<std::tuple<void*, size_t, sockaddr_in6>()> packet_func;
		while (sending)
		{
			if (send_queue.try_pop(packet_func) is_false)
			{
				continue;
			}

			auto&& [p_mem, len, addr] = packet_func();

			addr.sin6_port = ::htons(PORT_CLIENT);

			// send_packet.time_server_send = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			if (::sendto(send_socket, (char*)p_mem, len, 0, (sockaddr*)&addr, sizeof(sockaddr_in6)) == SOCKET_ERROR)
			{
				err_msg("sendto() failed");
			}
			else
			{
				// logger::info("sendto successed");
			}

			free(p_mem);
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
			// auto* p_packet		 = (packet*)(p_recv_io_data->recv_buf.data());
			auto* p_mem = (void*)(p_recv_io_data->recv_buf.data());

			if (res is_false)
			{
				if (p_recv_io_data is_nullptr)
				{
					continue;	 //?
				}

				err_msg("GetQueuedCompletionStatus failed");
				continue;
			}

			// p_packet->time_server_recv = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			// auto duration			   = std::chrono::nanoseconds(p_packet->time_server_recv - p_packet->time_client_send);
			// logger::info("[server] : seq num : {}, client->server duration : {}ns, thread_id : {}, key : {}", p_packet->seq_num, duration.count(), std::this_thread::get_id()._Get_underlying_id(), (uint64)p_iocp_key);
			// logger::info("p_recv->time_server_recv : {}ns, p_recv->time_client_send : {}ns", p_packet->time_server_recv, p_packet->time_client_send);

			// memset(p_session->recv_buf.data(), 0, p_session->recv_buf.size());

			server::handle_packet(p_mem, recv_len, &p_recv_io_data->client_addr);

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

	listen_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	send_socket	  = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

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

	{
		auto socks = net_core::get_binded_socks(PORT_SERVER, { IF_TYPE_ETHERNET_CSMACD, IF_TYPE_IEEE80211 }, 10);
		if (socks.size() == 0)
		{
			err_msg("bind() failed");
			goto failed;
		}

		listen_socket = socks[0];
	}

	{
		sockaddr_in server_addr		= {};
		server_addr.sin_family		= AF_INET;
		server_addr.sin_addr.s_addr = INADDR_ANY;
		server_addr.sin_port		= htons(PORT_SERVER);

		::bind(send_socket, (sockaddr*)&server_addr, sizeof(server_addr));
	}

	if (::CreateIoCompletionPort((HANDLE)listen_socket, h_iocp, /*(ULONG_PTR)&iocp_key_recv*/ 0, 0) == nullptr)
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

void server::handle_packet(void* p_mem, int32 recv_len, sockaddr_in6* p_addr)
{
	if (recv_len < sizeof(uint16))
	{
		logger::error("invalid packet, recv_len is {}", recv_len);
		return;
	}
	auto packet_type = *(uint16*)p_mem;

	switch (packet_type)
	{
	case 0:
	{
		auto name_len = *(uint16*)((char*)p_mem + sizeof(uint16));
		if (recv_len != sizeof(uint16) * 2 + name_len)
		{
			logger::error("invalid packet, packet type : {} but recv_len is {}", packet_type, recv_len);
			return;
		}

		static auto session_mutex = std::mutex {};

		session_mutex.lock();
		auto client_id = (uint32)sessions.size();
		sessions.push_back({ (char*)p_mem + sizeof(uint16) * 2, name_len, client_id });
		// auto* p_packet = (packet_0*)p_mem;
		session_mutex.unlock();

		auto send_packet = packet_1 { .res = 0, .client_id = client_id };
		// send_queue.push({ (void*)&send_packet, sizeof(packet_1), p_addr });

		send_queue.push(
			[id = client_id, addr = *p_addr]() {
				auto* p_packet = (packet_1*)malloc(sizeof(packet_1));
				assert(p_packet != nullptr);
				{
					p_packet->type		= 1;
					p_packet->res		= 0;
					p_packet->client_id = id;
				}

				logger::info("server : sending {} bytes to {}, id : {}", sizeof(packet_1), sessions[id].c_name, id);

				return std::tuple { (void*)p_packet, sizeof(packet_1), addr };
			});

		break;
	}
	case 2:
	{
		auto* p_packet							= (packet_2*)p_mem;
		sessions[p_packet->client_id].connected = true;

		logger::info("server : client [{}] is now connected", sessions[p_packet->client_id].c_name);
		break;
	}
	case 3:
	{
		auto* p_packet			   = (packet_3*)p_mem;
		p_packet->time_server_recv = utils::time_now();
		send_queue.push(
			[id = p_packet->client_id, addr = *p_addr, seq_num = p_packet->seq_num, time_client_send = p_packet->time_client_send, time_server_recv = p_packet->time_server_recv]() {
				logger::info("server : sending {} bytes to {}, seq_num {}", sizeof(packet_3), sessions[id].c_name, seq_num);
				auto* p_packet = (packet_3*)malloc(sizeof(packet_3));
				assert(p_packet != nullptr);
				{
					p_packet->type			   = 3;
					p_packet->seq_num		   = seq_num;
					p_packet->time_client_send = time_client_send;
					p_packet->time_server_recv = time_server_recv;
					p_packet->time_server_send = utils::time_now();
					p_packet->time_client_recv = 0;
				}

				return std::tuple { (void*)p_packet, sizeof(packet_3), addr };
			});
		break;
	}
	case 6:
	{
		auto* p_packet = (packet_6*)p_mem;
		logger::info("seq : [{}], delay : {}", p_packet->seq_num, p_packet->delay);
		break;
	}
	default:
		break;
	}
}