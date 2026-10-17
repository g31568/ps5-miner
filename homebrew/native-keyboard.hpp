#pragma once
#include <cstddef>
namespace native {
// Dialog state stays alive while Sony's keyboard holds its buffers.
bool keyboard_open(int user,unsigned row,const char *value);
bool keyboard_active();
int keyboard_poll(char *value,size_t size);
void keyboard_cancel();
}
