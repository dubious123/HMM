#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <chrono>

#define SERVER_IP "fe80::dffa:bfeb:7029:918d%en0"  // 서버의 IPv6 주소 (네트워크 인터페이스 확인 필수)
#define SERVER_PORT 12345                          // 서버의 포트 번호

struct packet {
    uint64_t seq_num;
    uint64_t time_client_send;
};

int main() {
    int sock;
    struct sockaddr_in6 server_addr;
    struct packet p;

    // UDP 소켓 생성
    sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket failed");
        return 1;
    }

    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(SERVER_PORT);
    inet_pton(AF_INET6, SERVER_IP, &server_addr.sin6_addr);

    // 패킷 전송 루프
    p.seq_num = 0;

    while (true) {
        // 현재 시간 기록 (패킷 전송 시간)
        p.time_client_send = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();

        // 서버로 패킷 전송
        if (sendto(sock, &p, sizeof(p), 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("sendto() failed");
            close(sock);
            return 1;
        }

        std::cout << "[Client] Sent packet: " << p.seq_num << std::endl;

        p.seq_num++;  // 패킷 번호 증가
        sleep(1);  // 1초 대기 후 다시 전송
    }

    close(sock);
    return 0;
}
