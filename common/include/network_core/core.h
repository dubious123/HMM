#pragma once
#include <spdlog/spdlog.h>

#define is_false   == false
#define is_true	   == true
#define is_nullptr == nullptr

#define err_msg(msg) logger::error(msg "with error code {} : {}", ::WSAGetLastError(), std::string(print_err(::WSAGetLastError())))

#define PORT_SERVER 5050
#define PORT_CLIENT 5054

using uint64 = uint64_t;
using uint32 = uint32_t;
using uint16 = uint16_t;
using uint8	 = uint8_t;

using int64 = int64_t;
using int32 = int32_t;
using int16 = int16_t;
using int8	= int8_t;

using float32  = float;
using double64 = double;

enum packet_type
{
	client_send,
	server_recv,
	server_send,
	client_recv,
	packet_type_count,
};

LPSTR print_err(int err_code);

struct packet
{
	uint32		seq_num;
	packet_type type	 = packet_type(0);
	uint64		t_c_send = 0;
	uint64		t_s_recv = 0;
	uint64		t_s_send = 0;
	uint64		t_c_recv = 0;
};

namespace logger
{
	namespace detail
	{
		using namespace std;
		extern std::shared_ptr<spdlog::logger> _logger;
	}	 // namespace detail

	void init(const char* output_file_name);

	void clear();

	template <typename... Args>
	inline void trace(spdlog::format_string_t<Args...> fmt, Args&&... args)
	{
		detail::_logger->trace(fmt, std::forward<Args>(args)...);
	}

	template <typename T>
	inline void trace(const T& msg)
	{
		detail::_logger->trace(msg);
	}

	template <typename... Args>
	inline void debug(spdlog::format_string_t<Args...> fmt, Args&&... args)
	{
		detail::_logger->debug(fmt, std::forward<Args>(args)...);
	}

	template <typename T>
	inline void debug(const T& msg)
	{
		detail::_logger->debug(msg);
	}

	template <typename... Args>
	inline void info(spdlog::format_string_t<Args...> fmt, Args&&... args)
	{
		detail::_logger->info(fmt, std::forward<Args>(args)...);
	}

	template <typename T>
	inline void info(const T& msg)
	{
		detail::_logger->info(msg);
	}

	template <typename... Args>
	inline void warn(spdlog::format_string_t<Args...> fmt, Args&&... args)
	{
		detail::_logger->warn(fmt, std::forward<Args>(args)...);
	}

	template <typename T>
	inline void warn(const T& msg)
	{
		detail::_logger->warn(msg);
	}

	template <typename... Args>
	inline void error(spdlog::format_string_t<Args...> fmt, Args&&... args)
	{
		detail::_logger->error(fmt, std::forward<Args>(args)...);
	}

	template <typename T>
	inline void error(const T& msg)
	{
		detail::_logger->error(msg);
	}

	template <typename... Args>
	inline void critical(spdlog::format_string_t<Args...> fmt, Args&&... args)
	{
		detail::_logger->critical(fmt, std::forward<Args>(args)...);
	}

	template <typename T>
	inline void critical(const T& msg)
	{
		detail::_logger->critical(msg);
	}
}	 // namespace logger
