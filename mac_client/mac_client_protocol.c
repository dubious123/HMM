#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>

#define SERVER_IP "2001:2d8:2214:9e87:c3a8:5f41:3f77:338d"  // 서버 IPv6 주소
#define SERVER_PORT 5050                           // 서버 포트 번호
#define CLIENT_NAME "Mac_Client"

// 패킷 구조체 정의
typedef struct {
    uint16_t type;
    uint16_t name_length;
    char name[32];
} Packet_0;

typedef struct {
    uint16_t type;
    char res;
    uint32_t client_id;
} Packet_1;

typedef struct {
    uint16_t type;
    uint32_t client_id;
    char res;
} Packet_2;

typedef struct {
    uint16_t type;
    uint32_t client_id;
    uint32_t seq_num;
    uint64_t time_client_send;
    uint64_t time_server_recv;
    uint64_t time_server_send;
    uint64_t time_client_recv;
} Packet_3;

typedef struct {
    uint16_t type;
    uint32_t client_id;
    uint32_t seq_num;
    uint64_t delay;
} Packet_6;

typedef struct {
    uint16_t type;
    uint32_t client_id;
} Packet_4, Packet_5;

// 전역 변수
int client_socket;
struct sockaddr_in6 server_addr;
uint32_t client_id = 0;
uint32_t seq_num = 0;
int is_connected = 0; // 연결 여부 확인

// 함수 원형 선언
void* delay_loop(void* arg);
void* receive_packets(void* arg);
ssize_t send_packet(void* packet, size_t size);

// 현재 시간을 ns 단위로 반환하는 함수
uint64_t get_current_time_ns() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000LL + (uint64_t)tv.tv_usec * 1000LL;
}

// 패킷 전송 함수
ssize_t send_packet(void* packet, size_t size) {
    return sendto(client_socket, packet, size, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
}

// 딜레이 측정 패킷 전송 루프 (연결 상태와 관계없이 계속 실행)
void* delay_loop(void* arg) {
    while (1) {
        Packet_3 delay_packet = {3, client_id, seq_num++, get_current_time_ns(), 0, 0, 0};
        if(send_packet(&delay_packet, sizeof(delay_packet))>0) {
            printf("Sent delay packet: Seq %u\n", delay_packet.seq_num);
        }
        sleep(1);
    }
    return NULL;
}

// 서버 응답 수신 스레드
void* receive_packets(void* arg) {
    char buffer[1024];
    socklen_t addr_len = sizeof(server_addr);

    while (1) {
        ssize_t recv_len = recvfrom(client_socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, &addr_len);
        if (recv_len > 0) {
            uint16_t packet_type = *(uint16_t*)buffer;
            switch (packet_type) {
                case 1: {  // 서버 응답 (Packet_1)
                    Packet_1* p = (Packet_1*)buffer;
                    if (p->res == 0) {
                        client_id = p->client_id;
                        is_connected = 1;
                        printf("Connected! Client ID: %u\n", client_id);

                        // 클라이언트 재응답 (Packet_2)
                        Packet_2 response = {2, client_id, 0};
                        send_packet(&response, sizeof(response));
                    } else {
                        printf("Connection failed. Error: %d\n", p->res);
                    }
                    break;
                }
                case 3: {  // 서버의 딜레이 응답 (Packet_3)
                    Packet_3* p = (Packet_3*)buffer;
                    p->time_client_recv = get_current_time_ns();
                    uint64_t delay = (p->time_client_recv - p->time_client_send);
                    printf("Seq %u, Delay: %" PRIu64 " ns\n", p->seq_num, delay);

                    // 딜레이 결과 전송 (Packet_6)
                    Packet_6 delay_response = {6, client_id, p->seq_num, delay};
                    send_packet(&delay_response, sizeof(delay_response));
                    break;
                }
                case 5: {  // 서버 종료 요청 (Packet_5)
                    printf("Server requested disconnect. Closing client...\n");
                    close(client_socket);
                    exit(0);
                }
            }
        }
    }
    return NULL;
}

int main() {
    // 소켓 생성 (IPv6, UDP)
    client_socket = socket(AF_INET6, SOCK_DGRAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(SERVER_PORT);
    if(inet_pton(AF_INET6, SERVER_IP, &server_addr.sin6_addr) != 1){
        printf("inet_pton failed");
    }

    // 연결 요청 (Packet_0)
    Packet_0 init_packet = {0, strlen(CLIENT_NAME)};
    strncpy(init_packet.name, CLIENT_NAME, sizeof(init_packet.name));

    ssize_t res = send_packet(&init_packet, sizeof(init_packet));
    if(res == -1){
printf("Sento failed with error code %s...\n", strerror(errno));
    }
    

    // 수신 스레드 시작
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_packets, NULL);
    pthread_detach(recv_thread);

    // 딜레이 측정 스레드 시작 (연결 여부와 관계없이 실행)
    pthread_t delay_thread;
    //pthread_create(&delay_thread, NULL, delay_loop, NULL);
    //pthread_detach(delay_thread);

    // 사용자 입력 대기
    while (1) {
        char command[10];
        scanf("%s", command);
        if (strcmp(command, "exit") == 0) {
            Packet_4 disconnect_packet = {4, client_id};
            send_packet(&disconnect_packet, sizeof(disconnect_packet));
            printf("Disconnected from server.\n");
            break;
        }
    }

    close(client_socket);
    return 0;
}

