clang -O0 -emit-llvm -c simple.c -o obj.o
llvm-dis obj.o
opt -enable-new-pm=0 -load /n/eecs583a/home/zjiaming/dswp-change/DSWP.so -dswp obj.o -o out.o
# llvm-dis out.o



clang++ -emit-llvm -c test1.cpp test2.cpp
llvm-link -o test.bc1 test1.o test2.o 
opt test.bc1 -o test.bc2 -std-compile-opts

llc -filetype=obj test.bc2 -o test.o
clang++ test.o

# correct code
# opt -enable-new-pm=0 -load /n/eecs583a/home/zjiaming/dswp-change/DSWP.so -dswp obj.o -o out.o

clang -c -fPIE simple.c -o obj.o -emit-llvm
llc -filetype=obj obj.o -o test.o
clang test.o -o test.out

cd ..
cd runtime
clang -emit-llvm queue.c -o queue.o -pthread -fPIE -c -I .
clang -emit-llvm simple_sync.c -o simple_sync.o -pthread -fPIE -c -I . 
# llc -filetype=obj queue.o -o queue_llc.o
# llc -filetype=obj simple_sync.o -o simple_sync_llc.o
cd ../Example

opt -enable-new-pm=0 -load /n/eecs583a/home/zjiaming/dswp-change/DSWP.so -dswp obj.o -o out.o
# ld -o linked -filetype=obj out.o ../runtime/queue.o ../runtime/simple_sync.o -lc -shared

llvm-link -o linked.o out.o ../runtime/queue.o ../runtime/simple_sync.o 
llc -filetype=obj linked.o -o optimized.o

clang optimized.o -o optimized.out
clang -fPIE optimized.o -o optimized.out

# gpt-4
clang -c -fPIE simple.c -o obj.o -emit-llvm

cd ../runtime
clang -emit-llvm queue.c -o queue.o -pthread -fPIE -c -I .
clang -emit-llvm simple_sync.c -o simple_sync.o -pthread -fPIE -c -I . 
llc -filetype=obj queue.o -o queue.o
llc -filetype=obj simple_sync.o -o simple_sync.o
cd ../Example

opt -enable-new-pm=0 -load /n/eecs583a/home/zjiaming/dswp-change/DSWP.so -dswp obj.o -o out.o

llc -filetype=obj out.o -o optimized.o
clang optimized.o ../runtime/queue.o ../runtime/simple_sync.o -o test.out -fPIE -pthread


# llc -filetype=obj out.o -o optimized.o

llvm-objcopy --input-format=llvm --output-format=elf64-x86-64 --rename-section .text=.text obj.o obj.o
llvm-objcopy --input-format=llvm --output-format=elf64-x86-64 --rename-section .text=.text out.o out.o



1. 
clang simple.c -o a.out

2. 
clang simple.c -c obj.o
clang obj.o -o a.out

llvm-ld obj.o
echo 'run original code'
start_time=`date +%s`
./a.out
end_time=`date +%s`
echo execution time was `expr $end_time - $start_time` s.

llvm-link out.o ../runtime/queue.o  ../runtime/simple_sync.o

clang out.o ../runtime/queue.o  ../runtime/simple_sync.o

echo 'run optimized code'
start_time=`date +%s`
./a.out
end_time=`date +%s`
echo execution time was `expr $end_time - $start_time` s.