#include <iostream>
#include <vector>

#ifdef __win__
#include "wepoll/wepoll.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
#endif

constexpr int MAXSIZE = 1024;

SOCKET CreateSocket() {
    return socket(AF_INET, SOCK_STREAM, 0);
}

bool SocketNoBlocking(SOCKET sock) {
    unsigned long flag = 1;
    auto ret = ioctlsocket(sock, FIONBIO, &flag);
    if (ret == SOCKET_ERROR) {
        std::cerr << "set socket noblock failed. error: " << GetLastError() << std::endl;
        return false;
    }
    return true;
}

bool Bind(SOCKET sock, const std::string &ip, uint16_t port) {
    SOCKADDR_IN addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.S_un.S_addr = inet_addr(ip.c_str());
    auto ret = bind(sock, (sockaddr*)&addr, sizeof(addr));
    return ret == 0;
}

bool Listen(SOCKET sock) {
    auto ret = listen(sock, SOMAXCONN);
    if (ret == SOCKET_ERROR) {
        std::cerr << "listen failed. error: " << GetLastError() << std::endl;
        return false;
    }

    return true;
}

SOCKET BindAndListen(const std::string &ip, uint16_t port) {
    auto sock = CreateSocket();
    if (sock == INVALID_SOCKET) {
        std::cerr << "create socket failed. error: " << GetLastError() << std::endl;
        return -1;
    }

    if (!Bind(sock, ip, port) || !Listen(sock)) {
        closesocket(sock);
        return -1;
    }

    return sock;
}

void AddEvent(HANDLE epollBase, SOCKET sock, int event) {
    struct epoll_event ev{};
    ev.events = event;
    ev.data.fd = static_cast<int>(sock);
    epoll_ctl(epollBase, EPOLL_CTL_ADD, sock, &ev);
}

void ModEvent(HANDLE epollBase, SOCKET sock, int event) {
    struct epoll_event ev{};
    ev.events = event;
    ev.data.fd = static_cast<int>(sock);
    epoll_ctl(epollBase, EPOLL_CTL_MOD, sock, &ev);
}

void DelEvent(HANDLE epollBase, SOCKET sock) {
    epoll_ctl(epollBase, EPOLL_CTL_DEL, sock, nullptr);
}

int32_t Loop(HANDLE epollBase, std::vector<epoll_event> &activeList, int32_t waitTime) {
    int32_t ret = epoll_wait(epollBase, &*activeList.begin(), static_cast<int>(activeList.size()), waitTime);
    if (ret == -1) {
        if (errno == EINTR) {
            return 0;
        }
        std::cout << "epoll error: " << errno << std::endl;
    }
    return ret;
}

void DoAccept(HANDLE epollBase, SOCKET listenSock) {
    struct sockaddr addr{};
    socklen_t len = sizeof(addr);
    auto fd = accept(listenSock, &addr, &len);
    if (fd < 0) {
        std::cerr << "accept failed. error: " << errno << std::endl;
        return;
    }

    SocketNoBlocking(fd);

    AddEvent(epollBase, fd, EPOLLIN);
}

void DoRead(HANDLE epollBase, SOCKET sock, char *buf) {
    std::cout << "on read event" << std::endl;
    // 获取数据
    int readCount = recv(sock, buf, MAXSIZE, 0);
    std::cout << "read count : " << readCount << std::endl;
    if (readCount < 0) {
        std::cerr << "read error." << std::endl;
        closesocket(sock);
        DelEvent(epollBase, sock);
    } else if (readCount == 0) {
        std::cout << "client close." << std::endl;
        closesocket(sock);
        DelEvent(epollBase, sock);
    } else {
        // echo
        std::cout << "client message is: " << buf << std::endl;
        int writeCount = send(sock, buf, strlen(buf), 0);
        if (writeCount < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
                std::cout << "wait next write event." << std::endl;
                ModEvent(epollBase, sock, EPOLLOUT);
                return;
            } else {
                std::cerr << "write failed. err: " << errno << std::endl;
                closesocket(sock);
                DelEvent(epollBase, sock);
            }
        }

        memset(buf,0,MAXSIZE);
    }
}

void DoWrite(HANDLE epollBase, SOCKET sock, char *buf) {
    std::cout << "on write event." << std::endl;

    int writeCount = send(sock, buf, strlen(buf), 0);
    if (writeCount < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
            std::cout << "wait next write event." << std::endl;
        } else {
            std::cerr << "write failed. err: " << errno << std::endl;
            closesocket(sock);
            DelEvent(epollBase, sock);
        }
    } else {
        std::cout << "echo message :" << buf << " success. " << std::endl;
        ModEvent(epollBase, sock, EPOLLIN);
    }

    memset(buf,0,MAXSIZE);
}

int main() {
    HANDLE epollBase = epoll_create(1024);
    if (epollBase == nullptr) {
        std::cerr << "create epoll failed. error: " << errno << std::endl;
        return 0;
    }

    auto listenSock = BindAndListen("0.0.0.0", 8546);
    if (listenSock <= 0) {
        return 0;
    }

    AddEvent(epollBase, listenSock, EPOLLIN);

    std::vector<epoll_event> activeList;
    activeList.resize(1024);
    int32_t activeCount;
    char buf[MAXSIZE];
    memset(buf,0,MAXSIZE);

    while (true) {
        activeCount = Loop(epollBase, activeList, -1);
        if (activeCount <= 0) {
            continue;
        }
        int clientSock;
        for (auto i = 0; i < activeCount; ++i) {
            clientSock = activeList[i].data.fd;
            if (clientSock == listenSock) {
                DoAccept(epollBase, listenSock);
            } else {
                if (activeList[i].events & EPOLLIN) {
                    DoRead(epollBase, clientSock, buf);
                }
                if (activeList[i].events & EPOLLOUT) {
                    DoWrite(epollBase, clientSock, buf);
                }
            }
        }
    }
}
