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

static int32_t read_full(int fd, char *buf, size_t n);
static int32_t write_all(int fd, const char *buf, size_t n);

static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
    uint32_t n = cmd.size();
    // 先构造 body（不含外层总长帧头）
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
    // 包上外层总长帧头
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

    uint32_t rescode = 0;
    memcpy(&rescode, &rbuf[4], 4);
    uint32_t datalen = wlen - 4;
    printf("server says: [%u] %.*s\n", rescode, datalen, &rbuf[8]);
    return 0;
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
    // get k2 (not exist)
    {
        int32_t err = send_req(clntfd, {"get", "k2"});
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
