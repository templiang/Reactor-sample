#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <string>
#include <unordered_map>
#include <string.h>
#include "Log.hpp"
using namespace log_ns;

const int SIZE = 1024;
const int EVTS_SIZE = 128;
class Event;
class Reactor;
typedef int (*callback_t)(Event *evt);

class Event
{
public:
    int sock_fd_;        // 所关注的fd
    epoll_event ep_evt_; // 所关注的事件
    // 为每个fd开辟单独的输入输出缓冲区
    std::string in_buffer_;
    std::string out_buffer_;
    // 回调函数
    callback_t recver_;
    callback_t sender_;
    callback_t errorer_;

    // 回指所属得Reactor的指针
    Reactor *R;
    //
    Event() : sock_fd_(-1), recver_(nullptr), sender_(nullptr), errorer_(nullptr), R(nullptr)
    {
    }
    Event(int sock_fd, epoll_event ep_evt, callback_t recver, callback_t sender, callback_t errorer, Reactor *r)
        : sock_fd_(sock_fd),
          ep_evt_(ep_evt),
          recver_(recver),
          sender_(sender),
          errorer_(errorer),
          R(r)
    {
    }
    // 注册回调函数
    void register_callback(callback_t recver, callback_t sender, callback_t errorer)
    {
        recver_ = recver;
        sender_ = sender;
        errorer_ = errorer;
    }
};

// 对epoll进一步封装。无需关心sock的类型，只负责对Event进行管理
class Reactor
{
private:
    int epfd_;
    // 管理所有需要关注的fd和相应得Event
    std::unordered_map<int, Event *> evts_;

public:
    Reactor()
    {
        epfd_ = -1;
    }
    ~Reactor()
    {
    }

    bool init_reactor()
    {
        epfd_ = epoll_create(128);
        if (epfd_ < 0)
        {
            std::cerr << "创建epoll instance失败" << std::endl;
            return false;
        }
        std::cout << "初始化Reactor成功" << std::endl;
        return true;
    }
    // 将fd和相应的需要关注的事件注册到内核。同时相应的Event交由Reactor管理
    bool insert_event(Event *evt)
    {
        int ret = 0;
        // 将fd和相应的需要关注的事件注册到内核
        if ((ret = epoll_ctl(epfd_, EPOLL_CTL_ADD, evt->sock_fd_, &evt->ep_evt_)) < 0)
        {
            std::cerr << "epoll_ctl添加事件失败,ret=" << ret << " errno=" << errno << " " << strerror(errno) << std::endl;
            return false;
        }

        // 将相应的Event插入到map中交由Reactor管理
        evts_.insert({evt->sock_fd_, evt});
        return true;
    }

    bool insert_event(int sock_fd, uint32_t evt_flag,
                      callback_t cb0 /*读事件回调*/,
                      callback_t cb1 /*写事件回调*/,
                      callback_t cb2 /*错误事件回调*/)
    {
        int ret = 0;
        struct epoll_event ep_evt;
        ep_evt.events = evt_flag;
        ep_evt.data.fd = sock_fd;
        // 将fd和相应的需要关注的事件注册到内核
        if ((ret = epoll_ctl(epfd_, EPOLL_CTL_ADD, sock_fd, &ep_evt)) < 0)
        {
            std::cerr << "epoll_ctl添加事件失败,ret=" << ret << " errno=" << errno << " " << strerror(errno) << std::endl;
            return false;
        }

        // !!!!evt的生命周期在此函数内，当出函数作用于后evt资源被释放，导致{sock_fd,evt}内的evt成为了野指针
        // Event evt(sock_fd,ep_evt,cb0,cb1,cb2,this); ---> Event *evt=new Event(sock_fd,ep_evt,cb0,cb1,cb2,this);
        
        // 将相应的Event插入到Reactor的map中管理
        Event *evt=new Event(sock_fd,ep_evt,cb0,cb1,cb2,this);
        evts_.insert({sock_fd,evt});
        return true;
    }

    bool mod_event(int sock_fd,uint32_t evt_flag){
        struct epoll_event ep_evt;
        ep_evt.events=evt_flag;
        ep_evt.data.fd = sock_fd;
        if(epoll_ctl(epfd_,EPOLL_CTL_MOD,sock_fd,&ep_evt)<0){
            LOG(WARING)<<"epoll_ctl modify failed"<<std::endl;
            return false;
        }
        
        //evts_[sock_fd]->ep_evt_ = ep_evt;

        auto iter = evts_.find(sock_fd);
        if(iter==evts_.end()){
            return false;
        }
        iter->second->ep_evt_ = ep_evt;
        return true;
    }

    // 取消关注fd的事件，并从Reactor中删除
    void delete_event(int fd)
    {
        auto iter = evts_.find(fd);
        if (iter == evts_.end())
        {
            return;
        }
        // 1.从epoll中删除
        epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
        // 2.从map中删除
        Event *temp=iter->second;
        evts_.erase(iter);
        // 3.关闭文件描述符
        close(fd);
        // 4.清理fd对应的Event对象
        delete temp;
    }
    // 就绪事件派发器
    void dispatcher(int timeout)
    {
        // 接收就绪队列
        struct epoll_event revts[EVTS_SIZE];
        int n = epoll_wait(epfd_, revts, EVTS_SIZE, timeout);
        //LOG(DEBUG)<<"就绪事件数:"<<n<<std::endl;
        // 将就绪事件逐一派发
        for (int i = 0; i < n; ++i)
        {
            int sock_fd = revts[i].data.fd;
            uint32_t revt_flag = revts[i].events;
            LOG(DEBUG)<<"就绪事件:fd="<<sock_fd<<" "<<revt_flag<<std::endl;
            // 差错处理。将所有的错误问题全部转化IO事件交由IO函数处理
            if (revt_flag & EPOLLERR)
                revt_flag |= (EPOLLIN | EPOLLOUT);
            if (revt_flag & EPOLLHUP)
                revt_flag |= (EPOLLIN | EPOLLOUT);

            // 读时间就绪,交由fd相应的回调函数处理
            if (revt_flag & EPOLLIN)
            {
                auto iter = evts_.find(sock_fd);
                if (iter != evts_.end())
                {
                    // 读事件回调函数是否注册
                    if (iter->second->recver_!=nullptr)
                    {
                        Event *evt = iter->second;
                        iter->second->recver_(evt);
                    }
                }
            }

            // 写事件就绪
            if (revt_flag & EPOLLOUT)
            {
                auto iter = evts_.find(sock_fd);
                if (iter != evts_.end())
                {
                    // 写事件回调函数是否注册
                    if (iter->second->sender_!=nullptr)
                    {
                        Event *evt = iter->second;
                        iter->second->sender_(evt);
                    }
                }
            }
        }
    }
};
