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
static int32_t one_request(int connfd);
static int32_t read_full(int fd,  char *buf,  size_t n);
static int32_t write_all(int fd,  const char *buf,  size_t n);
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

    while (true) {
        int nfds = epoll_wait(epfd, events, k_max_events, -1);
        if (nfds < 0) {
            std::cout << "epoll_wait() error\n";
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == fd) {
                while (true) {
                    sockaddr_in client_addr;
                    socklen_t clnt_addrlen = sizeof(client_addr);
                    int clientfd = accept(fd, (struct sockaddr*)&client_addr, &clnt_addrlen);
                    if (clientfd < 0) {
                        break;
                    }
                    fd_set_nb(clientfd);

                    struct epoll_event cev;
                    cev.events = EPOLLIN;
                    cev.data.fd = clientfd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &cev);
                }
            } else {
                if (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
                    int32_t err = one_request(events[i].data.fd);
                    if (err) {
                        epoll_ctl(epfd, EPOLL_CTL_DEL, events[i].data.fd, nullptr);
                        close(events[i].data.fd);
                    }
                }
            }
        }
    }

    close(fd);
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



static int32_t one_request(int connfd)  {
    char rbuf[4 +  k_max_msg +  1];
    errno =  0;
    int32_t err =  read_full(connfd,  rbuf,  4);
    if  (err)  {
        if  (errno ==  0)  {
            std::cout<<("EOF")<<"\n";
        }  else  {
            std::cout<<("read() error")<<"\n";
        }
        return  err;
    }

    uint32_t len =  0;
    memcpy(&len,  rbuf,  4);   
    if  (len >  k_max_msg)  {
        std::cout<<("too long")<<"\n";
        return  -1;
    }

    err =  read_full(connfd,  &rbuf[4],  len);
    if  (err)  {
        std::cout<<("read() error")<<"\n";
        return  err;
    }

    rbuf[4 +  len]  =   '\0';
    printf("client says: %s\n",  &rbuf[4]);

 
    const char reply[]  =  "world";
    char wbuf[4 +  sizeof(reply)];
    len =  (uint32_t)strlen(reply);

    memcpy(wbuf,  &len,  4);
    memcpy(&wbuf[4],  reply,  len);
    return  write_all(connfd,  wbuf,  4 +  len);
}
