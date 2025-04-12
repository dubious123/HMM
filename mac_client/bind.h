//
// Created by 김수오 on 25. 4. 5.
//

#ifndef BIND_H
#define BIND_H
typedef struct {
    int* socks;       // 소켓 파일 디스크립터 배열
    size_t count;     // 생성된 소켓 개수
} socket_array;

int is_link_local(struct in6_addr addr);
int is_wifi_or_ethernet(const char *ifname);
/*
 * port: 바인딩할 포트 번호
 * adapter_filter: 원래 Windows에서 어댑터 타입 필터링용으로 사용하던 값 (Linux에서는 사용하지 않음)
 * adapter_filter_count: adapter_filter 배열의 길이
 * max_count: 생성할 최대 소켓 개수
 */
socket_array get_binded_socks(uint16_t port, uint32_t max_count);
#endif //BIND_H
