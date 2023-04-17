#include <stdio.h>

int fun(int a) {
	//printf("%d\n", a);
	//getchar();

	int i = 0, res = 200, res2 = 0, j;
	//res = 0;
	//printf("begin loop\n");
	for (i = 0; i < a; i++) {
		//printf("%d\n", i);
		res = res + i;
		//res2 = res2 + i * 2;
		//for (j = 0; j < i; j++)
		//	res2 = res2 + 1;
		//break;
	}
	printf("%d %d\n", res, res2);

	return res;
}

int main() {
	int res;
	res = fun(100000);
	printf("return value: %d\n", res);
	return 0;
}
