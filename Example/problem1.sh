# correct code
# opt -enable-new-pm=0 -load /n/eecs583a/home/zjiaming/dswp-change/DSWP.so -dswp obj.o -o out.o

clang -c -fPIE simple.c -o obj.o -emit-llvm
llvm-dis obj.o
llc -filetype=obj obj.o -o test.o
clang test.o -o test.out

echo 'run original code'
start_time=$(date +%s%N | cut -b1-13)
./test.out
end_time=$(date +%s%N | cut -b1-13)
echo execution time was `expr $end_time - $start_time` ms.


opt -enable-new-pm=0 -load /n/eecs583a/home/zjiaming/dswp-change/DSWP.so -dswp obj.o -o out.o
# ld -o linked -filetype=obj out.o ../runtime/queue.o ../runtime/simple_sync.o -lc -shared

llvm-dis out.o
llc -filetype=obj out.o -o optimized.o 
clang optimized.o -o optimized.out  ../runtime/libruntime.a
echo 'run optimized code'
start_time=$(date +%s%N | cut -b1-13)
./optimized.out
end_time=$(date +%s%N | cut -b1-13)
echo execution time was `expr $end_time - $start_time` ms.