//#include <stdio.h>
//
//int main(int argc, char* argv[])
//{
	//int* pointer_array[10];		
	//int array[10];
	//int* array_pointer = array;

	//for (int i = 0; i < 10; i++)
	//{
	//	*(array_pointer + i) = i;
	//}

	//for (int i = 0; i < 10; i++)
	//{
	//	pointer_array[i] = (int*)i;
	//}

	//for (int i = 0; i < 10; i++)
	//{
	//	printf("%d", array[i]);
	//}

	//printf("\n");
	//return 0;

#include <stdio.h>

typedef struct _LN
{
	int nValue;
	struct _LN* nextNode;
} LinkedList;

int main(int argc, char* argv[])
{
	LinkedList ln1, ln2, ln3;

	ln1.nValue = 10;
	ln1.nextNode = &ln2;

	ln2.nValue = 20;
	ln2.nextNode = &ln3;

	ln3.nValue = 30;
	ln3.nextNode = NULL;

	puts("Very Tiny Linked List Example\n\n");

	for (LinkedList* iter = &ln1; iter != NULL; iter = iter->nextNode)
	{
		printf("%d -> ", iter->nValue);
	}
	printf("[x]\n");

	return 0;
}