#include "Utils.h"

// Only declare the variables as extern since they are defined in main.cpp
extern char sharedBuf[];
extern const size_t max_str_len;
extern const size_t max_elems;

void uint16ArrayToHexString(const uint16_t *arr, size_t len, char *out, size_t outSize) {
  size_t offset = 0;
  for (size_t i = 0; i < len; ++i) {
    int written = snprintf(out + offset,
                           outSize  - offset,
                           "%04X%s",
                           arr[i],
                           (i == len - 1) ? "" : " ");
    if (written < 0 || (size_t)written >= outSize - offset) {
      break;  // buffer full or error
    }
    offset += written;
  }
}

uint16_t* parseHexMessage(size_t &outLen) {
  outLen = 0;

  // copy into temp so we don't mangle sharedBuf
  size_t len = strlen(sharedBuf);
  if (len + 1 > max_str_len) return nullptr;
  char *tmp = new char[len + 1];
  memcpy(tmp, sharedBuf, len + 1);

  // first token = length (in HEX)
  char *tok = strtok(tmp, " ");
  if (!tok) { delete[] tmp; return nullptr; }
  long count = strtol(tok, nullptr, 16);
  if (count < 0 || count > max_elems) { delete[] tmp; return nullptr; }
  outLen = (size_t)count;

  // allocate output array
  uint16_t *outArr = new uint16_t[outLen];

  // parse exactly outLen hex words
  for (size_t i = 0; i < outLen; ++i) {
    tok = strtok(nullptr, " ");
    if (!tok) { 
      delete[] outArr;
      delete[] tmp;
      outLen = 0;
      return nullptr;
    }
    long v = strtol(tok, nullptr, 16);
    if (v < 0 || v > 0xFFFF) {
      delete[] outArr;
      delete[] tmp;
      outLen = 0;
      return nullptr;
    }
    outArr[i] = (uint16_t)v;
  }

  delete[] tmp;
  return outArr;
}
