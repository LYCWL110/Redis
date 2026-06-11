#include<iostream>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<netdb.h>
#include<sys/uio.h>
#include<cstring>
#include<assert.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<errno.h>
#include<vector>

const size_t k_max_msg =  4096;

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,
};

struct Conn {
    int fd = -1;
    uint32_t state = STATE_REQ;
    // 读缓冲区
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
    // 写缓冲区
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

static void fd_set_nb(int fd) {
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

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static void state_req(Conn *conn);
static void state_res(Conn *conn);
static bool try_fill_buffer(Conn *conn);
static bool try_one_request(Conn *conn);
static bool try_flush_buffer(Conn *conn);

static void connection_io(Conn *conn) {
    if (conn->state == STATE_REQ) {
        state_req(conn);
    } else if (conn->state == STATE_RES) {
        state_res(conn);
    } else {
        assert(0);
    }
}

static bool try_fill_buffer(Conn *conn) {
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
        conn->state = STATE_END;
        return false;
    }
    if (rv == 0) {
        if (conn->rbuf_size > 0) {
            std::cout << "unexpected EOF\n";
        } else {
            std::cout << "EOF\n";
        }
        conn->state = STATE_END;
        return false;
    }

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    while (try_one_request(conn)) {}
    return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn) {
    while (try_fill_buffer(conn)) {}
}

static bool try_one_request(Conn *conn) {
    if (conn->rbuf_size < 4) {
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg) {
        std::cout << "too long\n";
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        return false;
    }

    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    // 回显响应
    memcpy(&conn->wbuf[0], &len, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;

    // 从缓冲区移除这个请求
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    conn->state = STATE_RES;
    state_res(conn);

    return (conn->state == STATE_REQ);
}

static void state_res(Conn *conn) {
    while (try_flush_buffer(conn)) {}
}

static bool try_flush_buffer(Conn *conn) {
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
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size) {
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    return true;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd, int epfd) {
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
        std::cout << "accept() error\n";
        return -1;
    }

    fd_set_nb(connfd);
    struct Conn *conn = new Conn();
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);

    struct epoll_event cev;
    cev.events = EPOLLIN;
    cev.data.ptr = conn;
    epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &cev);
    return 0;
}

int main(){
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

    int rv = bind(fd, (const sockaddr*)&addrr, sizeof(addrr));
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

    fd_set_nb(fd);

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        std::cout << "epoll_create1() failed\n";
        return 1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

    const int k_max_events = 64;
    struct epoll_event events[k_max_events];
    std::vector<Conn *> fd2conn;

    while (true) {
        int nfds = epoll_wait(epfd, events, k_max_events, -1);
        if (nfds < 0) {
            std::cout << "epoll_wait() error\n";
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == fd) {
                while (true) {
                    int32_t err = accept_new_conn(fd2conn, fd, epfd);
                    if (err) {
                        break;
                    }
                }
            } else {
                Conn *conn = (Conn *)events[i].data.ptr;
                if (events[i].events & (EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLERR)) {
                    connection_io(conn);
                    if (conn->state == STATE_END) {
                        fd2conn[conn->fd] = NULL;
                        epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, nullptr);
                        close(conn->fd);
                        delete conn;
                    } else {
                        struct epoll_event cev;
                        cev.data.ptr = conn;
                        cev.events = (conn->state == STATE_REQ) ? EPOLLIN : EPOLLOUT;
                        epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &cev);
                    }
                }
            }
        }
    }

    close(fd);
    return 0;
}
