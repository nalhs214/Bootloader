#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>

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

Data* nu40data=NULL;	//버퍼 최적화를 위한 포인터
Data _nu40data;		
int main()
{
	FILE* fp;
	char buff[BUFSIZ] = { 0, };
	char* pstr;
	int cnt = 0, n = 0;

	fp = fopen("__", "r");

	if (!fp)
	{
		goto file_not_exists;
	}

	while (!feof(fp))		// feof는 파일의 끝인지 확인
	{
		pstr = fgets(buff, sizeof(buff), fp);

		if (pstr == NULL) break;
		n = sscanf(pstr, "\"%[^\"]\",\"%[^\"]\",\"%[^\"]\",\"%lf\",\"%lf\",\"%d\",\"%[^\"]",
			&_nu40data.Designator,
			&_nu40data.Comment,
			&_nu40data.Layer,
			&_nu40data.Center_X,
			&_nu40data.Center_Y,
			&_nu40data.Rotation,
			&_nu40data.Description
		);
		if (n == 7) cnt++;
		n = 0;

	}
	nu40data = (Data*)malloc(sizeof(Data) * cnt);		// 데이터 갯수를 측정한 다음에 해당 데이터만큼 메모리 받기
	int readcnt;
	if (!nu40data)
	{
		goto memerr;
	}
	fseek(fp, 0, SEEK_SET);
	readcnt = 0;

	// 여기서부터는 진짜 메모리에 넣기
	while (!feof(fp))		// feof는 파일의 끝인지 확인
	{
		pstr = fgets(buff, sizeof(buff), fp);

		if (pstr == NULL) break;
		n = sscanf(pstr, "\"%[^\"]\",\"%[^\"]\",\"%[^\"]\",\"%lf\",\"%lf\",\"%d\",\"%[^\"]",
			&nu40data[readcnt].Designator,
			&nu40data[readcnt].Comment,
			&nu40data[readcnt].Layer,
			&nu40data[readcnt].Center_X,
			&nu40data[readcnt].Center_Y,
			&nu40data[readcnt].Rotation,
			&nu40data[readcnt].Description
		);
		if (n == 7) readcnt++;
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

	free(nu40data);
	return 0;

memerr:
	fclose(fp);
	printf("Memerr!\n");
	return 0;
file_not_exists:
	printf("File not exists!\n");
	return 0;
}


