#pragma once

// The RP2040 (earlephilhower) core exposes std::min / std::max as templates,
// so the motion code can write min<uint32_t>(a, b). The STM32 core instead
// defines min / max as preprocessor macros, which makes that template syntax
// a compile error. Undefining the macros and pulling in the std:: templates
// gives both cores the same explicit-type min<T>() / max<T>() spelling.
//
// Include this last, after Arduino.h has been pulled in by the other headers.

#include <Arduino.h>
#include <algorithm>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using std::max;
using std::min;
