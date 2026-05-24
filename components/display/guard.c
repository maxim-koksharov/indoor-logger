// Single-threaded C++ static guard implementations for ESP8266 (single-core)

int __cxa_guard_acquire(int *g)  { return !*g; }
void __cxa_guard_release(int *g) { *g = 1; }
void __cxa_guard_abort(int *g)   { (void)g; }
