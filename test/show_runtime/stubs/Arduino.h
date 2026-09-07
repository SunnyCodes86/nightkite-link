#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <algorithm>
using std::min;
extern uint32_t hostMs;
inline uint32_t millis() { return hostMs; }
class Stream {
public:
 virtual ~Stream() {}
 virtual int available()=0;
 virtual int read()=0;
 virtual int availableForWrite()=0;
 virtual size_t write(const uint8_t*,size_t)=0;
};
