// Stubs for pthread functions needed by libstdc++ guard.o on ESP8266
// ESP8266 is single-core; these are never actually called.

int pthread_cond_init(void *cond, const void *attr) { (void)cond; (void)attr; return 0; }
int pthread_cond_wait(void *cond, void *mutex) { (void)cond; (void)mutex; return 0; }
int pthread_cond_broadcast(void *cond) { (void)cond; return 0; }
int pthread_cond_destroy(void *cond) { (void)cond; return 0; }
