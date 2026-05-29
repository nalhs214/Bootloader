#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma warning(disable:4996)

char buffer[BUFSIZ] = { 0, };

int main()
{
	FILE* fp = fopen("sample.txt", "w");
	//char s[60];

	//if (!fp)
	//{
	//	printf("파일 열기 실패\n");
	//}

	fputs("test\n", fp);
	fprintf(fp, "printf test : %d\n", 00);
	fclose(fp);

	FILE* fp = fopen("sample.txt", "r");
	for(int i = 0; i <)
	

	fclose(fp);


}
