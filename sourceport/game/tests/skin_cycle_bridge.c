#include <assert.h>
#include <stdarg.h>
#include <string.h>
#define CMD_SKIN_CYCLE 0xF9
static unsigned char answer[128], request[8];
static unsigned answer_size, request_size;
static int transport_error;
static int mu_online_abi_command(unsigned command, const unsigned char* payload, unsigned size,
    unsigned char* response, unsigned capacity, unsigned* got)
{
    assert(command == CMD_SKIN_CYCLE && size <= sizeof request && answer_size <= capacity);
    memcpy(request, payload, size); request_size = size;
    memcpy(response, answer, answer_size); *got = answer_size;
    return transport_error;
}
static void OSReport(const char* format, ...) { (void) format; }
#include "mu_skin_cycle_body.h"
int main(void)
{
    char name[16]; int costume = 0;
    answer[0] = 1; answer[1] = 3;
    memcpy(answer + 2, "Green skin", 10); answer_size = 12;
    assert(mu_skin_cycle(0, 9, &costume, 1, 0x1F, name, sizeof name));
    assert(costume == 3 && strcmp(name, "Green skin") == 0 && request_size == 5 && request[4] == 0x1F);
    assert(request[1] == 9 && request[2] == 0 && request[3] == 1);
    /* The online request and name layout remain compatible with the per-color command. */
    memcpy(answer + 1, "Online skin", 11); answer_size = 12;
    assert(mu_skin_cycle(2, 9, &costume, 0, 0, name, sizeof name));
    assert(costume == 3 && request_size == 4 && request[0] == 2 && request[3] == 0 &&
           strcmp(name, "Online skin") == 0);
    answer[1] = 3;
    assert(!mu_skin_cycle(0, 9, &costume, 1, 1, name, sizeof name));
    assert(costume == 3 && name[0] == 0);
    answer_size = 1;
    assert(!mu_skin_cycle(0, 9, &costume, 1, 0x1F, name, sizeof name));
    transport_error = -1;
    assert(!mu_skin_cycle(0, 9, &costume, 1, 0x1F, name, sizeof name));
    return 0;
}
