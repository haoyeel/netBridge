/*
 *�û��ռ��Ӧ�Ĳ��Գ���
 */

#include <stdio.h>
#include <fcntl.h>
#include <signal.h>

void sighander(int signo)
{
	printf("signal handled\n");
}

int main()
{
	int fd, Oflags;
	signal(SIGIO, sighander);
	fd = open("/dev/capture_dev", O_RDWR);
	if(fd < 0) {
		printf("open capture dev filed\n");
		return -1;
	}
	fcntl(fd, F_SETOWN, getpid());
	Oflags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, Oflags | FASYNC);
	while(1)
	{
		sleep(10);
	}
	return 0;
}