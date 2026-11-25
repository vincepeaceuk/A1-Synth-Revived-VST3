#pragma once
#include "Protocol.h"
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include <sys/types.h>

namespace a1 {
class Bridge {
public:
    Bridge(const std::string& resources,const std::string& bottleRoot,
           const std::string& logPath,const std::string& actualCubaseName);
    ~Bridge();
    Bridge(const Bridge&)=delete;
    Bridge& operator=(const Bridge&)=delete;
    bool ready() const {return alive.load();}
    std::string error() const {std::lock_guard lock(reasonMutex);return reason;}
    bool process(const A1Process&,const A1Midi*,float* actual,float* left,float* right);
    A1Metadata metadata{};
private:
    bool transfer(bool writing,void*,size_t,int timeout);
    bool reply(uint32_t command,void* data,size_t expected,int timeout);
    void fail(const std::string&);
    int socket_=-1;
    pid_t pid=-1;
    std::atomic<bool> alive{false};
    mutable std::mutex reasonMutex;
    std::string reason;
    std::vector<float> samples;
};
}
