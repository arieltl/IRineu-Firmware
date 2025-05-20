#include <Arduino.h>

void uint16ArrayToHexString(const uint16_t *arr, size_t len, char *out, size_t outSize);
uint16_t* parseHexMessage(size_t &outLen);

