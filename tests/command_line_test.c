#include "command_line.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { const char *data; size_t length, offset, calls; bool endless; } source_t;
static bool read_byte(char *out,void *context)
{
    source_t *s=context;s->calls++;
    if(s->endless) { *out='x';return true; }
    if(s->offset==s->length) return false;
    *out=s->data[s->offset++];return true;
}
int main(void)
{
    command_line_t s={0};char out[96]={0};
    source_t input={.data="sta",.length=3};
    assert(!command_readline(&s,out,sizeof(out),read_byte,&input));
    input=(source_t){.data="tus\r\n",.length=5};
    assert(command_readline(&s,out,sizeof(out),read_byte,&input));
    assert(!strcmp(out,"status"));
    input=(source_t){.endless=true};
    assert(!command_readline(&s,out,sizeof(out),read_byte,&input));
    assert(input.calls==COMMAND_READ_BUDGET); // sustained flood must yield
    input=(source_t){.data="\nstatus\n",.length=8};
    assert(command_readline(&s,out,sizeof(out),read_byte,&input));
    assert(!strcmp(out,"status")); // oversized line discarded in full
    input=(source_t){.data="sta\0tus\nstatus\n",.length=15};
    assert(command_readline(&s,out,sizeof(out),read_byte,&input));
    assert(!strcmp(out,"status")); // embedded NUL cannot truncate a command
    input=(source_t){.data="status\n",.length=7};
    assert(!command_readline(&s,out,3,read_byte,&input));
    input=(source_t){.endless=true};
    assert(!command_readline(&s,NULL,0,read_byte,&input));assert(!input.calls);
    puts("PASS: bounded CDC parsing, fragmented input, overflow/NUL rejection and recovery");
}
