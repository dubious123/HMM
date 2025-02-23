#include <print>
#include <string>
#include <format>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "core.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

#pragma comment(lib, "Ws2_32.lib")
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
