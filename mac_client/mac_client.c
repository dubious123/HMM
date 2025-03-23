#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <inttypes.h>

#define SERVER_IP "fe80::7087:d734:b188:8dde%en0"  // 서버의 IPv6 주소
#define SERVER_PORT 5050                           // 서버의 포트 번호

// 패킷 구조체 정의
struct packet {
    uint8_t type;                  // 0: 요청, 1: 응답 등
    uint64_t seq_num;              // 패킷 순번
    uint64_t time_client_send;     // 클라이언트에서 전송한 시간 (ns 단위)
    uint64_t time_server_receive;  // 서버에서 요청을 수신한 시간 (ns 단위)
    uint64_t time_server_send;     // 서버에서 응답한 시간 (ns 단위)
    uint64_t time_client_recv;     // 클라이언트에서 응답 수신 시간 (ns 단위)
    uint64_t predicted_delay;      // 예측된 네트워크 딜레이 값 (ns 단위)
};

// 현재 시간을 ns 단위로 반환하는 함수
uint64_t get_current_time_ns() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000LL + (uint64_t)tv.tv_usec * 1000LL;
}

int main() {
    int sock;
    struct sockaddr_in6 server_addr, client_addr;
    struct packet p;
    socklen_t addr_len = sizeof(server_addr);

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

    // 클라이언트 소켓 바인딩 (서버 응답 수신을 위해 필요)
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin6_family = AF_INET6;
    client_addr.sin6_port = htons(0);  // OS가 랜덤한 포트 할당
    client_addr.sin6_addr = in6addr_any;
    if (bind(sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        perror("bind failed");
        close(sock);
        return 1;
    }

    // 패킷 전송 루프
    p.seq_num = 0;

    while (1) {
        // 현재 시간 기록 (패킷 전송 시간)
        p.time_client_send = get_current_time_ns();

        // 서버로 패킷 전송
        if (sendto(sock, &p, sizeof(p), 0, (struct sockaddr*)&server_addr, addr_len) < 0) {
            perror("sendto() failed");
            close(sock);
            return 1;
        }

        printf("[Client] Sent packet: %" PRIu64 "\n", p.seq_num);

        // 서버 응답 대기
        // if (recvfrom(sock, &p, sizeof(p), 0, (struct sockaddr*)&server_addr, &addr_len) < 0) {
        //     perror("recvfrom() failed");
        //     close(sock);
        //     return 1;
        // }

        // 현재 시간 기록 (서버 응답 수신 시간)
        p.time_client_recv = get_current_time_ns();

        // RTT (왕복 시간) 계산 및 출력
        uint64_t rtt = p.time_client_recv - p.time_client_send;
        printf("[Client] Received seq_num: %" PRIu64 ", RTT: %" PRIu64 " ns\n", p.seq_num, rtt);

        p.seq_num++;  // 패킷 번호 증가
        sleep(1);  // 1초 대기 후 다시 전송
    }

    close(sock);
    return 0;
}
