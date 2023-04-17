#include "Utils.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <sstream>

using namespace std;

string itoa(int i) {
    std::stringstream out;
    out << i;
    return out.str();
}

void error(const char *info) {
	printf("ERROR: %s\n", info);
    exit(1);
}

void error(string info) {
    error(info.c_str());
}

int Utils::id = 0;

string Utils::genId() {
	std::stringstream out;
	out << "t" << id++;
	return out.str();
}
