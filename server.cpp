#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

GlobalData g_data;

void Msg(const char *s) { std::cout << s << "\n"; }

bool CmdIs(const std::string &word, const char *cmd) { return word == cmd; }

bool Str2Int(const std::string &s, int64_t &out) {
  char *endp = nullptr;
  out = strtoll(s.c_str(), &endp, 10);
  return endp == s.c_str() + s.size();
}

bool EntryEq(HNode *lhs, HNode *rhs) {
  struct Entry *le = container_of(lhs, struct Entry, node);
  struct Entry *re = container_of(rhs, struct Entry, node);
  return lhs->hcode == rhs->hcode && le->key == re->key;
}

bool HnodeSame(HNode *lhs, HNode *rhs) { return lhs == rhs; }

void CbScan(HNode *node, void *arg) {
  std::string &out = *(std::string *)arg;
  OutStr(out, container_of(node, Entry, node)->key);
}

void EntrySetTtl(Entry *ent, int64_t ttl_ms) {
  if (ttl_ms < 0 && ent->heap_idx != (size_t)-1) {
    size_t pos = ent->heap_idx;
    g_data.heap[pos] = g_data.heap.back();
    g_data.heap.pop_back();
    if (pos < g_data.heap.size()) {
      HeapUpdate(g_data.heap.data(), pos, g_data.heap.size());
    }
    ent->heap_idx = -1;
  } else if (ttl_ms >= 0) {
    size_t pos = ent->heap_idx;
    if (pos == (size_t)-1) {
      HeapItem item;
      item.ref = &ent->heap_idx;
      g_data.heap.push_back(item);
      pos = g_data.heap.size() - 1;
    }
    g_data.heap[pos].val =
        GetMonotonicUsec() + (uint64_t)ttl_ms * 1000;
    HeapUpdate(g_data.heap.data(), pos, g_data.heap.size());
  }
}

void ZsetDispose(ZSet *zset) { (void)zset; }

void EntryDel(Entry *ent) {
  switch (ent->type) {
    case kTypeZset:
      ZsetDispose(ent->zset);
      break;
  }
  EntrySetTtl(ent, -1);
  delete ent;
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cout << "socket() failed\n";
    return 1;
  }

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  sockaddr_in addrr;
  addrr.sin_family = AF_INET;
  addrr.sin_addr.s_addr = htonl(INADDR_ANY);
  addrr.sin_port = htons(1234);

  int rv = bind(fd, (const sockaddr *)&addrr, sizeof(addrr));
  if (rv) {
    std::cout << "bind() failed\n";
    return 1;
  }

  rv = listen(fd, 5);
  if (rv) {
    std::cout << "listen() failed\n";
    return 1;
  }

  std::cout << "Server listening on port 1234...\n";

  FdSetNb(fd);

  int epfd = epoll_create1(0);
  if (epfd < 0) {
    std::cout << "epoll_create1() failed\n";
    return 1;
  }

  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = fd;
  epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

  g_data.epfd = epfd;
  DlistInit(&g_data.idle_list);

  struct epoll_event events[kMaxEvents];

  while (true) {
    int timeout_ms = (int)NextTimerMs();
    int nfds = epoll_wait(epfd, events, kMaxEvents, timeout_ms);
    if (nfds < 0) {
      std::cout << "epoll_wait() error\n";
      continue;
    }

    for (int i = 0; i < nfds; i++) {
      if (events[i].data.fd == fd) {
        while (true) {
          int32_t err = AcceptNewConn(fd, epfd);
          if (err) {
            break;
          }
        }
      } else {
        Conn *conn = (Conn *)events[i].data.ptr;
        if (events[i].events &
            (EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLERR)) {
          ConnectionIo(conn);
          if (conn->state == kStateEnd) {
            ConnDone(conn);
          } else {
            struct epoll_event cev;
            cev.data.ptr = conn;
            cev.events =
                (conn->state == kStateReq) ? EPOLLIN : EPOLLOUT;
            epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &cev);
          }
        }
      }
    }

    ProcessTimers();
  }

  close(fd);
  return 0;
}
