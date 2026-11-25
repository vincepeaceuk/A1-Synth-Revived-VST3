/* Deliberately withholds the handshake to verify cancellation of setup. */
#include <unistd.h>
__attribute__((constructor)) static void slow_startup(void) { sleep(5); }
