/* Windows socket fixture only. It never loads a DLL or starts a synth. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

int main(void) {
    char ready[2] = {0};
    if (GetEnvironmentVariableA("A1_X87_READY", ready, sizeof(ready)) != 1 || ready[0] != '1')
        return 10;
    uint8_t data[16384];
    for (unsigned i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)i;
    DWORD done = 0;
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), data, sizeof(data), &done, NULL) ||
        done != sizeof(data)) return 11;
    uint8_t ack = 0;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), &ack, 1, &done, NULL) || done != 1 || ack != 0x7f)
        return 12;
    return 0;
}
