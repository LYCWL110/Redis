#include "server.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

void FdSetNb(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    std::cout << "fcntl(F_GETFL) error\n";
    return;
  }
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) < 0) {
    std::cout << "fcntl(F_SETFL) error\n";
  }
}

void ConnPut(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

uint64_t GetMonotonicUsec() {
  timespec tv = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &tv);
  return uint64_t(tv.tv_sec) * 1000000 + tv.tv_nsec / 1000;
}

void ConnDone(Conn *conn) {
  g_data.fd2conn[conn->fd] = nullptr;
  epoll_ctl(g_data.epfd, EPOLL_CTL_DEL, conn->fd, nullptr);
  close(conn->fd);
  DlistDetach(&conn->idle_list);
  delete conn;
}

void ConnectionIo(Conn *conn) {
  conn->idle_start = GetMonotonicUsec();
  DlistDetach(&conn->idle_list);
  DlistInsertBefore(&g_data.idle_list, &conn->idle_list);

  if (conn->state == kStateReq) {
    StateReq(conn);
  } else if (conn->state == kStateRes) {
    StateRes(conn);
  } else {
    assert(0);
  }
}

bool TryFillBuffer(Conn *conn) {
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  ssize_t rv = 0;
  do {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (rv < 0 && errno == EINTR);
  if (rv < 0 && errno == EAGAIN) {
    return false;
  }
  if (rv < 0) {
    std::cout << "read() error\n";
    conn->state = kStateEnd;
    return false;
  }
  if (rv == 0) {
    if (conn->rbuf_size > 0) {
      std::cout << "unexpected EOF\n";
    } else {
      std::cout << "EOF\n";
    }
    conn->state = kStateEnd;
    return false;
  }

  conn->rbuf_size += (size_t)rv;
  assert(conn->rbuf_size <= sizeof(conn->rbuf));

  while (TryOneRequest(conn)) {
  }
  return (conn->state == kStateReq);
}

void StateReq(Conn *conn) {
  while (TryFillBuffer(conn)) {
  }
}

bool TryOneRequest(Conn *conn) {
  if (conn->rbuf_size < 4) {
    return false;
  }
  uint32_t len = 0;
  memcpy(&len, &conn->rbuf[0], 4);
  if (len > kMaxMsg) {
    Msg("too long");
    conn->state = kStateEnd;
    return false;
  }
  if (4 + len > conn->rbuf_size) {
    return false;
  }

  std::vector<std::string> cmd;
  if (0 != ParseReq(&conn->rbuf[4], len, cmd)) {
    Msg("bad req");
    conn->state = kStateEnd;
    return false;
  }

  std::string out;
  DoRequest(cmd, out);

  if (4 + out.size() > kMaxMsg) {
    out.clear();
    OutErr(out, kErr2Big, "response is too big");
  }
  uint32_t wlen = (uint32_t)out.size();
  memcpy(&conn->wbuf[0], &wlen, 4);
  memcpy(&conn->wbuf[4], out.data(), out.size());
  conn->wbuf_size = 4 + wlen;

  size_t remain = conn->rbuf_size - 4 - len;
  if (remain) {
    memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
  }
  conn->rbuf_size = remain;

  conn->state = kStateRes;
  StateRes(conn);

  return (conn->state == kStateReq);
}

void StateRes(Conn *conn) {
  while (TryFlushBuffer(conn)) {
  }
}

bool TryFlushBuffer(Conn *conn) {
  ssize_t rv = 0;
  do {
    size_t remain = conn->wbuf_size - conn->wbuf_sent;
    rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
  } while (rv < 0 && errno == EINTR);
  if (rv < 0 && errno == EAGAIN) {
    return false;
  }
  if (rv < 0) {
    std::cout << "write() error\n";
    conn->state = kStateEnd;
    return false;
  }
  conn->wbuf_sent += (size_t)rv;
  assert(conn->wbuf_sent <= conn->wbuf_size);
  if (conn->wbuf_sent == conn->wbuf_size) {
    conn->state = kStateReq;
    conn->wbuf_sent = 0;
    conn->wbuf_size = 0;
    return false;
  }
  return true;
}

int32_t AcceptNewConn(int fd, int epfd) {
  struct sockaddr_in client_addr = {};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    std::cout << "accept() error\n";
    return -1;
  }

  FdSetNb(connfd);
  struct Conn *conn = new Conn();
  conn->fd = connfd;
  conn->state = kStateReq;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;
  conn->idle_start = GetMonotonicUsec();
  DlistInsertBefore(&g_data.idle_list, &conn->idle_list);
  ConnPut(g_data.fd2conn, conn);

  struct epoll_event cev;
  cev.events = EPOLLIN;
  cev.data.ptr = conn;
  epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &cev);
  return 0;
}

uint32_t NextTimerMs() {
  uint64_t now_us = GetMonotonicUsec();
  uint64_t next_us = (uint64_t)-1;

  if (!DlistEmpty(&g_data.idle_list)) {
    Conn *next =
        container_of(g_data.idle_list.next, Conn, idle_list);
    next_us = next->idle_start + kIdleTimeoutMs * 1000;
  }

  if (!g_data.heap.empty() && g_data.heap[0].val < next_us) {
    next_us = g_data.heap[0].val;
  }

  if (next_us == (uint64_t)-1) {
    return 10000;
  }

  if (next_us <= now_us) {
    return 0;
  }
  return (uint32_t)((next_us - now_us) / 1000);
}

void ProcessTimers() {
  uint64_t now_us = GetMonotonicUsec() + 1000;

  while (!DlistEmpty(&g_data.idle_list)) {
    Conn *next =
        container_of(g_data.idle_list.next, Conn, idle_list);
    uint64_t next_us = next->idle_start + kIdleTimeoutMs * 1000;
    if (next_us >= now_us) {
      break;
    }

    std::cout << "removing idle connection: " << next->fd << "\n";
    ConnDone(next);
  }

  const size_t kMaxWorks = 2000;
  size_t nworks = 0;
  while (!g_data.heap.empty() && g_data.heap[0].val < now_us) {
    Entry *ent = container_of(g_data.heap[0].ref, Entry, heap_idx);
    HNode *node = HmPop(&g_data.db, &ent->node, &HnodeSame);
    assert(node == &ent->node);
    EntryDel(ent);
    if (nworks++ >= kMaxWorks) {
      break;
    }
  }
}
