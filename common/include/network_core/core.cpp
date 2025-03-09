#include <print>
#include <string>
#include <format>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "core.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

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
		std::shared_ptr<spdlog::logger> _logger = std::make_shared<spdlog::logger>(spdlog::logger("Logger"));
	}	 // namespace detail

	void init(const char* output_file_name)
	{
		auto file_sink	  = std::make_shared<spdlog::sinks::basic_file_sink_st>(output_file_name, true);
		auto console_sink = std::make_shared<spdlog::sinks::stdout_sink_st>();
		detail::_logger->sinks().push_back(file_sink);
		detail::_logger->sinks().push_back(console_sink);
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

bool net_core::bind(SOCKET sock, sockaddr_in6* p_out_addr, uint16 port)
{
	auto flags	  = GAA_FLAG_INCLUDE_PREFIX;
	auto family	  = AF_INET6;
	auto addr_buf = std::array<char, (sizeof(IP_ADAPTER_ADDRESSES) * 30)>();

	// Get the adapter addresses.
	auto buf_len = addr_buf.size();
	auto ret	 = ::GetAdaptersAddresses(family, flags, NULL, (PIP_ADAPTER_ADDRESSES)addr_buf.data(), (PULONG)&buf_len);
	if (ret != NO_ERROR)
	{
		err_msg("GetAdaptersAddresses() failed");
		return false;
	}

	for (auto* adapter = (IP_ADAPTER_ADDRESSES*)(addr_buf.data()); adapter != nullptr; adapter = adapter->Next)
	{
		// if (adapter->IfType != 6 and adapter->IfType != 71)
		//{
		//	continue;
		// }

		if (adapter->IfType != 6 and _wcsnicmp(adapter->FriendlyName, L"Wi-Fi", 100) != 0)
		{
			continue;
		}

		// logger::info(L"Adapter: {}", adapter->FriendlyName);
		// logger::info(L"Interface Type: {}", adapter->IfType);

		// todo
		for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
		{
			if (unicast->Address.lpSockaddr->sa_family != AF_INET6)
			{
				continue;
			}

			*p_out_addr			  = *(sockaddr_in6*)(unicast->Address.lpSockaddr);
			p_out_addr->sin6_port = ::htons(port);

			if (::bind(sock, (sockaddr*)p_out_addr, sizeof(sockaddr_in6)) == SOCKET_ERROR)
			{
				// err_msg("bind() failed");
				continue;
			}

			auto	wstr_len					  = (DWORD)INET6_ADDRSTRLEN;
			wchar_t p_wstr_ipv6[INET6_ADDRSTRLEN] = { 0 };

			// Convert the IPv6 address to a string.
			if (::WSAAddressToStringW((LPSOCKADDR)p_out_addr, sizeof(sockaddr_in6), NULL, p_wstr_ipv6, &wstr_len) == 0)
			{
				logger::info(L"binding success, IPv6 Address: {}, interface : {}", p_wstr_ipv6, adapter->FriendlyName);
			}


			return true;
		}
		// logger::info("=====================================================");
	}

	return false;
}
