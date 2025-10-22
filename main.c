#include <stdint.h>

int series_sum(int n)
{
    volatile int x = 3;
    volatile int y = 5;
    volatile int z = 7;
    volatile int p = 9;
    volatile int q = 11;

    if (n > 0)
    {
        return n + series_sum(n - 1);
    }
    else
    {
        return 0;
    }
}

int main(void)
{
    int result;
    int k = 10;

    result = series_sum(k); // Compute sum of 1+2+...+10
    while (1); // Infinite loop for stack observation
}