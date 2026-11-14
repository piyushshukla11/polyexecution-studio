/* Test program crashing with SIGSEGV to verify signal termination decoding */
int main(void) {
    volatile int *ptr = (volatile int *)0;
    *ptr = 42;
    return 0;
}
