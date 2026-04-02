#include "helper_funcs.h"

bool valid_chksum(const char * cbuf, int start, int len)
{
  uint16_t chksum = 0;
  for (uint8_t x = start; x < len; x++)
  {
    chksum += cbuf[x];
  }
  if (chksum % 256 == 0)
    return true;
  else
    return false;
}

//Calculate checksum via 2's complement negation from start and where len is number of bytes to include in calculation. 
uint8_t calc_chksum_two(const char * cbuf, int start, int len) 
{
    uint8_t chksum = 0;
    for (int x = 0; x < len; x++)
    {
        chksum += cbuf[x];
    }
    chksum = ~chksum + 1;
    return chksum;
}

bool valid_chksum_two(const char * cbuf, int start, int len) //2's complement negation
{
  uint8_t sum = 0;
  for (uint8_t x = start; x < len - 1; x++)
  {
    sum += cbuf[x];
  }
  if (~(sum) + 1 == cbuf[len - 1])
    return true;
  else
    return false;
}

bool isInt(std::string s, int base)
{
    if (s.empty() || std::isspace(s[0]))
        return false;
    char *p;
    strtol(s.c_str(), &p, base);
    return (*p == 0);
}

int toDec(int n)
{
    return ((n >> 8) * 100) + (((n & 0xFF) >> 4) * 10) + (n & 0x0F);
}

int toInt(std::string s, int base)
{
    if (s.empty() || std::isspace(s[0]))
        return 0;
    char *p;
    int li = strtol(s.c_str(), &p, base);
    return li;
}

bool areEqual(char *a1, char *a2, uint8_t len)
{
    for (int x = 0; x < len; x++)
    {
        if (a1[x] != a2[x])
            return false;     
    }
    return true;
}
