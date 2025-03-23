#include <print>
#include <string>
#include <format>
#include <span>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "core.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/async.h>
#include <spdlog/spdlog.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "spdlog.lib")

LPSTR print_err(int err_code)
{
	static char msg[1024];

	// If this program was multithreaded, we'd want to use
	// FORMAT_MESSAGE_ALLOCATE_BUFFER instead of a static buffer here.
	// (And of course, free the buffer when we were done with it)

	::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK, NULL, err_code,
					 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					 (LPSTR)msg, 1024, NULL);
	return msg;
}

namespace logger
{
	namespace detail
	{
		std::shared_ptr<spdlog::logger> _logger;
	}	 // namespace detail

	void init(const char* output_file_name)
	{
		spdlog::init_thread_pool(1024, 1);
		auto file_sink	  = std::make_shared<spdlog::sinks::basic_file_sink_st>(output_file_name, true);
		auto console_sink = std::make_shared<spdlog::sinks::stdout_sink_st>();
		detail::_logger	  = std::make_shared<spdlog::async_logger>(std::string("Logger"), spdlog::sinks_init_list { file_sink, console_sink }, spdlog::thread_pool(), spdlog::async_overflow_policy::block);
	}

	void clear()
	{
		detail::_logger->flush();
	}

}	 // namespace logger

std::string utils::ip6addr_to_string(IN6_ADDR addr)
{
	return std::format("{:X},{:X},{:X},{:X},{:X},{:X},{:X},{:X}",
					   addr.u.Word[0],
					   addr.u.Word[1],
					   addr.u.Word[2],
					   addr.u.Word[3],
					   addr.u.Word[4],
					   addr.u.Word[5],
					   addr.u.Word[6],
					   addr.u.Word[7]);
}

uint64 utils::time_now()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::vector<SOCKET> net_core::get_binded_socks(uint16 port, std::initializer_list<uint64> adapter_filter, uint32 max_count)
{
	auto socks	  = std::vector<SOCKET> {};
	auto flags	  = GAA_FLAG_INCLUDE_PREFIX;
	auto family	  = AF_INET6;
	auto addr_buf = std::vector<char>((sizeof(IP_ADAPTER_ADDRESSES) * 30));
	auto buf_len  = addr_buf.size();
	auto ret	  = ::GetAdaptersAddresses(family, flags, NULL, (PIP_ADAPTER_ADDRESSES)addr_buf.data(), (PULONG)&buf_len);
	if (ret == ERROR_BUFFER_OVERFLOW)
	{
		addr_buf.resize(buf_len);
		ret = ::GetAdaptersAddresses(family, flags, NULL, (PIP_ADAPTER_ADDRESSES)addr_buf.data(), (PULONG)&buf_len);
	}
	if (ret != NO_ERROR)
	{
		err_msg("GetAdaptersAddresses() failed");
		return socks;
	}

	for (auto* adapter = (IP_ADAPTER_ADDRESSES*)(addr_buf.data()); adapter != nullptr; adapter = adapter->Next)
	{
		if (adapter->OperStatus != IfOperStatusUp)
		{
			continue;
		}

		if (adapter->IfType != 0 and std::ranges::find(adapter_filter, adapter->IfType) == adapter_filter.end())
		{
			continue;
		}

		for (auto* p_unicast = adapter->FirstUnicastAddress; p_unicast != nullptr; p_unicast = p_unicast->Next)
		{
			if (p_unicast->Address.lpSockaddr->sa_family != AF_INET6)
			{
				continue;
			}

			auto sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
			if (sock == INVALID_SOCKET)
			{
				err_msg("socket creation failed");
				continue;
			}

			auto* addr		= (sockaddr_in6*)(p_unicast->Address.lpSockaddr);
			auto  sock_addr = sockaddr_in6 {};
			ZeroMemory(&sock_addr, sizeof(sock_addr));
			sock_addr.sin6_family	= AF_INET6;
			sock_addr.sin6_port		= htons(port);
			sock_addr.sin6_addr		= addr->sin6_addr;
			sock_addr.sin6_scope_id = addr->sin6_scope_id;	  // Needed for link-local addresses.

			if (::bind(sock, (sockaddr*)&sock_addr, sizeof(sockaddr_in6)) == SOCKET_ERROR)
			{
				err_msg("bind failed");
				::closesocket(sock);
				continue;
			}

			auto	wstr_len					  = (DWORD)INET6_ADDRSTRLEN;
			wchar_t p_wstr_ipv6[INET6_ADDRSTRLEN] = { 0 };

			// Convert the IPv6 address to a string.
			if (::WSAAddressToStringW((LPSOCKADDR)&sock_addr, sizeof(sockaddr_in6), NULL, p_wstr_ipv6, &wstr_len) == 0)
			{
				logger::info(L"binding success, IPv6 Address: {}, interface description : {}", p_wstr_ipv6, adapter->Description);
			}

			socks.emplace_back(sock);

			if (socks.size() >= max_count)
			{
				return socks;
			}
		}
		// logger::info("=====================================================");
	}

	return socks;
}
