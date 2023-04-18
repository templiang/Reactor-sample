#pragma once

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <memory>
#include <json/json.h>

namespace util_ns{
    class FdUtil
    {
    public:
        static void set_non_block(int sock_fd){
            int flags = fcntl(sock_fd, F_GETFL, 0);
            flags |= O_NONBLOCK;
            fcntl(sock_fd, F_SETFL, flags);
        }
    };
    class TimeUtil
    {
    public:
        static std::string get_time_stamp()
        {
            struct timeval time;
            gettimeofday(&time, nullptr);
            return std::to_string(time.tv_sec);
        }

        // 获取毫秒级时间戳
        static std::string get_time_stamp_ms()
        {
            struct timeval time;
            gettimeofday(&time, nullptr);
            return std::to_string(time.tv_sec * 1000 + time.tv_usec / 1000);
        }
    };

    class JsonUtil
    {
    public:
        // value:需要序列化的Value值
        // str:输出型参数
        static void serialization(const Json::Value &inValue, std::string &outputJson)
        {
            std::ostringstream os;
            Json::StreamWriterBuilder writerBuilder;
            std::unique_ptr<Json::StreamWriter> jsonWriter(writerBuilder.newStreamWriter());
            jsonWriter->write(inValue, &os);
            outputJson = os.str();
        }
        /*
            inputJson:需要反序列化的json字符串
            outValue:输出型参数。存储反序列化后的Value
        */
        static bool deserialization(const std::string &inputJson, Json::Value &outValue)
        {
            JSONCPP_STRING errs;
            Json::CharReaderBuilder readerBuilder;
            std::unique_ptr<Json::CharReader> const jsonReader(readerBuilder.newCharReader());

            if (!errs.empty() || !jsonReader->parse(inputJson.c_str(),
                                                    inputJson.c_str() + inputJson.length(),
                                                    &outValue, &errs))
            {
                return false;
            }

            return true;
        }
    };

    class StringUtil
    {
    public:

        /*
            str:待切分的字符串
            target:输出型参数，切分后存于此处
            separator:分隔符
        */
        static void split_string(std::string &str, std::vector<std::string> &target, const std::string &separator)
        {
            target.clear();
            std::string token; // 用于存储每个子字符串

            while(1){
                size_t s_pos = str.find(separator);
                std::string body;
                int content_length=0;
                if(s_pos == std::string::npos){
                    break;
                }
                token = str.substr(0,s_pos);
                target.push_back(token);
                token.clear();
                str.erase(0,s_pos+separator.size());
            }
        }
    
        /*
            str:待切分的字符串
            target:输出型参数，切分后存于此处
            separator:分隔符
        */
        static void receive_http_body(std::string &str, std::vector<std::string> &target, const std::string &separator)
        {
            target.clear();

            std::string token; // 用于存储每个子字符串

            while (1)
            {
                // 1. 提取http协议报头
                size_t s_pos = str.find(separator);
                std::string body;
                int content_length=0;
                if(s_pos == std::string::npos){
                    break;
                }
                std::string header = str.substr(0,s_pos);//http报头
                // 解析 HTTP 消息头中的 Content-Length 字段，并根据该字段确定正文的长度。
                size_t pos = header.find("Content-Length: ");
                if (pos != std::string::npos) {
                    pos += strlen("Content-Length: ");
                    auto endpos = header.find("\r\n", pos);
                    content_length = std::stoi(header.substr(pos, endpos - pos));
                }

                // 数据是否构成一个完整的http报文
                if(str.size()>=s_pos+separator.size()+content_length){
                    body+=str.substr(0,s_pos+separator.size()+content_length);
                    str.erase(0,s_pos+separator.size()+content_length);
                    target.push_back(body);
                }
                else{
                    // 无法构成一个完整报文，留在缓冲区
                    break;
                }
            }
            // 对于http协议,最后一个子字符串未构成一个完整报文，我们不处理
            // if (!str.empty())
            // {                            
            //     target.push_back(token); 
            // }
        }
    };
}