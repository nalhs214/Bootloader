#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>


typedef struct Place_Locations
{
	char Designator[10];
	char Comment[50];
	char Layer[20];
	double Center_X;
	double Center_Y;
	int Rotation;
	char Description[100];
}Data;

Data nu40data[150];

int main()
{
	FILE* fp;
	char buff[2560] = { 0, };
	char* pstr;
	int cnt = 0, n = 0;

	fp = fopen("Pick Place for NU40-DK-Basic(Default).csv", "r");

	if (!fp)
	{
		goto file_not_exists;
	}

	while (!feof(fp))		// feof는 파일의 끝인지 확인
	{
		pstr = fgets(buff, sizeof(buff), fp);

		if (pstr == NULL) break;
		n = sscanf(pstr, "\"%[^\"]\",\"%[^\"]\",\"%[^\"]\",\"%lf\",\"%lf\",\"%d\",\"%[^\"]",
			&nu40data[cnt].Designator,
			&nu40data[cnt].Comment,
			&nu40data[cnt].Layer,
			&nu40data[cnt].Center_X,
			&nu40data[cnt].Center_Y,
			&nu40data[cnt].Rotation,
			&nu40data[cnt].Description
		);
		if (n == 7) cnt++;
		n = 0;

	}
	fclose(fp);

	for (int i = 0; i < cnt; i++)
	{
		printf("%s/%s/%s/%.4lf/%.4lf/%d/%s\n\n",
			nu40data[i].Designator,
			nu40data[i].Comment,
			nu40data[i].Layer,
			nu40data[i].Center_X,
			nu40data[i].Center_Y,
			nu40data[i].Rotation,
			nu40data[i].Description);
	}

	return 0;

file_not_exists:
	printf("File not exists!\n");
	return 0;
}



/*
질문 1 : 버퍼의 크기를 어떤 설정하는게 좋은가?
질문 2 : 설명하는 부분, 오류값에 대해서 sscanf return 값을 사용해서
		 7개를 맞게 받았는지 비교함으로 제거했는데 맞게 접근한건지


*/