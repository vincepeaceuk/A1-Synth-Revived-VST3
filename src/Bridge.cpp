#include "Bridge.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;

namespace a1 {
namespace {
enum class ChildWait { exited, running, error };

ChildWait waitForChild(pid_t child, int milliseconds) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    for(;;) {
        int status=0;
        const auto result=waitpid(child,&status,WNOHANG);
        if(result==child || (result<0 && errno==ECHILD))return ChildWait::exited;
        if(result<0 && errno!=EINTR)return ChildWait::error;
        const auto remaining=std::chrono::duration_cast<std::chrono::microseconds>(
            deadline-std::chrono::steady_clock::now()).count();
        if(remaining<=0)return ChildWait::running;
        usleep(useconds_t(std::min<int64_t>(remaining,5000)));
    }
}

// The PID is an owned direct child. Every wait remains nonblocking, including
// after KILL, so an OS-level stalled child cannot hang the host's destructor.
void stopChild(pid_t& child) {
    if(child<=0)return;
    auto result=waitForChild(child,100);
    if(result==ChildWait::running) {
        if(kill(child,SIGTERM)==0 || errno==ESRCH)result=waitForChild(child,200);
        else result=ChildWait::error;
    }
    if(result==ChildWait::running) {
        if(kill(child,SIGKILL)==0 || errno==ESRCH)result=waitForChild(child,500);
    }
    // No detached reaper is started from this unloadable plug-in library.
    // A pathological uninterruptible child may outlive the bounded deadline.
    child=-1;
}
}
void Bridge::fail(const std::string& message) {
    {std::lock_guard lock(reasonMutex);reason=message;}
    alive.store(false);
}
bool Bridge::transfer(bool writing,void* memory,size_t size,int timeout) {
    auto* bytes=static_cast<uint8_t*>(memory);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    while(size) {
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        if(remaining<=0) {fail("Original A1 engine stopped responding");return false;}
        pollfd p{socket_,short(writing?POLLOUT:POLLIN),0};
        int result=poll(&p,1,int(remaining));
        if(result<0 && errno==EINTR)continue;
        if(result<=0) {fail("Original A1 engine stopped responding");return false;}
        auto n=writing?send(socket_,bytes,size,0):recv(socket_,bytes,size,0);
        if(n<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK))continue;
        if(n<=0) {fail("Original A1 engine connection closed");return false;}
        bytes+=n;size-=size_t(n);
    }
    return true;
}
bool Bridge::reply(uint32_t command,void* data,size_t expected,int timeout) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    auto remaining=[&] {return int(std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline-std::chrono::steady_clock::now()).count()));};
    A1Header h{};
    if(!transfer(false,&h,sizeof(h),remaining()))return false;
    if(h.magic!=A1_MAGIC || h.command!=command) {fail("Invalid A1 engine response");return false;}
    if(h.status!=A1_OK) {
        if(h.bytes>1024) {fail("Invalid A1 error response");return false;}
        std::string text(h.bytes,'\0');
        if(h.bytes && !transfer(false,text.data(),text.size(),remaining()))return false;
        fail("Original A1: "+text);return false;
    }
    if(h.bytes!=expected) {fail("Unexpected A1 response size");return false;}
    return expected==0 || transfer(false,data,expected,remaining());
}
Bridge::Bridge(const std::string& resources,const std::string& bottleRoot,
               const std::string& logPath,const std::string& hostName) {
    const std::string wine="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine";
    const std::string helper=resources+"/a1-helper.exe", dll=resources+"/a1.dll";
    if(access(wine.c_str(),X_OK) || access(helper.c_str(),R_OK) || access(dll.c_str(),R_OK)) {
        fail("A1 preview resources or CrossOver are missing");return;
    }
    if(access((bottleRoot+"/A1RevivalPreview/cxbottle.conf").c_str(),R_OK)) {
        fail("A1 preview's compatibility environment is missing");return;
    }
    int pair[2];
    if(socketpair(AF_UNIX,SOCK_STREAM,0,pair)) {fail("Cannot create A1 engine connection");return;}
    socket_=fcntl(pair[0],F_DUPFD_CLOEXEC,10);
    int child=fcntl(pair[1],F_DUPFD_CLOEXEC,10);close(pair[0]);close(pair[1]);
    if(socket_<0 || child<0) {
        if(child>=0)close(child);fail("Cannot create A1 engine descriptors");return;
    }
    const int yes=1;setsockopt(socket_,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
    fcntl(socket_,F_SETFL,fcntl(socket_,F_GETFL)|O_NONBLOCK);
    int log=open(logPath.c_str(),O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0600);
    if(log<0) {close(child);fail("Cannot create A1 preview log");return;}
    int childLog=fcntl(log,F_DUPFD_CLOEXEC,10);close(log);
    if(childLog<0) {close(child);fail("Cannot create A1 log descriptor");return;}
    auto windowsPath=[](std::string path) {
        std::replace(path.begin(),path.end(),'/','\\');return "Z:"+path;
    };
    std::vector<std::string> arguments{wine,"--bottle","A1RevivalPreview","--no-gui",
        "--debugmsg","-all,+msgbox","--ux-app",resources+"/a1-engine-launcher",windowsPath(helper),windowsPath(dll)};
    std::vector<char*> argv;for(auto& s:arguments)argv.push_back(s.data());argv.push_back(nullptr);
    std::vector<std::string> environment;
    for(char** p=environ;*p;++p) {
        std::string e=*p;
        if(e.starts_with("CX_BOTTLE_PATH=") || e.starts_with("CX_BOTTLE=") || e.starts_with("WINEPREFIX=") ||
           e.starts_with("CX_ENV=") || e.starts_with("DYLD_INSERT_LIBRARIES=") ||
           e.starts_with("A1_X87_") || e.starts_with("X87_") || e.starts_with("ROSETTA_"))continue;
        environment.push_back(std::move(e));
    }
    environment.push_back("CX_BOTTLE_PATH="+bottleRoot);
    std::vector<char*> env;for(auto& e:environment)env.push_back(e.data());env.push_back(nullptr);
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,child,STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions,child,STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions,childLog,STDERR_FILENO);
    posix_spawnattr_t attributes;posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes,POSIX_SPAWN_CLOEXEC_DEFAULT);
    const int result=posix_spawn(&pid,wine.c_str(),&actions,&attributes,argv.data(),env.data());
    posix_spawnattr_destroy(&attributes);posix_spawn_file_actions_destroy(&actions);
    close(child);close(childLog);
    if(result) {pid=-1;fail("Cannot start original A1 engine");return;}
    A1Host host{};
    std::strncpy(host.product,hostName.c_str(),sizeof(host.product)-1);
    std::strcpy(host.vendor,"Steinberg");
    A1Header hello{A1_MAGIC,A1_HELLO,sizeof(host),0};
    if(!transfer(true,&hello,sizeof(hello),25000) || !transfer(true,&host,sizeof(host),25000)
        || !reply(A1_HELLO,&metadata,sizeof(metadata),25000))return;
    if(metadata.parameters!=99 || metadata.outputs!=2) {fail("Unexpected original A1 configuration");return;}
    samples.resize(99+2*A1_MAX_FRAMES);alive.store(true);
}
Bridge::~Bridge() {
    if(socket_>=0) {
        if(ready()) {A1Header quit{A1_MAGIC,A1_QUIT,0,0};(void)transfer(true,&quit,sizeof(quit),20);}
        shutdown(socket_,SHUT_RDWR);close(socket_);
    }
    stopChild(pid);
}
bool Bridge::process(const A1Process& request,const A1Midi* midi,float* actual,float* left,float* right) {
    if(!ready() || !request.frames || request.frames>A1_MAX_FRAMES || request.midiCount>A1_MAX_MIDI)return false;
    A1Header h{A1_MAGIC,A1_PROCESS,uint32_t(sizeof(request)+request.midiCount*sizeof(A1Midi)),0};
    // This exchange belongs to the render worker. A scheduler hiccup should
    // recover as a measured late result, not permanently kill the instrument.
    // Keep a single bounded budget across request/reply operations.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
    auto remaining=[&] {return int(std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline-std::chrono::steady_clock::now()).count()));};
    if(!transfer(true,&h,sizeof(h),remaining())
        || !transfer(true,const_cast<A1Process*>(&request),sizeof(request),remaining())
        || (request.midiCount && !transfer(true,const_cast<A1Midi*>(midi),request.midiCount*sizeof(A1Midi),remaining()))
        || !reply(A1_PROCESS,samples.data(),(99+2*request.frames)*sizeof(float),remaining()))return false;
    std::copy_n(samples.data(),99,actual);
    std::copy_n(samples.data()+99,request.frames,left);
    std::copy_n(samples.data()+99+request.frames,request.frames,right);
    return true;
}
}
