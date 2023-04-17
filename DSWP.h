#ifndef DSWP_H_
#define DSWP_H_

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/raw_os_ostream.h"

#include "Utils.h"

#include <ostream>
#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <queue>

using namespace llvm;
using namespace std;

static const int MAX_THREAD = 2;

//REG: register dependency
//DTRUE: data dependency - read after write
//DANTI: data dependency - write after read
//DOUT: data dependency - write after write
//DSYN: data dependency - read after read

enum DType {
	REG, DTRUE, DANTI, DOUT, DSYN, CONTROL, CONTROL_LC
};

struct Edge {
	Instruction *u, *v;
	DType dtype;
	Edge(Instruction *u, Instruction *v, DType dtype);

	bool operator== (const Edge& rhs) const {
		return (this->u == rhs.u && this->v == rhs.v 
				&& this->dtype == rhs.dtype);
	}
};

struct QNode {
	int u;
	int latency;
	QNode(int u, int latency);
	bool operator <(const QNode &y) const;
};

class DSWP: public LoopPass {

private:
	//neccesary information
	Module *module;
	Function *func;
	BasicBlock *header;
	BasicBlock *predecessor;
	BasicBlock *exit;
	LLVMContext *context;
	Type *eleType;
	int loopCounter;

	set<Function *> generated;	//all generated function that should not be run in the pass

	// part 0: initial setup and helpers for dependency
	bool initialize(Loop *L);

	void addEdge(Instruction *u, Instruction *v, DType dtype);
	bool checkEdge(Instruction *u, Instruction *v);

	//part 1: program dependence graph
	void buildPDG(Loop *L);

	void checkControlDependence(BasicBlock *a, BasicBlock *b,
								PostDominatorTree &pdt);

	void addControlDependence(BasicBlock *a, BasicBlock *b);

	void dfsVisit(BasicBlock *BB, std::set<BasicBlock *> &vis,
				  std::vector<BasicBlock *> &ord, Loop *L);

	//part 2: scc and dag
	void findSCC(Loop *L);

	void dfs_forward(Instruction *inst);
	void dfs_reverse(Instruction *inst);

	//program dependence graph
	map<Instruction *, vector<Edge> > pdg;
	//reverse graph for scc
	map<Instruction *, vector<Edge> > rev;

	// DAG of the SCC relationships
	map<int, vector<int> > scc_dependents;
	map<int, vector<int> > scc_parents;

	//edge lookup for SCC dag
	map<std::pair<int, int>, bool> dag_added;

	//edge table, all the dependency relationship
	vector<Edge> allEdges;

	vector<vector<Instruction *> > InstInSCC;

	// the immediate dominator and postdominator for each basic block
	map<BasicBlock *, BasicBlock *> idom, postidom;

	//total number of scc
	int sccNum;

	//map instruction to sccId
	map<Instruction *, int> sccId;

	//tmp flag
	map<Instruction *, bool> used;

	//store the dfs sequence
	vector<Instruction *> list;

	BasicBlock * replaceBlock;

	//part 3: thread partition
	void threadPartition(Loop *L);

	//get the latency estimate of a instruction
	int getLatency(Instruction *I);

	//assigned[i] = node i in dag has been assgined to assigned[i] thread
	vector<int> assigned;

	int getInstAssigned(Value *inst);

	//part[i] = i thread contains part[i] nodes of the dag
	vector<int> part[MAX_THREAD];
	//set<BasicBlock *> BBS[MAX_THREAD];

	//total lantency within a scc
	vector<int> sccLatency;

	//part 4: code splitting
	void preLoopSplit(Loop *L);

	void loopSplit(Loop *L);
	StructType *argStructTy; // the type of the struct that worker threads get

	// map from old instructions to new instuction, by thread
	map<Value *, Value *> instMap[MAX_THREAD];

	// keep track of where we should insert produces() for each old instruction
	// in each thread, if we need them. maps to the following instruction.
	map<Instruction *, Instruction *> placeEquivalents[MAX_THREAD];

	map<Value *, Value *> newToOld;
	map<Value *, int> newInstAssigned; // which thread each new inst is in

	int getNewInstAssigned(Value *inst);

	//the new functions (has already been inserted, waiting for syn)
	vector<Function *> allFunc;

	// get dominator information
	void getDominators(Loop *L);

	//get live variable information
	void getLiveinfo(Loop *L);
	vector<Value *> livein; // inputs to the loop
	vector<Value *> defin; // variable generated within the loop
	vector<Value *> liveout; // variables needed after the loop is done

	// part 5: synchronization insertion
	void insertSynchronization(Loop *L);

	void insertProduce(Instruction * u, Instruction *v, DType dtype,
			int channel, int uthread, int vthread);

	void insertConsume(Instruction * u, Instruction *v, DType dtype,
			int channel, int uthread, int vthread);

	void cleanup(Loop *L, LPPassManager &LPM);
	void clear();

	//test function
	void showGraph(Loop *L);

	//show DAC information
	void showDAG(Loop *L);

	//show partition
	void showPartition(Loop *L);

	//show live in and live out set
	void showLiveInfo(Loop *L);

	Utils util;

	//give each instruction a name, including terminator instructions (which can not be setName)
	map<Instruction *, string> dname;

public:
	static char ID;
	DSWP();
	virtual void getAnalysisUsage(AnalysisUsage &AU) const;
	virtual bool runOnLoop(Loop *L, LPPassManager &LPM);
	virtual bool doInitialization(Loop *, LPPassManager &LPM);
	//virtual bool doFinalization();
};

#endif
