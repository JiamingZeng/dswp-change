double fun(double a) {
	int i;
	double res;
	res = 0;
	for (i = 0; i < 100; i++) {
		double x = i;
		res = res + x;
	}
	return res;
}

int main() {
    int res;
    res = fun(100000);
    printf("return value: %d\n", res);
    return 0;
}
