#include <stdio.h>

int fun(int a) {

	int i = 0, res = 200, res2 = 0, j;
	for (i = 0; i < a; i++) {
		res = res + i;
	}
	printf("%d %d\n", res, res2);

	return res;
}

int main() {
	int res;
	res = fun(1000);
	printf("return value: %d\n", res);
	return 0;
}
