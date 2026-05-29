#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

int main(void)
{
	char basestr[1000];
	int port;
	char protocol[100];
	char domain[200];
	scanf("%[^\n]", basestr);

	int res = sscanf(basestr, "%[^:]://%[^:]:%d", protocol, domain, &port);

	printf("Protocol: %s\n"
		"Domain: %s\n"
		"Port: %d\n"
		"sscanf result: %d\n", protocol, domain, port, res);



	return 0;
}