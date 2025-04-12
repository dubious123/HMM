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
#include "bind.h"  // get_binded_socks 함수가 정의되어 있다고 가정

#define SERVER_IP   "2001:2d8:2214:9e87:c3a8:5f41:3f77:338d"  // 서버 IPv6 주소
#define SERVER_PORT 5050                                        // 서버 포트 번호
#define CLIENT_PORT 5054                                        // 클라이언트 포트 번호
#define CLIENT_NAME "Mac_Client"

// 패킷 구조체 정의
typedef struct {
    uint16_t type;
    uint16_t name_length;
    char name[10];
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

// 전역 서버 주소 (모든 세션에 동일)
struct sockaddr_in6 server_addr;

// Session 구조체 정의 (각 세션이 독립적으로 관리됨)
typedef struct {
    int sock;
    uint32_t client_id;
    uint32_t seq_num;
    int is_connected;
    pthread_t recv_thread;
    pthread_t delay_thread; // 연결 후 딜레이 측정용 스레드
} Session;

// 현재 시간을 ns 단위로 반환하는 함수
uint64_t get_current_time_ns() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000LL + (uint64_t)tv.tv_usec * 1000LL;
}

// 패킷 전송 함수 (세션별 소켓 사용)
ssize_t send_packet(int sock, void* packet, size_t size) {
    ssize_t res = sendto(sock, packet, size, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(res == -1) {
        printf("sendto failed with %s\n", strerror(errno));
    } else {
        printf("sendto succeeded, sent %zd bytes\n", res);
    }
    return res;
}

// 세션별 딜레이 측정 패킷 전송 루프
void* session_delay(void* arg) {
    Session* s = (Session*) arg;
    while (1) {
        Packet_3 delay_packet = {
            .type = 3,
            .client_id = s->client_id,
            .seq_num = s->seq_num++,
            .time_client_send = get_current_time_ns(),
            .time_server_recv = 0,
            .time_server_send = 0,
            .time_client_recv = 0
        };
        if(send_packet(s->sock, &delay_packet, sizeof(delay_packet)) > 0) {
            printf("Session (client_id=%u): Sent delay packet, Seq=%u\n", s->client_id, delay_packet.seq_num);
        }
        sleep(1);
    }
    return NULL;
}

// 세션별 서버 응답 수신 스레드
void* session_receive(void* arg) {
    Session* s = (Session*) arg;
    char buffer[1024];
    socklen_t addr_len = sizeof(server_addr);

    while (1) {
        ssize_t recv_len = recvfrom(s->sock, buffer, sizeof(buffer), 0, NULL, &addr_len);
        if (recv_len > 0) {
            uint16_t packet_type = *(uint16_t*)buffer;
            printf("Session (socket=%d): Received packet, len=%zd, type=%u\n", s->sock, recv_len, packet_type);
            switch (packet_type) {
                case 1: {  // 서버 응답 (Packet_1)
                    Packet_1* p = (Packet_1*)buffer;
                    if (p->res == 0) {
                        s->client_id = p->client_id;
                        s->is_connected = 1;
                        printf("Session (socket=%d): Connected! Client ID: %u\n", s->sock, s->client_id);
                        // 클라이언트 재응답 (Packet_2)
                        Packet_2 response = { .type = 2, .client_id = s->client_id, .res = 0 };
                        send_packet(s->sock, &response, sizeof(response));
                    } else {
                        printf("Session (socket=%d): Connection failed, error: %d\n", s->sock, p->res);
                    }
                    // 연결 후 딜레이 스레드 시작 (한 번만 생성)
                    if (s->is_connected && s->delay_thread == 0) {
                        pthread_t tid;
                        if (pthread_create(&tid, NULL, session_delay, s) == 0) {
                            s->delay_thread = tid;
                            pthread_detach(tid);
                        } else {
                            perror("Failed to create delay thread");
                        }
                    }
                    break;
                }
                case 3: {  // 서버의 딜레이 응답 (Packet_3)
                    Packet_3* p = (Packet_3*)buffer;
                    p->time_client_recv = get_current_time_ns();
                    uint64_t delay = (p->time_client_recv - p->time_client_send);
                    printf("Session (socket=%d): Seq=%u, Delay: %" PRIu64 " ns\n", s->sock, p->seq_num, delay);
                    // 딜레이 결과 전송 (Packet_6)
                    Packet_6 delay_response = { .type = 6, .client_id = s->client_id, .seq_num = p->seq_num, .delay = delay };
                    send_packet(s->sock, &delay_response, sizeof(delay_response));
                    break;
                }
                case 5: {  // 서버 종료 요청 (Packet_5)
                    printf("Session (socket=%d): Server requested disconnect. Closing session...\n", s->sock);
                    close(s->sock);
                    pthread_exit(NULL);
                    break;
                }
                default:
                    printf("Session (socket=%d): Unknown packet type received: %u\n", s->sock, packet_type);
            }
        } else if (recv_len == -1) {
            printf("recvfrom failed: %s\n", strerror(errno));
        }
    }
    return NULL;
}

int main() {
    printf("Multi-session client starting...\n");

    // get_binded_socks()로 바인딩된 소켓들을 가져옴
    socket_array sock_arr = get_binded_socks(CLIENT_PORT, 10);
    if(sock_arr.count == 0) {
        perror("Socket creation failed");
        return 1;
    }

    int session_count = sock_arr.count;
    printf("Total sessions: %d\n", session_count);

    // 세션 배열 동적 할당
    Session* sessions = (Session*)malloc(sizeof(Session) * session_count);
    if (!sessions) {
        perror("Failed to allocate memory for sessions");
        return 1;
    }
    memset(sessions, 0, sizeof(Session) * session_count);

    // 서버 주소 설정 (모든 세션에서 공유)
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(SERVER_PORT);
    if(inet_pton(AF_INET6, SERVER_IP, &server_addr.sin6_addr) != 1) {
        printf("inet_pton failed\n");
        return 1;
    }

    // 각 소켓에 대해 세션 초기화 및 연결 요청 (Packet_0 전송)
    for (int i = 0; i < session_count; i++) {
        sessions[i].sock = sock_arr.socks[i];
        sessions[i].seq_num = 0;
        sessions[i].client_id = 0;
        sessions[i].is_connected = 0;
        sessions[i].delay_thread = 0; // 아직 생성되지 않음

        Packet_0 init_packet;
        init_packet.type = 0;
        init_packet.name_length = strlen(CLIENT_NAME);
        memset(init_packet.name, 0, sizeof(init_packet.name));
        strncpy(init_packet.name, CLIENT_NAME, sizeof(init_packet.name));

        if(send_packet(sessions[i].sock, &init_packet, sizeof(init_packet)) == -1) {
            printf("Session %d: Failed to send init packet\n", i);
        } else {
            printf("Session %d: Sent init packet\n", i);
        }

        // 각 세션별 수신 스레드 생성
        pthread_t tid;
        if(pthread_create(&tid, NULL, session_receive, &sessions[i]) != 0) {
            perror("Failed to create receive thread");
        } else {
            sessions[i].recv_thread = tid;
            pthread_detach(tid);
        }
    }

    // 사용자 입력 대기: "exit" 입력 시 모든 세션 종료
    while(1) {
        char command[10];
        scanf("%s", command);
        if (strcmp(command, "exit") == 0) {
            for (int i = 0; i < session_count; i++) {
                Packet_4 disconnect_packet = { .type = 4, .client_id = sessions[i].client_id };
                send_packet(sessions[i].sock, &disconnect_packet, sizeof(disconnect_packet));
                printf("Session %d: Disconnected from server.\n", i);
                close(sessions[i].sock);
            }
            break;
        }
    }

    free(sessions);
    return 0;
}
