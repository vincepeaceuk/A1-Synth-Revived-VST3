/* Native socket/process fixtures only: never starts Wine or loads A1.
 * Build with -fno-access-control to inject an owned native child and socket
 * into a Bridge whose ordinary constructor rejects missing resources.
 */
#include "../src/Bridge.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
int checks=0,failures=0;
constexpr uint32_t frames=256;

void check(bool condition,const char* label) {
    ++checks;
    std::cout<<(condition?"PASS: ":"FAIL: ")<<label<<'\n';
    if(!condition)++failures;
}

bool exact(int fd,void* data,size_t bytes,bool writing) {
    auto* next=static_cast<uint8_t*>(data);
    while(bytes) {
        const auto n=writing?write(fd,next,bytes):read(fd,next,bytes);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return false;
        next+=n;bytes-=size_t(n);
    }
    return true;
}

void respond(int fd,bool hang) {
    // A broken test/transport cannot leave an indefinitely orphaned fixture.
    signal(SIGALRM,SIG_DFL);alarm(3);
    if(hang)signal(SIGTERM,SIG_IGN);
    A1Header header{};A1Process request{};A1Midi midi{};
    if(!exact(fd,&header,sizeof(header),false)
       || header.magic!=A1_MAGIC || header.command!=A1_PROCESS
       || header.bytes!=sizeof(request)+sizeof(midi)
       || !exact(fd,&request,sizeof(request),false)
       || !exact(fd,&midi,sizeof(midi),false))_exit(10);
    if(request.frames!=frames || request.midiCount!=1 || request.program!=6
       || request.sampleRate!=44100 || request.tempo!=120
       || request.samplePosition!=987654 || request.parameters[37]!=37.0f/128
       || midi.frame!=13 || midi.size!=3 || midi.data[0]!=0x90
       || midi.data[1]!=60 || midi.data[2]!=100)_exit(11);
    if(hang)for(;;)pause();

    usleep(100000);
    std::array<float,A1_PARAMETERS+2*frames> response{};
    for(uint32_t i=0;i<A1_PARAMETERS;++i)response[i]=float(i)/128;
    for(uint32_t i=0;i<frames;++i) {
        response[A1_PARAMETERS+i]=0.25f+float(i)/1024;
        response[A1_PARAMETERS+frames+i]=-0.5f+float(i)/2048;
    }
    header={A1_MAGIC,A1_PROCESS,uint32_t(response.size()*sizeof(float)),A1_OK};
    if(!exact(fd,&header,sizeof(header),true)
       || !exact(fd,response.data(),response.size()*sizeof(float),true))_exit(12);
    // Leave the socket open until the owner sends QUIT or closes it.
    (void)exact(fd,&header,sizeof(header),false);
    close(fd);_exit(0);
}

class Fixture {
public:
    explicit Fixture(bool hang)
      : bridge(std::make_unique<a1::Bridge>("/a1-fixture-resources-missing",
          "/a1-fixture-bottle-missing","/a1-fixture-log-unused","Fixture only")) {
        check(!bridge->ready(),"missing resources prevent starting an original engine");
        int sockets[2];
        if(socketpair(AF_UNIX,SOCK_STREAM,0,sockets))throw std::runtime_error("fixture socketpair");
        const int yes=1;
        if(setsockopt(sockets[0],SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes))
           || setsockopt(sockets[1],SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes))
           || fcntl(sockets[0],F_SETFL,fcntl(sockets[0],F_GETFL)|O_NONBLOCK)<0) {
            close(sockets[0]);close(sockets[1]);throw std::runtime_error("fixture socket flags");
        }
        child=fork();
        if(child==0) {close(sockets[0]);respond(sockets[1],hang);_exit(13);}
        close(sockets[1]);
        if(child<0) {close(sockets[0]);throw std::runtime_error("fixture fork");}
        bridge->socket_=sockets[0];bridge->pid=child;
        bridge->samples.resize(A1_PARAMETERS+2*A1_MAX_FRAMES);
        bridge->alive.store(true);
    }
    void closeBridge() {bridge.reset();}
    bool reaped() const {int status=0;return waitpid(child,&status,WNOHANG)==-1 && errno==ECHILD;}
    std::unique_ptr<a1::Bridge> bridge;
private:
    pid_t child=-1;
};

A1Process request() {
    A1Process value{};value.frames=frames;value.midiCount=1;value.program=6;
    value.sampleRate=44100;value.tempo=120;value.samplePosition=987654;
    for(uint32_t i=0;i<A1_PARAMETERS;++i)value.parameters[i]=float(i)/128;
    return value;
}
}

int main() {
    try {
        const auto process=request();const A1Midi midi{13,3,{0x90,60,100,0}};
        {
            Fixture fixture(false);
            std::array<float,A1_PARAMETERS> actual{};
            std::array<float,frames> left{},right{};
            const auto start=Clock::now();
            const bool ok=fixture.bridge->process(process,&midi,actual.data(),left.data(),right.data());
            const auto elapsed=Clock::now()-start;
            check(ok && fixture.bridge->ready(),"100 ms helper response succeeds and preserves the engine connection");
            check(elapsed>=80ms && elapsed<500ms,"delayed exchange completes within 500 ms");
            bool exactValues=ok;
            for(uint32_t i=0;i<A1_PARAMETERS;++i)exactValues&=actual[i]==float(i)/128;
            for(uint32_t i=0;i<frames;++i) {
                exactValues&=left[i]==0.25f+float(i)/1024;
                exactValues&=right[i]==-0.5f+float(i)/2048;
            }
            check(exactValues,"all 99 parameters and both planar audio channels survive the delayed reply");
            fixture.closeBridge();check(fixture.reaped(),"delayed responder is closed and reaped by its owning Bridge");
        }
        {
            const auto start=Clock::now();Fixture fixture(true);
            std::array<float,A1_PARAMETERS> actual{};
            std::array<float,frames> left{},right{};
            check(!fixture.bridge->process(process,&midi,actual.data(),left.data(),right.data()),
                  "a helper that never responds returns a transport failure");
            check(!fixture.bridge->ready(),"unresponsive helper connection is marked failed");
            fixture.closeBridge();
            check(Clock::now()-start<2s,"unresponsive exchange and owned-child teardown remain bounded below 2 seconds");
            check(fixture.reaped(),"TERM-ignoring unresponsive responder is killed and reaped");
        }
        std::cout<<"Bridge process checks: "<<checks<<", failures: "<<failures<<'\n';
        return failures?1:0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;
    }
}
