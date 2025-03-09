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
    uint64_t time_server_send;
    uint64_t time_client_recv;
};

int main() {
    int sock;
    struct sockaddr_in6 server_addr, client_addr;
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

    // 클라이언트 소켓 바인딩 (응답을 받기 위해 필요)
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin6_family = AF_INET6;
    client_addr.sin6_port = htons(0);  // OS가 랜덤한 포트 할당
    client_addr.sin6_addr = in6addr_any;
    bind(sock, (struct sockaddr*)&client_addr, sizeof(client_addr));

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

        // 서버 응답 대기
        socklen_t addr_len = sizeof(server_addr);
        if (recvfrom(sock, &p, sizeof(p), 0, (struct sockaddr*)&server_addr, &addr_len) < 0) {
            perror("recvfrom() failed");
            close(sock);
            return 1;
        }

        // 현재 시간 기록 (서버 응답 수신 시간)
        p.time_client_recv = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();

        // RTT (왕복 시간) 계산 및 출력
        auto rtt = p.time_client_recv - p.time_client_send;
        std::cout << "[Client] Received seq_num: " << p.seq_num
                  << ", RTT: " << rtt << " ns" << std::endl;

        p.seq_num++;  // 패킷 번호 증가
        sleep(1);  // 1초 대기 후 다시 전송
    }

    close(sock);
    return 0;
}
