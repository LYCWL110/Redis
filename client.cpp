#include<iostream>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<netdb.h>
#include<sys/uio.h>
#include<string.h>
#include<assert.h>
const size_t k_max_msg =  4096;
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
   int32_t err = query(clntfd, "hello");
    if (err) {
        std::cout << "query failed\n";
    }
    
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

static int32_t query(int fd,  const char *text)  {
    uint32_t len =  (uint32_t)strlen(text);
    if  (len >  k_max_msg)  {
        return  -1;
    }

    char wbuf[4 +  k_max_msg];
    memcpy(wbuf,  &len,  4);   
    memcpy(&wbuf[4],  text,  len);
    if  (int32_t err =  write_all(fd,  wbuf,  4 +  len))  {
        return  err;
    }

    char rbuf[4 +  k_max_msg +  1];
    errno =  0;
    int32_t err =  read_full(fd,  rbuf,  4);
    if  (err)  {
        if  (errno ==  0)  {
            std::cout<<("EOF")<<"\n";
        }  else  {
            std::cout<<("read() error")<<"\n";
        }
        return  err;
    }

    memcpy(&len,  rbuf,  4);   
    if  (len >  k_max_msg)  {
        std::cout<<("too long")<<"\n";
        return  -1;
    }

    err =  read_full(fd,  &rbuf[4],  len);
    if  (err)  {
        std::cout<<("read() error")<<"\n";
        return  err;
    }
    rbuf[4 +  len]  =   '\0';
    printf("server says: %s\n",  &rbuf[4]);
    return  0;
}
