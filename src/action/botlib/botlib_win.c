#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void seed_random_number_generator(void) {
#if _MSC_VER >= 1920 && !__INTEL_COMPILER
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    srand((unsigned int)(li.QuadPart));
#else
    // Fallback for other Windows compilers
    srand((unsigned int)time(NULL));
#endif
}
#endif