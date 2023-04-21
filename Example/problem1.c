/*
 * Copyright (c) 2009, eagletmt
 * Released under the MIT License <http://opensource.org/licenses/mit-license.php>
 */
// https://raw.github.com/eagletmt/project-euler-c/master/1-9/problem1.c

#include <stdio.h>

int main(void)
{
  int s3 = 0, s5 = 0, s15 = 0;
  int i;
  int a = 0, b = 0, c = 0;

  for (i = 0; i < 100000; i++) {
    if (i % 3 == 0) {
      s3 += i;
    }
    if (i % 5 == 0) {
      s5 += s3;
    }
    if (i % 15 == 0) {
      s15 += s5;
    }
    a += b;
    b += c;
    c += a;
    a++;
    b++;
    c++;
    a = a % 2;
    b = b % 2;
    c = c % 2;
  }

  // printf("%d\n", s3 + s5 - s15);
  printf("%d\n", s3+s5-s15);

  return 0;
}
  
