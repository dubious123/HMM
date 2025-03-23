#pragma once

namespace client
{
	bool init();
	void run();
	void deinit();

	void handle_packet(uint32 session_idx, void* p_packet, int32 recv_len);
}	 // namespace client