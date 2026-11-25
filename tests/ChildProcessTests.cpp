/* Native process fixtures only. Never starts Wine or the original A1 DLL.
 * Build with -fno-access-control to inject an owned fixture PID into Bridge;
 * the production class and its normal constructor are otherwise unchanged.
 */
#include "../src/Bridge.h"
#include <chrono>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace {
int checks;
void check(bool value, const char* label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}

pid_t ignoringChild() {
    int ready[2];
    if (pipe(ready)) throw std::runtime_error("fixture pipe");
    const pid_t child = fork();
    if (child == 0) {
        close(ready[0]); signal(SIGTERM, SIG_IGN);
        const char byte = 'R'; (void)write(ready[1], &byte, 1); close(ready[1]);
        for (;;) pause();
    }
    close(ready[1]);
    if (child < 0) { close(ready[0]); throw std::runtime_error("fixture fork"); }
    char byte = 0;
    const auto received = read(ready[0], &byte, 1); close(ready[0]);
    check(received == 1 && byte == 'R', "child must ignore TERM before teardown");
    return child;
}

void destroyBridgeWith(pid_t child) {
    a1::Bridge fixture("/a1-fixture-resources-missing", "/a1-fixture-bottle-missing",
                       "/a1-fixture-log-unused", "Fixture only");
    check(!fixture.ready(), "missing resources must not start an original engine");
    fixture.pid = child;
}

struct OwnedChild {
    pid_t value;
    ~OwnedChild() {
        if (value > 0) {
            kill(value, SIGKILL);
            int status = 0;
            while (waitpid(value, &status, 0) < 0 && errno == EINTR) {}
        }
    }
};
}

int main() {
    try {
        OwnedChild unrelated{ignoringChild()};
        const pid_t target = ignoringChild();
        const auto start = std::chrono::steady_clock::now();
        destroyBridgeWith(target);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        check(elapsed < std::chrono::seconds(2), "TERM-ignoring child teardown must be bounded");
        int status = 0;
        check(waitpid(target, &status, WNOHANG) == -1 && errno == ECHILD,
              "owned child must be reaped");
        check(kill(unrelated.value, 0) == 0, "unrelated child must remain alive");

        const pid_t finished = fork();
        if (finished == 0) _exit(7);
        check(finished > 0, "finished fixture fork");
        destroyBridgeWith(finished);
        check(waitpid(finished, &status, WNOHANG) == -1 && errno == ECHILD,
              "naturally exiting child must be reaped");
        destroyBridgeWith(-1);
        std::cout << "Child-process checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
