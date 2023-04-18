#include "Reactor.hpp"
#include "CallbackFun.hpp"
#include "Log.hpp"
// #include "Util.hpp"

using namespace log_ns;
// using namespace util_ns;
void Usage(){
    std::cout<<"Usage: ./epoll_server port"<<std::endl;
}
int main(int argc,char *argv[]){
    if(argc!=2){
        Usage();
        exit(-1);
    }
    
    // 1.创建描述符
    int listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if (listen_fd < 0) {
        std::cerr<<("socket creation failed")<<std::endl;
        exit(EXIT_FAILURE);
    }
    //设置为非阻塞
    FdUtil::set_non_block(listen_fd);

    struct sockaddr_in addr;
    //addr.sin_addr.s_addr=inet_addr("127.0.0.1");
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_family=AF_INET;
    addr.sin_port=htons((uint16_t)atoi(argv[1]));

    if(bind(listen_fd,(struct sockaddr*)&addr,sizeof(addr))!=0){
        std::cerr<<"bind failed"<<std::endl;
        exit(EXIT_FAILURE);
    }

    if(listen(listen_fd,10)<0){
        LOG(ERROR)<<"listen failed"<<std::endl;
        exit(EXIT_FAILURE);
    }

    // 2.创建Reactor并初始化
    Reactor *R=new Reactor();
    R->init_reactor();
    R->insert_event(listen_fd,EPOLLIN |EPOLLET,accepter,nullptr,nullptr);
    
    // // 3.为listen_fd创建事件结构体Event。并将其插入到Reactor
    // Event *evt=new Event();
    // evt->sock_fd_=listen_fd;
    // //我们只关注listen_fd的读就绪事件，即请求连接事件（内核会以读就绪的方式通知连接请求）并将其设置为ET模式
    // //因此我们只需为listen_fd注册读回调函数
    // evt->ep_evt_.data.fd=listen_fd;
    // evt->ep_evt_.events=EPOLLIN | EPOLLET;
    // evt->register_callback(accepter,nullptr,nullptr);
    // // 使Event回指向Reactor
    // evt->R=R;
    // R->insert_event(evt);

    // 4.事件派发
    const int timeout=1000;
    while(1){
        R->dispatcher(1000);
    }

    return 0;
}