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
#include<cstdlib>
#include<cstddef>
#include<string>

const size_t k_max_msg =  4096;
const size_t k_max_args = 1024;

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
struct HNode;

static int32_t do_request(const uint8_t *req, uint32_t reqlen,
        uint32_t *rescode, uint8_t *res, uint32_t *reslen);
static int32_t parse_req(const uint8_t *data, size_t len,
        std::vector<std::string> &out);
static bool entry_eq(HNode *lhs, HNode *rhs);
static uint64_t str_hash(const uint8_t *data, size_t len);
static uint32_t do_get(std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen);
static uint32_t do_set(std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen);
static uint32_t do_del(std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen);

static void msg(const char *s) {
    std::cout << s << "\n";
}

static bool cmd_is(const std::string &word, const char *cmd) {
    return word == cmd;
}

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
        msg("too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        return false;
    }

    uint32_t rescode = 0;
    uint32_t wlen = 0;
    int32_t err = do_request(
        &conn->rbuf[4], len,
        &rescode, &conn->wbuf[4 + 4], &wlen
    );
    if (err) {
        conn->state = STATE_END;
        return false;
    }
    wlen += 4;
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], &rescode, 4);
    conn->wbuf_size = 4 + wlen;

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

enum {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

#define container_of(ptr, type, member) ({                              \
    const decltype(((type *)0)->member) *mptr = (ptr);                  \
    (type *)((char *)mptr - offsetof(type, member));                    \
})

static uint64_t str_hash(const uint8_t *data, size_t len) {
    uint64_t h = 0x811C9DC5;
    for (size_t i = 0; i < len; i++) {
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}

struct HNode {
    HNode *next = NULL;
    uint64_t hcode = 0;
};

struct HTab {
    HNode **tab = NULL;
    size_t mask = 0;
    size_t size = 0;
};

struct HMap {
    HTab ht1;
    HTab ht2;
    size_t resizing_pos = 0;
};

static void h_init(HTab *htab, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);
    htab->tab = (HNode **)calloc(n, sizeof(HNode *));
    htab->mask = n - 1;
    htab->size = 0;
}

static void h_insert(HTab *htab, HNode *node) {
    size_t pos = node->hcode & htab->mask;
    HNode *next = htab->tab[pos];
    node->next = next;
    htab->tab[pos] = node;
    htab->size++;
}

static HNode **h_lookup(
    HTab *htab, HNode *key, bool (*cmp)(HNode *, HNode *))
{
    if (!htab->tab) {
        return NULL;
    }

    size_t pos = key->hcode & htab->mask;
    HNode **from = &htab->tab[pos];
    while (*from) {
        if (cmp(*from, key)) {
            return from;
        }
        from = &(*from)->next;
    }
    return NULL;
}

static HNode *h_detach(HTab *htab, HNode **from) {
    HNode *node = *from;
    *from = (*from)->next;
    htab->size--;
    return node;
}

const size_t k_resizing_work = 128;
const size_t k_max_load_factor = 8;

static void hm_start_resizing(HMap *hmap) {
    assert(hmap->ht2.tab == NULL);
    hmap->ht2 = hmap->ht1;
    h_init(&hmap->ht1, (hmap->ht1.mask + 1) * 2);
    hmap->resizing_pos = 0;
}

static void hm_help_resizing(HMap *hmap) {
    if (hmap->ht2.tab == NULL) {
        return;
    }

    size_t nwork = 0;
    while (nwork < k_resizing_work && hmap->ht2.size > 0) {
        HNode **from = &hmap->ht2.tab[hmap->resizing_pos];
        if (!*from) {
            hmap->resizing_pos++;
            continue;
        }

        h_insert(&hmap->ht1, h_detach(&hmap->ht2, from));
        nwork++;
    }

    if (hmap->ht2.size == 0) {
        free(hmap->ht2.tab);
        hmap->ht2 = HTab{};
    }
}

HNode *hm_lookup(
    HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *))
{
    hm_help_resizing(hmap);
    HNode **from = h_lookup(&hmap->ht1, key, cmp);
    if (!from) {
        from = h_lookup(&hmap->ht2, key, cmp);
    }
    return from ? *from : NULL;
}

void hm_insert(HMap *hmap, HNode *node) {
    if (!hmap->ht1.tab) {
        h_init(&hmap->ht1, 4);
    }

    h_insert(&hmap->ht1, node);

    if (!hmap->ht2.tab) {
        size_t load_factor = hmap->ht1.size / (hmap->ht1.mask + 1);
        if (load_factor >= k_max_load_factor) {
            hm_start_resizing(hmap);
        }
    }
    hm_help_resizing(hmap);
}

HNode *hm_pop(
    HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *))
{
    hm_help_resizing(hmap);
    HNode **from = h_lookup(&hmap->ht1, key, cmp);
    if (from) {
        return h_detach(&hmap->ht1, from);
    }
    from = h_lookup(&hmap->ht2, key, cmp);
    if (from) {
        return h_detach(&hmap->ht2, from);
    }
    return NULL;
}

struct Entry {
    struct HNode node;
    std::string key;
    std::string val;
};

static struct {
    HMap db;
} g_data;

static bool entry_eq(HNode *lhs, HNode *rhs) {
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return lhs->hcode == rhs->hcode && le->key == re->key;
}

static uint32_t do_get(
    std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (!node) {
        return RES_NX;
    }

    const std::string &val = container_of(node, Entry, node)->val;
    assert(val.size() <= k_max_msg);
    memcpy(res, val.data(), val.size());
    *reslen = (uint32_t)val.size();
    return RES_OK;
}

static uint32_t do_set(
    std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    (void)res;
    (void)reslen;

    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (node) {
        container_of(node, Entry, node)->val.swap(cmd[2]);
    } else {
        Entry *ent = new Entry();
        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        ent->val.swap(cmd[2]);
        hm_insert(&g_data.db, &ent->node);
    }
    return RES_OK;
}

static uint32_t do_del(
    std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    (void)res;
    (void)reslen;

    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
    if (node) {
        delete container_of(node, Entry, node);
    }
    return RES_OK;
}

static int32_t do_request(
    const uint8_t *req, uint32_t reqlen,
    uint32_t *rescode, uint8_t *res, uint32_t *reslen)
{
    std::vector<std::string> cmd;
    if (0 != parse_req(req, reqlen, cmd)) {
        msg("bad req");
        return -1;
    }
    if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
        *rescode = do_get(cmd, res, reslen);
    } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
        *rescode = do_set(cmd, res, reslen);
    } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
        *rescode = do_del(cmd, res, reslen);
    } else {
        *rescode = RES_ERR;
        const char *errmsg = "Unknown cmd";
        strcpy((char *)res, errmsg);
        *reslen = strlen(errmsg);
        return 0;
    }
    return 0;
}

static int32_t parse_req(
    const uint8_t *data, size_t len, std::vector<std::string> &out)
{
    if (len < 4) {
        return -1;
    }
    uint32_t n = 0;
    memcpy(&n, &data[0], 4);
    if (n > k_max_args) {
        return -1;
    }

    size_t pos = 4;
    while (n--) {
        if (pos + 4 > len) {
            return -1;
        }
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if (pos + 4 + sz > len) {
            return -1;
        }
        out.push_back(std::string((char *)&data[pos + 4], sz));
        pos += 4 + sz;
    }

    if (pos != len) {
        return -1;
    }
    return 0;
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
