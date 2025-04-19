#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <inttypes.h>
#include "bind.h"

// 서버 IPv4 주소 & 포트
#define SERVER_IP   "121.88.244.43"   // 혹은 "192.168.0.10" 등
#define SERVER_PORT 12345

// 클라이언트가 bind할 포트
#define CLIENT_PORT 12346
#define CLIENT_NAME "Mac_Client"

// ----------------------------
// 패킷 구조체들
// ----------------------------
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

// ----------------------------
// 세션 구조체
// ----------------------------
typedef struct {
    int sock;
    uint32_t client_id;
    uint32_t seq_num;
    int is_connected;
    pthread_t recv_thread;
    pthread_t delay_thread;
} Session;

// 서버 주소 (IPv4)
struct sockaddr_in server_addr;

// ----------------------------
// 도우미 함수들
// ----------------------------

// 현재 시간을 ns로 반환
uint64_t get_current_time_ns() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000LL + (uint64_t)tv.tv_usec * 1000LL;
}

// 서버로 패킷 전송
ssize_t send_packet(int sock, void* packet, size_t size) {
    ssize_t res = sendto(sock, packet, size, 0,
                         (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (res == -1) {
        printf("sendto failed: %s\n", strerror(errno));
    } else {
        printf("sendto succeeded, sent %zd bytes\n", res);
        printf("packet type : %d\n", *(ushort*)packet);
    }
    return res;
}

// ----------------------------
// 딜레이 측정용 스레드
// ----------------------------
void* session_delay(void* arg) {
    Session* s = (Session*) arg;
    while (1) {
        Packet_3 pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type             = 3;
        pkt.client_id        = s->client_id;
        pkt.seq_num          = s->seq_num++;
        pkt.time_client_send = get_current_time_ns();

        if (send_packet(s->sock, &pkt, sizeof(pkt)) > 0) {
            printf("[client_id=%u] Sent delay packet, Seq=%u\n",
                   s->client_id, pkt.seq_num);
        }
        sleep(1);
    }
    return NULL;
}

// ----------------------------
// 수신 스레드
// ----------------------------
void* session_receive(void* arg) {
    Session* s = (Session*) arg;
    char buffer[1024];
    socklen_t addr_len = sizeof(server_addr);

    while (1) {
        ssize_t recv_len = recvfrom(s->sock, buffer, sizeof(buffer),
                                    0, NULL, &addr_len);
        if (recv_len > 0) {
            uint16_t packet_type = *(uint16_t*)buffer;
            printf("[socket=%d] Received packet, len=%zd, type=%u\n",
                   s->sock, recv_len, packet_type);

            switch (packet_type) {
                case 1: { // Packet_1 (서버 응답)
                    Packet_1* p = (Packet_1*)buffer;
                    if (p->res == 0) {
                        s->client_id   = p->client_id;
                        s->is_connected = 1;
                        printf("Connected! Client ID=%u\n", s->client_id);

                        // 연결 성공 → Packet_2 전송
                        Packet_2 resp;
                        memset(&resp, 0, sizeof(resp));
                        resp.type      = 2;
                        resp.client_id = s->client_id;
                        resp.res       = 0;
                        send_packet(s->sock, &resp, sizeof(resp));

                        // 연결 후 딜레이 스레드 시작
                        if (s->delay_thread == 0) {
                            pthread_t tid;
                            if (pthread_create(&tid, NULL, session_delay, s) == 0) {
                                s->delay_thread = tid;
                                pthread_detach(tid);
                            } else {
                                perror("Failed to create delay thread");
                            }
                        }
                    } else {
                        printf("Connection failed, error code=%d\n", p->res);
                    }
                    break;
                }
                case 3: { // Packet_3 (딜레이 응답)
                    Packet_3* p = (Packet_3*)buffer;
                    p->time_client_recv = get_current_time_ns();
                    uint64_t delay_ns = (p->time_client_recv - p->time_client_send);
                    printf("Seq=%u, Delay=%" PRIu64 " ns\n", p->seq_num, delay_ns);

                    // 딜레이 결과(Packet_6) 전송
                    Packet_6 dres;
                    memset(&dres, 0, sizeof(dres));
                    dres.type      = 6;
                    dres.client_id = s->client_id;
                    dres.seq_num   = p->seq_num;
                    dres.delay     = delay_ns;
                    send_packet(s->sock, &dres, sizeof(dres));
                    break;
                }
                case 5: { // Packet_5 (서버 종료 알림)
                    printf("Server requested disconnect. Closing session...\n");
                    close(s->sock);
                    pthread_exit(NULL);
                    break;
                }
                default:
                    printf("Unknown packet type=%u\n", packet_type);
            }
        } else if (recv_len == -1) {
            printf("recvfrom failed: %s\n", strerror(errno));
        }
    }
    return NULL;
}

// ----------------------------
// main
// ----------------------------
int main() {
    printf("=== IPv4 Client Starting... ===\n");

    // bind.h/c를 통해 IPv4 소켓들 가져오기
    socket_array sock_arr = get_binded_socks(CLIENT_PORT, 10);

    if (sock_arr.count == 0) {
        printf("No sockets created. Exiting.\n");
        return 1;
    }

    printf("Total sessions: %zu\n", sock_arr.count);

    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) != 1) {
        printf("inet_pton failed\n");
        return 1;
    }

    // 세션 배열 할당
    Session* sessions = (Session*)malloc(sizeof(Session) * sock_arr.count);
    if (!sessions) {
        perror("malloc for sessions failed");
        return 1;
    }
    memset(sessions, 0, sizeof(Session) * sock_arr.count);

    // 각 소켓에 대해 init 패킷(Packet_0) 전송
    for (size_t i = 0; i < sock_arr.count; i++) {
        sessions[i].sock = sock_arr.socks[i];
        sessions[i].seq_num = 0;
        sessions[i].client_id = 0;
        sessions[i].is_connected = 0;
        sessions[i].delay_thread = 0;

        // Packet_0
        Packet_0 initp;
        memset(&initp, 0, sizeof(initp));
        initp.type         = 0;
        initp.name_length  = strlen(CLIENT_NAME);
        strncpy(initp.name, CLIENT_NAME, sizeof(initp.name));

        // 전송
        if (send_packet(sessions[i].sock, &initp, sizeof(initp)) == -1) {
            printf("[session %zu] Failed to send init packet\n", i);
        } else {
            printf("[session %zu] Sent init packet\n", i);
        }

        // 수신 스레드 생성
        pthread_t tid;
        if (pthread_create(&tid, NULL, session_receive, &sessions[i]) != 0) {
            perror("Failed to create recv thread");
        } else {
            sessions[i].recv_thread = tid;
            pthread_detach(tid);
        }
    }

    // "exit" 입력 시 disconnect
    while (1) {
        char command[16];
        if (scanf("%15s", command) == 1) {
            if (strcmp(command, "exit") == 0) {
                for (size_t i = 0; i < sock_arr.count; i++) {
                    Packet_4 disc;
                    memset(&disc, 0, sizeof(disc));
                    disc.type      = 4; // disconnect
                    disc.client_id = sessions[i].client_id;
                    send_packet(sessions[i].sock, &disc, sizeof(disc));
                    printf("[session %zu] Disconnected.\n", i);
                    close(sessions[i].sock);
                }
                break;
            }
        }
    }

    free(sessions);
    free(sock_arr.socks);
    return 0;
}
