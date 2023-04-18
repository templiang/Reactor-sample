#pragma once

#include <iostream>
#include <string>
#include "Util.hpp"
using namespace util_ns;

namespace log_ns{
    //日志等级
    enum LogLevel{
        INFO,
        DEBUG,
        WARING,
        ERROR,
        FATAL
    };
    // level:log level
    // fileName:报错文件名称
    // line:错误发生行
    std::ostream& Log(const std::string &level,const std::string &fileName, int line){
        
        //添加日志等级
        std::string message = "[";
        message+=level;
        message+="]";

        //添加报错文件名称
        message+="[";
        message+=fileName;
        message+="]";

        //添加报错行
        message+="[";
        message+=std::to_string(line);
        message+="]";

        //时间戳
        message+="[";
        message+=TimeUtil::get_time_stamp();
        message+="]";

        //cout内部是包含缓冲区的
        std::cout<<message;// 在此将message输入cout缓冲区，不进行endl刷新，等待外部刷新

        return std::cout;
    }
    //调用方式  LOG(INFO)<< "message" <<"\n";  ---> Log(INFO,__FILE__,__LINE__)<<<< "message" <<"\n";
    //开放式日志
    #define LOG(level) Log(#level,__FILE__,__LINE__) // #level 宏用此来传递字符串

}