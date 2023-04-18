#pragma once

#include "Reactor.hpp"
#include <vector>
#include <string>
#include <iostream>
// #include <cerrno>

const int ONCE_SIZE = 1024;

// 链接管理器，负责处理listen_fd上的链接请求。
int accepter(Event *evt);
int recver(Event *evt);
int sender(Event *evt);
int errorer(Event *evt);

int accepter(Event *evt)
{
    // 仅为listen_fd所属的Event注册了accepter函数，因此调用此函数的fd一定为Listen_fd
    struct sockaddr_in addr;
    uint32_t addr_len = sizeof(addr);
    // ET模式非阻塞
    while (1)
    {
        // 1.accept建立链接
        int new_sock_fd = accept(evt->sock_fd_, (sockaddr *)&addr, &addr_len);
        if (new_sock_fd < 0)
        {
            break;
        }
        // 2.将新链接的fd添加到Reactor
        FdUtil::set_non_block(new_sock_fd);
        // 写事件基本一直是就绪的，但我们不确定用户是不是就绪的，因此，我们不能在此关注写事件，否则会导致epoll一直被该写事件唤醒。我们在构建响应时按需设置
        evt->R->insert_event(new_sock_fd, EPOLLIN | EPOLLET, recver, sender, nullptr);
        LOG(INFO) << "新的链接已建立,fd=" << new_sock_fd << std::endl;
    }
    return 0;
}

/*
    return:
        1 读取完成
       -1 读取出错
        0 对端关闭链接
*/
static int recver_from_core(int sock_fd, std::string &in_buffer)
{
    while (1)
    {
        char buffer[ONCE_SIZE];
        int ret = recv(sock_fd, buffer, ONCE_SIZE - 1, 0);
        // recv return:When a stream socket peer has performed an orderly shutdown, the return value will be 0
        if (ret > 0)
        {
            // 读取成功
            buffer[ret] = '\0';
            in_buffer += buffer;
        }
        else if (ret < 0)
        {
            // EINTR  The receive was interrupted by delivery of a signal before any data were available
            if (errno == EINTR)
            {
                // 1. IO被信号中断
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 2. 底层数据已读完,读取完成
                return 1; // success
            }

            // 3. 读取出错
            return -1;
        }
        else
        {
            // ret == 0 对端关闭链接
            return 0;
        }
    }
}

static void split_string(std::string &str, std::vector<std::string> &target, const std::string &separator)
{
    LOG(INFO) << __func__<< std::endl;
    target.clear();
    std::string token; // 用于存储每个子字符串

    while (1)
    {
        size_t pos = str.find(separator);
        if (pos == std::string::npos)
        {
            break;
        }
        token = str.substr(0, pos);
        LOG(INFO) << __func__<<":"<<token<<std::endl;
        target.push_back(token);
        token.clear();
        str.erase(0, pos + separator.size());
    }
}
static void receive_http_body(std::string &str, std::vector<std::string> &target, const std::string &separator)
{
    target.clear();

    std::string token; // 用于存储每个子字符串

    while (1)
    {
        // 1. 提取http协议报头
        size_t s_pos = str.find(separator);
        std::string body;
        int content_length = 0;
        if (s_pos == std::string::npos)
        {
            break;
        }
        std::string header = str.substr(0, s_pos); // http报头
        // 解析 HTTP 消息头中的 Content-Length 字段，并根据该字段确定正文的长度。
        size_t pos = header.find("Content-Length: ");
        if (pos != std::string::npos)
        {
            pos += strlen("Content-Length: ");
            auto endpos = header.find("\r\n", pos);
            content_length = std::stoi(header.substr(pos, endpos - pos));
        }

        // 数据是否构成一个完整的http报文
        if (str.size() >= s_pos + separator.size() + content_length)
        {
            body += str.substr(0, s_pos + separator.size() + content_length);
            str.erase(0, s_pos + separator.size() + content_length);
            target.push_back(body);
        }
        else
        {
            // 无法构成一个完整报文，留在缓冲区
            break;
        }
    }
}

/*
    recver，读事件回调函数，包含真正的读取
    步骤：
        1. 读取到缓冲区
        2. 分包，解决粘包问题
        3. 反序列化，针对一个完整报文，提取有效信息
        4. 业务逻辑
        5. 构建响应
*/
int recver(Event *evt)
{
    LOG(INFO) << "fd:" << evt->sock_fd_ << " call recver" << std::endl;
    // 1.读取
    int ret = recver_from_core(evt->sock_fd_, evt->in_buffer_);
    if (ret <= 0)
    {
        // 差错处理
        if (evt->errorer_ != nullptr)
        {
            evt->errorer_(evt);
        }
        return -1;
    }
    // 2.分包
    std::vector<std::string> tokens;
    split_string(evt->in_buffer_, tokens, "\n");
    LOG(INFO) << __func__<< std::endl;
    // 3.反序列化
    for (const auto &t : tokens)
    {
        // 4.业务逻辑 TODO...
        std::cout<<"content:"<<t<<std::endl;
        // 5.构建响应,写入缓冲区，等待写就绪
        std::string res = "ok\n";
        res += "\n";
        evt->out_buffer_ += res;
    }

    // 6.发送。在发送前，我们需等待写事件就绪才能发送（写事件基本一直是就绪的，但我们不确定用户是不是就绪的）
    if (!evt->out_buffer_.empty())
    {
        // 写事件打开的时候，默认就是就绪的。EPOLL 中只要用户重新设置了OUT事件，EPOLLOUT至少会触发一次
        LOG(DEBUG) << "Add EPOLLOUT" << std::endl;
        evt->R->mod_event(evt->sock_fd_, EPOLLIN | EPOLLOUT | EPOLLET);
    }

    return 0;
}

/*
    return：
        1 发送完成
        0 未完全发送完成，但不能再发了
       -1 发送失败
*/
int send_to_core(int sock_fd, std::string &out_buffer)
{
    int total = 0; // 本轮累计发送的数据
    const char *start = out_buffer.c_str();
    int size = out_buffer.size();

    while (1)
    {
        int ret = send(sock_fd, start + total, size - total, 0);
        if (ret > 0)
        {
            total += ret;
            if (total == size)
            {
                // 缓冲区内容已全部发送完成
                out_buffer.clear();
                return 1;
            }
        }
        else
        {
            // 1.数据发送被信号中断
            if (errno == EINTR)
            {

                continue;
            }
            // 2.内核缓冲区已满，数据尚未发完，不能继续再发
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 将已经发送的清理掉
                out_buffer.erase(0, total);
                return 0;
            }

            // 3.发送错误，现不做区分
            return -1;
        }
    }
}
/*
    sender,写事件回调
    步骤:
        1.
*/
int sender(Event *evt)
{
    LOG(INFO) << "fd:" << evt->sock_fd_ << " call sender" << std::endl;
    int ret = send_to_core(evt->sock_fd_, evt->out_buffer_);

    if (ret == 1)
    {
        // 发送完成，不再关注写事件
        evt->R->mod_event(evt->sock_fd_, EPOLLIN | EPOLLET);
    }
    else if (ret == 0)
    {
        // 未完全发送完成，继续关注写事件
        // do nothing
    }
    else
    {
        // 发送失败,差错处理
        if (evt->errorer_ != nullptr)
        {
            evt->errorer_(evt);
        }
    }
}

int errorer(Event *evt)
{
    LOG(INFO) << "fd:" << evt->sock_fd_ << " call errorer" << std::endl;
    evt->R->delete_event(evt->sock_fd_);
}