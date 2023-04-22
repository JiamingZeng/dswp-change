## DSWP - Decoupled Software Pipelining

This is a class project for class EECS 583. We reimplemented the DSWP-plus repository (https://github.com/djsutherland/dswp-plus) with LLVM version 15.0.6 and improve its functionality. This repository can divide loops into multithreading and potentially speed up.

In this repository, we complete the following task,

- We fix several logic bugs including 
    * not correctly replacing the uses of registers obtained from consume
    * copy only part of the codes between different threads instead of all the instructions
    * keep branch instructions

- We also allow multithreading instead of 2 threading in the original repository
    * You can change the number of threads by changing the DSWP header file (DSWP.h) (note that you need to make clean)

- We successfully compile and optimize the demo including
    * simple.c
    * problem1.c

### Dependencies:
    - LLVM 15.0.6
    
### How to use it:
```
cd Example/
sh try.sh
```
    
### Parameters:
    - You can change the `MAX_THREAD` in the DSWP.h file. The minimum value is 2. When you change the maximum thread allowed, you need to do a remake.
    - In the first line of `Examples/problem1.sh`, you are able to try running the file you like by `clang -O0 -c -fPIE yourfile.c -o obj.o -emit-llvm`.

### DSWP structures:
    - To view the structures of DSWP, there are 4 files in the `Example` folder:
      - `dag` outlines the SCCs and their dependencies
      - `showgraph` shows the instructions 
      - `partition` presents how SCCs are organized into each partition/threads. This is determined by the heuristic based on the latencies of each SCC. If you see one empty partition, reduce `MAX_THREAD`
      - `out.o.ll` is a human readable file that displays the whole structures in each thread.

### Contributing
For major changes, please open an issue first to discuss what you would like to change.

Please make sure to update tests as appropriate.

### License
[MIT](https://choosealicense.com/licenses/mit/)