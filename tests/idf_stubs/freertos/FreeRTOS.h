#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void test_enter_critical(portMUX_TYPE *mux);
void test_exit_critical(portMUX_TYPE *mux);
#define portENTER_CRITICAL(mux) test_enter_critical(mux)
#define portEXIT_CRITICAL(mux) test_exit_critical(mux)
#endif
