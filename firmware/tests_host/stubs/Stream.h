#pragma once
#include <stddef.h>
#include <stdint.h>

class Stream {
 public:
  virtual ~Stream() = default;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;

  size_t readBytesUntil(char terminator, char *buffer, size_t length) {
    size_t count = 0;
    while (count < length && available() > 0) {
      const int next = read();
      if (next < 0) break;
      if (static_cast<char>(next) == terminator) break;
      buffer[count++] = static_cast<char>(next);
    }
    return count;
  }
};
