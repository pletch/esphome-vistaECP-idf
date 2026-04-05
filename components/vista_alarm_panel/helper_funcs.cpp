#include "helper_funcs.h"

// Sum bytes cbuf[start..len) and return true if the total is 0 modulo 256.
// The ECP protocol appends a checksum byte such that the whole frame (data +
// checksum) sums to exactly 0x00 mod 256, so a zero sum means the frame is valid.
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

// Calculate a two's-complement checksum over cbuf[start..start+len).
// ~sum + 1 produces the byte that, when appended to the frame, makes the
// entire frame sum to 0x00 modulo 256.
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

// Validate a frame whose final byte is a two's-complement checksum.
// Sums the body bytes cbuf[start..len-1) and checks whether ~sum+1 equals
// the stored checksum at cbuf[len-1].
bool valid_chksum_two(const char * cbuf, int start, int len) // 2's complement negation
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

// Return true if s is a non-empty string with no leading whitespace that can
// be fully parsed as an integer in the given base.  strtol() sets *p to the
// first character it could not convert; if *p == '\0' the entire string was valid.
bool isInt(std::string s, int base)
{
    if (s.empty() || std::isspace(s[0]))
        return false;
    char *p;
    strtol(s.c_str(), &p, base);
    return (*p == 0);
}

// Convert a 12-bit BCD value packed into an int to a plain decimal integer.
// Bits [15:8] hold the hundreds digit, bits [7:4] hold the tens digit, and
// bits [3:0] hold the units digit.
// Example: n = 0x0042 → (0)*100 + (4)*10 + 2 = 42.
int toDec(int n)
{
    return ((n >> 8) * 100) + (((n & 0xFF) >> 4) * 10) + (n & 0x0F);
}

// Parse a string as an integer in the given base.
// Returns 0 for empty or whitespace-only strings.
int toInt(std::string s, int base)
{
    if (s.empty() || std::isspace(s[0]))
        return 0;
    char *p;
    int li = strtol(s.c_str(), &p, base);
    return li;
}

// Return true if the first 'len' bytes of a1 and a2 are identical.
// Short-circuits on the first mismatch for efficiency.
bool areEqual(char *a1, char *a2, uint8_t len)
{
    for (int x = 0; x < len; x++)
    {
        if (a1[x] != a2[x])
            return false;
    }
    return true;
}
