#pragma once

namespace server
{
	bool init();
	void run();
	void deinit();

	void handle_packet(void* p_packet, int32 recv_len, sockaddr_in6* p_addr);
}	 // namespace server