long long fun(long long a) {
	long long i, res;
	res = 0;
	for (i = 0; i < 100; i++) {
		res = res + i;
	}
	return res;
}

int main() {
    int res;
    res = fun(100000);
    printf("return value: %d\n", res);
    return 0;
}
