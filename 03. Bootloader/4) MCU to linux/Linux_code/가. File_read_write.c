#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    /* ── 쓰기 ── */
    int fd = open("test.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd < 0) {
        perror("open 실패");
        return 1;
    }

    write(fd, "Hello World!\n", 13);
    close(fd);
    printf("쓰기 완료\n");

    /* ── 읽기 ── */
    fd = open("test.txt", O_RDONLY);
    if(fd < 0) {
        perror("open 실패");
        return 1;
    }

    char buf[64] = {0};
    int n = read(fd, buf, 64);
    close(fd);

    printf("읽은 내용: %s", buf);
    printf("읽은 바이트: %d\n", n);

    return 0;
}
