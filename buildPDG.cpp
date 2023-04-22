//1st step: create dependence graph

#include "DSWP.h"
#include "llvm/Support/raw_os_ostream.h"

using namespace llvm;
using namespace std;

void DSWP::dfsVisit(BasicBlock *BB, std::set<BasicBlock *> &vis,
					std::vector<BasicBlock *> &ord, Loop *L) {
	vis.insert(BB); 
	for (succ_iterator SI = succ_begin(BB), E = succ_end(BB); SI != E; ++SI)
		if (L->contains(*SI) && vis.find(*SI) == vis.end())
			dfsVisit(*SI, vis, ord, L);

	ord.push_back(BB);
}

void DSWP::buildPDG(Loop *L) {
	errs()<<"Building PDG for new loop\n";
	raw_os_ostream outstream(cout);

	for (Loop::block_iterator bi = L->getBlocks().begin(); bi != L->getBlocks().end(); bi++) {
		BasicBlock *BB = *bi;

		for (BasicBlock::iterator ui = BB->begin(); ui != BB->end(); ui++) {
			Instruction *inst = &(*ui);

			//standardize the name for all expr
			if (inst->getType()->isVoidTy()) {
				dname[inst] = util.genId();
			} else {
				inst->setName(util.genId());
				dname[inst] = inst->getName().str();
			}

			// initialize vectors
			pdg[inst];
			rev[inst];
		}
	}

	errs()<<">>End basic blocks\n";

	/*
	 * Memory dependence analysis
	 */

	AAResults &aa = getAnalysis<AAResultsWrapperPass>().getAAResults();
	MemoryDependenceResults &mda = getAnalysis<MemoryDependenceWrapperPass>().getMemDep();

	for (Loop::block_iterator bi = L->getBlocks().begin(); bi != L->getBlocks().end(); bi++) {
		BasicBlock *BB = *bi;
		for (BasicBlock::iterator ii = BB->begin(); ii != BB->end(); ii++) {
			Instruction *inst = &(*ii);
			inst->print(errs());
			//data dependence = register dependence + memory dependence

			//begin register dependence
			for (auto ui : ii->users()) {
			// for (Value::use_iterator ui = ii->use_begin(); ui != ii->use_end(); ui++) {
				Instruction* a = dyn_cast<Instruction>(ui);
				if (Instruction *user = dyn_cast<Instruction>(ui)) {
					if (L->contains(user)) {
						addEdge(inst, user, REG);
					}
				}
			}
			//finish register dependence

			//begin memory dependence
			if (!inst->mayReadOrWriteMemory()) {
				continue; // not supposed to run getDependency on non-mem insts
			}

			MemDepResult mdr = mda.getDependency(inst);

			if (mdr.isDef()) {
				Instruction *dep = mdr.getInst();

				if (isa<LoadInst>(inst)) {
					//READ AFTER WRITE
					if (isa<StoreInst>(dep)) {
						addEdge(dep, inst, DTRUE);
					}
					//READ AFTER ALLOCATE
					if (isa<AllocaInst>(dep)) {
						addEdge(dep, inst, DTRUE);
					}
				}

				if (isa<StoreInst>(inst)) {
					//WRITE AFTER READ
					if (isa<LoadInst>(dep)) {
						addEdge(dep, inst, DANTI);
					}
					//WRITE AFTER WRITE
					if (isa<StoreInst>(dep)) {
						addEdge(dep, inst, DOUT);
					}
					//WRITE AFTER ALLOCATE
					if (isa<AllocaInst>(dep)) {
						addEdge(dep, inst, DOUT);
					}
				}
				//READ AFTER READ IS INSERT AFTER PDG BUILD
			}

			if (mdr.isClobber()) {
				Instruction *dep = mdr.getInst();
				addEdge(dep, inst, DANTI); // TODO dep type
			}

			if (mdr.isNonLocal()) {
				if (CallInst *call = dyn_cast<CallInst>(inst)) {
					const MemoryDependenceResults::NonLocalDepInfo &rs = mda.getNonLocalCallDependency(call);
					for (MemoryDependenceResults::NonLocalDepInfo::const_iterator
							ri = rs.begin(), re = rs.end(); ri != re; ++ri) {
						const NonLocalDepEntry &entry = *ri;
						Instruction *dep = entry.getResult().getInst();

						if (dep == NULL) {
							continue; // what's going on here?
						} else if (!L->contains(dep)) {
							continue;
						}

						addEdge(dep, inst, DANTI); // TODO: dep type

					}
				} else {
					typedef SmallVector<NonLocalDepResult, 6> res_t;
					res_t res;
					// mda.getNonLocalPointerDependency(
					// 	MemLoc, is_load, inst->getParent(), res);
					mda.getNonLocalPointerDependency(inst, res);

					for (res_t::iterator ri = res.begin(), re = res.end();
							ri != re; ++ri) {
						NonLocalDepResult &r = *ri;
						Instruction *dep = r.getResult().getInst();

						if (dep == NULL) {
							continue; // what's going on here?
						} else if (!L->contains(dep)) {
							continue;
						}

						addEdge(dep, inst, DANTI);
					}
				}
			}
		}
	}

	errs()<<">>Finished finding data dependences\n";


	/*
	 *
	 * Topologically sort blocks and create peeled loop
	 *
	 */

	errs()<<">>Finding control dependences\n";

	BasicBlock *curheader = L->getHeader();
	Function *curfunc = curheader->getParent();
	Module *curmodule = curfunc->getParent();
	LLVMContext *curcontext = &curmodule->getContext();
	FunctionType *functype = FunctionType::get(Type::getVoidTy(*curcontext),
											   false);
	static LLVMContext MyGlobalContext;
	Module newmodule("dummymodule", MyGlobalContext);
	IntegerType *int_arg = IntegerType::get(MyGlobalContext, 32);
	FunctionCallee func = newmodule.getOrInsertFunction("dummyloopunroll",
													Type::getVoidTy(MyGlobalContext),
													Type::getInt32Ty(MyGlobalContext));
	Constant *cfunc = dyn_cast<Constant>(func.getCallee());
	Function *ctrlfunc = cast<Function>(cfunc);

	/*
	Function *ctrlfunc = Function::Create(functype, Function::ExternalLinkage,
										  "dummyloopunroll", module);
	*/

	Function &ctrlfuncref = *ctrlfunc;

	std::set<BasicBlock *> b_visited;
	std::vector<BasicBlock *> bb_ordered;
	std::vector<BasicBlock *> dummylist;

	// DFS to (reverse) topologically sort the basic blocks
	b_visited.clear();
	for (Loop::block_iterator bi = L->getBlocks().begin(),
					be = L->getBlocks().end(); bi != be; bi++) {
		BasicBlock *BB = *bi;
		if (b_visited.find(BB) == b_visited.end()) //Not visited
			dfsVisit(BB, b_visited, bb_ordered, L);
	}

	assert(!bb_ordered.empty());

	std::map<BasicBlock *, std::pair<BasicBlock *, BasicBlock *> > realtodummy;
	std::map<BasicBlock *, BasicBlock *> dummytoreal;
	std::map<BasicBlock *, unsigned int> instnum;
	LLVMContext &ctxt = ctrlfunc->getParent()->getContext();

	//Create dummy basic blocks and populate lookup tables
	if (!bb_ordered.empty()) {
		std::vector<BasicBlock *>::iterator it = bb_ordered.end();
		unsigned int i = 0;
		do {
			--it;
			//Dummy block for first iteration
			const std::string str1 = "tophalf_" + (*it)->getName().str();
			const std::string str2 = "bottomhalf_" + (*it)->getName().str();
			const Twine n1(str1);
			const Twine n2(str2);
			// errs()<<"n1 = "<<n1.str()<<", n2 = "<<n2.str();
			BasicBlock *newbb = BasicBlock::Create(ctxt,
												   n1, ctrlfunc, 0);
			//Dummy block for second iteration
			BasicBlock *newbb2 = BasicBlock::Create(ctxt,
												   n2, ctrlfunc, 0);

			dummylist.push_back(newbb);
			dummylist.push_back(newbb2);

			//Update lookup tables
			realtodummy[*it] = std::make_pair(newbb, newbb2);
			dummytoreal[newbb] = *it;
			dummytoreal[newbb2] = *it;
			instnum[*it] = i;
			i++;
		} while (it != bb_ordered.begin());
	}

	//Create the exit block
	BasicBlock *dummyexitblock = BasicBlock::Create(ctxt,
													"exitblock", ctrlfunc, 0);
	ReturnInst::Create(ctxt, 0, dummyexitblock);

	errs()<<">>Printing out names of dummy blocks inside our fake function\n";

	SmallVector<BasicBlock *, 10> bb_exits;
	std::set<BasicBlock *> returnblocks;

	L->getExitBlocks(bb_exits);
	for (SmallVector<BasicBlock *, 10>::iterator it = bb_exits.begin(),
			 ie = bb_exits.end(); it != ie; ++it) {
		BasicBlock *bbend = *it;
		for (pred_iterator pi = pred_begin(bbend), pe = pred_end(bbend);
				pi != pe; ++pi) {
			if (L->contains(*pi)) //block in loop?
				if (returnblocks.find(*pi) == returnblocks.end()) //to insert?
				{
					errs()<<">>Adding "<<(*pi)->getName().str()<<" as exiting block\n";
					returnblocks.insert(*pi); //Insert block as a return block
				}
		}
	}

	//Add branch instructions for dummy blocks
	if (!bb_ordered.empty()) {
		std::vector<BasicBlock *>::iterator it = bb_ordered.end();
		do {
			--it;
			std::pair<BasicBlock *, BasicBlock *> dummypair = realtodummy[*it];
			BasicBlock *bbdummy1 = dummypair.first;
			BasicBlock *bbdummy2 = dummypair.second;
			IRBuilder<> builder1(bbdummy1);
			IRBuilder<> builder2(bbdummy2);

			Instruction *tinst = (*it)->getTerminator();
			unsigned nsucc = tinst->getNumSuccessors();
			SwitchInst *SI1, *SI2;
			bool swcreated1 = false, swcreated2 = false;

			//Deal with exit blocks
			if (returnblocks.find(*it) != returnblocks.end())
			{
				SI1 = builder1.CreateSwitch(ConstantInt::get(
						Type::getInt64Ty(MyGlobalContext), 0),
						dummyexitblock, nsucc+1);
				SI2 = builder2.CreateSwitch(ConstantInt::get(
						Type::getInt64Ty(MyGlobalContext), 0),
						dummyexitblock, nsucc+1);

				SI1->addCase(ConstantInt::get(
						Type::getInt64Ty(MyGlobalContext), 0),
						dummyexitblock);
				SI2->addCase(ConstantInt::get(
						Type::getInt64Ty(MyGlobalContext), 0),
						dummyexitblock);

				swcreated1 = true;
				swcreated2 = true;
			}


			for (unsigned i = 0; i < nsucc; i++) {
				BasicBlock *bsucc = tinst->getSuccessor(i);

				if (L->contains(bsucc)) { //successor is still inside loop?
					BasicBlock *destblock1;
					BasicBlock *destblock2 = realtodummy[bsucc].second;

					if (instnum[bsucc] <= instnum[*it]) //points to earlier block
						destblock1 = realtodummy[bsucc].second;
					else //points to a latter block
						destblock1 = realtodummy[bsucc].first;

					if (!swcreated1) { //need to create switch instruction for top half
						SI1 = builder1.CreateSwitch(ConstantInt::get(
								Type::getInt64Ty(MyGlobalContext), 0),
						        destblock1, nsucc);
						swcreated1 = true;
					}

					//Add a case to the switch instruction for the top half
					SI1->addCase(ConstantInt::get(
							Type::getInt64Ty(MyGlobalContext), i+1),
							destblock1);

					//Now, deal with instruction for bottom half
					if (!swcreated2) { //need to create switch instr. for bottom half
						SI2 = builder2.CreateSwitch(ConstantInt::get(
								Type::getInt64Ty(MyGlobalContext), 0),
								destblock2, nsucc);
						swcreated2 = true;
					}

					//Add a case to the switch instruction for the bottom half
					SI2->addCase(ConstantInt::get(
							Type::getInt64Ty(MyGlobalContext), i+1),
							destblock2);

				}
			}
		} while (it != bb_ordered.begin());
	}

	errs()<<">>Printing out FUNCTION ctrlfunc:\n";
	ctrlfunc->print(errs());
	/*
	 *
	 * Begin control dependence calculation
	 *
	 */

	PostDominatorTreeWrapperPass pdtw;
	pdtw.runOnFunction(ctrlfuncref);

	PostDominatorTree &pdt = pdtw.getPostDomTree();

	for (std::vector<BasicBlock *>::iterator it = dummylist.begin();
		 it != dummylist.end(); ++it)
	{
		Instruction *tinst = (*it)->getTerminator();
		unsigned nsucc = tinst->getNumSuccessors();

		for (unsigned i = 0; i < nsucc; i++) {
			BasicBlock *bsucc = tinst->getSuccessor(i);

			//Find LCA of two nodes in the post-dominator tree
			DomTreeNode *succnode = pdt.getNode(bsucc);
			BasicBlock *realblock = dummytoreal[*it];
			DomTreeNode *dn = pdt.getNode(pdt.findNearestCommonDominator(*it,
											bsucc));
			BasicBlock *depblock = succnode->getBlock();

			while (succnode != dn && depblock != dummyexitblock)
			{
				BasicBlock *realdepblock = dummytoreal[depblock];
				Instruction *ti = realblock->getTerminator();
				for (BasicBlock::iterator bi = realdepblock->begin(),
						 be = realdepblock->end(); bi != be; ++bi) {
					Instruction *inst = &(*bi);
					addEdge(ti, inst, CONTROL);
				}

				succnode = succnode->getIDom();
				depblock = succnode->getBlock();
			}
		}
	}
	//Note that our dummy function and its underlying blocks and instructions
	//will automatically be deleted
}
