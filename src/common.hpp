#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

inline int hydroMaxInt(int a, int b) {
    return a > b ? a : b;
}

inline int hydroMinInt(int a, int b) {
    return a < b ? a : b;
}

inline int hydroClampInt(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}
