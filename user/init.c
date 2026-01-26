#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("I am running via ELF Loader!\n");
    int i = 0;
    while (1) {
        printf("Hello from C User Program!\n");
        printf("Counting: %d\n", i);
        i++;
        sleep(2);
    }
    return 0;
}
