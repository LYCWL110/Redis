#include<iostream>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<netdb.h>
#include<sys/uio.h>
#include<string.h>
#include<assert.h>
#include<vector>
#include<string>
const size_t k_max_msg =  4096;

enum {
    SER_NIL = 0,
    SER_ERR = 1,
    SER_STR = 2,
    SER_INT = 3,
    SER_ARR = 4,
};

static int32_t read_full(int fd, char *buf, size_t n);
static int32_t write_all(int fd, const char *buf, size_t n);
static int32_t on_response(const uint8_t *data, size_t size);

static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
    uint32_t n = cmd.size();
    char body[4 + k_max_msg];
    memcpy(&body[0], &n, 4);
    size_t body_len = 4;
    for (const std::string &s : cmd) {
        uint32_t sz = s.size();
        memcpy(&body[body_len], &sz, 4);
        body_len += 4;
        memcpy(&body[body_len], s.data(), sz);
        body_len += sz;
    }
    char wbuf[4 + k_max_msg];
    memcpy(&wbuf[0], &body_len, 4);
    memcpy(&wbuf[4], body, body_len);
    return write_all(fd, wbuf, 4 + body_len);
}

static int32_t read_res(int fd) {
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            std::cout << "EOF\n";
        } else {
            std::cout << "read() error\n";
        }
        return err;
    }

    uint32_t wlen = 0;
    memcpy(&wlen, rbuf, 4);
    if (wlen > k_max_msg) {
        std::cout << "too long\n";
        return -1;
    }

    err = read_full(fd, &rbuf[4], wlen);
    if (err) {
        std::cout << "read() error\n";
        return err;
    }

    int32_t rv = on_response((const uint8_t *)&rbuf[4], wlen);
    if (rv < 0) {
        std::cout << "bad response\n";
        return -1;
    }
    return 0;
}

static int32_t on_response(const uint8_t *data, size_t size) {
    if (size < 1) {
        std::cout << "bad response\n";
        return -1;
    }
    switch (data[0]) {
        case SER_NIL:
            printf("(nil)\n");
            return 1;
        case SER_ERR:
            if (size < 1 + 8) {
                std::cout << "bad response\n";
                return -1;
            }
            {
                int32_t code = 0;
                uint32_t len = 0;
                memcpy(&code, &data[1], 4);
                memcpy(&len, &data[1 + 4], 4);
                if (size < 1 + 8 + len) {
                    std::cout << "bad response\n";
                    return -1;
                }
                printf("(err) %d %.*s\n", code, len, &data[1 + 8]);
                return 1 + 8 + len;
            }
        case SER_STR:
            if (size < 1 + 4) {
                std::cout << "bad response\n";
                return -1;
            }
            {
                uint32_t len = 0;
                memcpy(&len, &data[1], 4);
                if (size < 1 + 4 + len) {
                    std::cout << "bad response\n";
                    return -1;
                }
                printf("(str) %.*s\n", len, &data[1 + 4]);
                return 1 + 4 + len;
            }
        case SER_INT:
            if (size < 1 + 8) {
                std::cout << "bad response\n";
                return -1;
            }
            {
                int64_t val = 0;
                memcpy(&val, &data[1], 8);
                printf("(int) %ld\n", val);
                return 1 + 8;
            }
        case SER_ARR:
            if (size < 1 + 4) {
                std::cout << "bad response\n";
                return -1;
            }
            {
                uint32_t len = 0;
                memcpy(&len, &data[1], 4);
                printf("(arr) len=%u\n", len);
                size_t arr_bytes = 1 + 4;
                for (uint32_t i = 0; i < len; ++i) {
                    int32_t rv = on_response(&data[arr_bytes], size - arr_bytes);
                    if (rv < 0) {
                        return rv;
                    }
                    arr_bytes += (size_t)rv;
                }
                printf("(arr) end\n");
                return (int32_t)arr_bytes;
            }
        default:
            std::cout << "bad response\n";
            return -1;
    }
}

int main(){
    int clntfd = socket(AF_INET,SOCK_STREAM,0);
    if(clntfd<0){
        std::cout<<"socket() error"<<"\n";
    }
    sockaddr_in clntaddr;
    clntaddr.sin_family = AF_INET;
    clntaddr.sin_port = htons(1234);
    clntaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int rv = connect(clntfd,(struct sockaddr*)&clntaddr,sizeof(clntaddr));
    if(rv){
        std::cout<<"connect"<<"\n";
    }

    // set k1 v1
    {
        int32_t err = send_req(clntfd, {"set", "k1", "v1"});
        if (err) goto L_DONE;
    }
    // get k1
    {
        int32_t err = send_req(clntfd, {"get", "k1"});
        if (err) goto L_DONE;
    }
    // keys
    {
        int32_t err = send_req(clntfd, {"keys"});
        if (err) goto L_DONE;
    }
    // del k1
    {
        int32_t err = send_req(clntfd, {"del", "k1"});
        if (err) goto L_DONE;
    }
    // get k1 after del
    {
        int32_t err = send_req(clntfd, {"get", "k1"});
        if (err) goto L_DONE;
    }

    for (int i = 0; i < 5; ++i) {
        int32_t err = read_res(clntfd);
        if (err) goto L_DONE;
    }

L_DONE:
    close(clntfd);
    return 0;
}

static int32_t read_full(int fd,  char *buf,  size_t n)  {
    while  (n >  0)  {
        ssize_t rv =  read(fd,  buf,  n);
        if  ( rv <=  0)  {
            return  -1;
        }
        assert((size_t)rv <=  n);
        n -=  (size_t)rv;
        buf +=  rv;
    }
    return  0;
}

static int32_t write_all(int fd,  const char *buf,  size_t n)  {
    while  (n >  0)  {
        ssize_t rv =  write(fd,  buf,  n);
        if  ( rv <=  0)  {
            return  -1;
        }
        assert((size_t)rv <=  n);
        n -=  (size_t)rv;
        buf +=  rv;
    }
    return  0;
}
