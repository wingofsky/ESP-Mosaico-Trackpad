#include "command_line.h"
#include <string.h>

bool command_readline(command_line_t *s, char *out, size_t cap,
                      command_read_fn read_byte, void *context)
{
    if(!out || !cap) return false;
    for(unsigned budget=0;budget<COMMAND_READ_BUDGET;budget++) {
        char ch;
        if(!read_byte(&ch,context)) break;
        if(ch=='\n' || ch=='\r') {
            size_t n=s->used;
            bool valid=n && !s->overflow && n<cap;
            s->used=0;s->overflow=false;
            if(valid) {
                memcpy(out,s->line,n);out[n]=0;
                return true;
            }
        } else if(ch<0x20 || ch>0x7e) s->overflow=true;
        else if(s->used+1<sizeof(s->line)) s->line[s->used++]=ch;
        else s->overflow=true;
    }
    return false;
}
