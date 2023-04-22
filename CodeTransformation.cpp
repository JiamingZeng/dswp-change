//4th step: code splitting

#include "DSWP.h"
#include "llvm/IR/IRBuilder.h"

using namespace llvm;
using namespace std;


void DSWP::preLoopSplit(Loop *L) {
	allFunc.clear();


	/*
	 * Insert a new block to replace the old loop
	 */
	replaceBlock = BasicBlock::Create(*context, "loop-replace", func);
	BranchInst *brInst = BranchInst::Create(exit, replaceBlock);
	replaceBlock->moveBefore(exit);

	// point branches to the loop header to replaceBlock instead
	for (pred_iterator PI = pred_begin(header); PI != pred_end(header); ++PI) {
		BasicBlock *pred = *PI;
		if (L->contains(pred)) {
			continue;
		}
		Instruction *termInst = pred->getTerminator();

		for (unsigned int i = 0; i < termInst->getNumOperands(); i++) {
			BasicBlock *bb = dyn_cast<BasicBlock>(termInst->getOperand(i));
			if (bb == header) {
				termInst->setOperand(i, replaceBlock);
			}
		}
	}

	// sanity check: nothing in the loop branches to the new replacement block
	// you know, in case we don't trust Loop::contains or something
	// NOTE: kind of pointless
	for (Loop::block_iterator li = L->block_begin(), le = L->block_end();
			li != le; li++) {
		BasicBlock *BB = *li;
		if (BB == replaceBlock) {
			error("the block should not appear here!");
		}
	}

	/*
	 * add functions for the worker threads
	 */

	Type *void_ty = Type::getVoidTy(*context),
	     *int32_ty = Type::getInt32Ty(*context),
	     *int64_ty = Type::getInt64Ty(*context),
	     *int8_ptr_t = Type::getInt8PtrTy(*context);

	// types for the functions: void* -> void*, b/c that's what pthreads wants
	vector<Type *> funArgTy;
	funArgTy.push_back(int8_ptr_t);
	FunctionType *fType = FunctionType::get(int8_ptr_t, funArgTy, false);

	// add the actual functions for each thread
	for (int i = 0; i < MAX_THREAD; i++) {
		llvm::FunctionCallee fc = module->getOrInsertFunction(
				itoa(loopCounter) + "_subloop_" + itoa(i), fType);
		Constant* c = dyn_cast<Constant>(fc.getCallee());
		if (c == NULL) {  // NOTE: don't think this is possible...?
			error("no function!");
		}
		Function *func = cast<Function>(c);
		func->setCallingConv(CallingConv::C);
		allFunc.push_back(func);
		generated.insert(func);
	}


	/*
	 * construct the argument struct
	 */

	// create a struct type to store the values of liveout vars in
	vector<Type *> liveoutTypes;
	for (unsigned int i = 0; i < liveout.size(); i++) {
		liveoutTypes.push_back(liveout[i]->getType());
	}
	StructType *outStructTy = StructType::create(
		*context, liveoutTypes, "outstruct_" + itoa(loopCounter) + "_ty");

	// create a struct type for the arguments to worker functions
	vector<Type *> argTypes;
	for (unsigned int i = 0; i < livein.size(); i++) {
		argTypes.push_back(livein[i]->getType());
	}
	argTypes.push_back(outStructTy);
	argStructTy = StructType::create(
		*context, argTypes, "argstruct_" + itoa(loopCounter) + "_ty");

	// allocate the argument struct
	AllocaInst *argStruct = new AllocaInst(argStructTy, 0, "argstruct", brInst);

	// store the livein arguments
	for (unsigned int i = 0; i < livein.size(); i++) {
		// get the element pointer where we're storing this argument
		vector<Value *> gep_args;
		gep_args.push_back(ConstantInt::get(int64_ty, 0));
		gep_args.push_back(ConstantInt::get(int32_ty, i));
		GetElementPtrInst *ele_addr = GetElementPtrInst::CreateInBounds(
			argStructTy, argStruct, gep_args, livein[i]->getName() + "_argptr", brInst);

		StoreInst *storeVal = new StoreInst(livein[i], ele_addr, brInst);
	}
	// NOTE: the output argument struct is left uninitialized

	/*
	 * initialize the communication queues
	 */
	Function *init = module->getFunction("sync_init");
	CallInst *callInit = CallInst::Create(init, "", brInst);


	/*
	 * call the worker functions
	 */
	Function *delegate = module->getFunction("sync_delegate");
	CastInst *argStruct_voidPtr = CastInst::CreatePointerCast(
		argStruct, int8_ptr_t,
		"argstruct_" + itoa(loopCounter) + "_cast", brInst);

	for (int i = 0; i < MAX_THREAD; i++) {
		vector<Value*> args;
		args.push_back(ConstantInt::get(int32_ty, i)); // the thread id
		args.push_back(allFunc[i]); // the function pointer
		args.push_back(argStruct_voidPtr); // the argument struct
		CallInst * callfunc = CallInst::Create(delegate, args, "", brInst);
	}


	/*
	 * join them back
	 */
	Function *join = module->getFunction("sync_join");
	CallInst *callJoin = CallInst::Create(join, "", brInst);

	/*
	 * load the liveout variables from the out struct
	 */
	if (!liveout.empty()) {
		// get the pointer to the output structure
		vector<Value *> gep_args;
		gep_args.push_back(ConstantInt::get(int64_ty, 0));
		gep_args.push_back(ConstantInt::get(int32_ty, livein.size()));
		GetElementPtrInst *out_addr = GetElementPtrInst::CreateInBounds(
			argStructTy, argStruct, gep_args, "load_outs", brInst);

		map<Value*, Value*> replacement_map;

		for (unsigned int i = 0; i < liveout.size(); i++) {
			// get the pointer to this out value
			gep_args.clear();
			gep_args.push_back(ConstantInt::get(int64_ty, 0));
			gep_args.push_back(ConstantInt::get(int32_ty, i));
			GetElementPtrInst *ele_addr = GetElementPtrInst::CreateInBounds(
				outStructTy, out_addr, gep_args, liveout[i]->getName() + "_ptr", brInst);

			// load the out value
			// need to fix
			LoadInst *outVal = new LoadInst(
				ele_addr->getType(), ele_addr, liveout[i]->getName() + "_load", brInst);

			replacement_map[liveout[i]] = outVal;
		}

		// replace any uses *outside of the loop* with this load instruction
		for (Function::iterator bi = func->begin(), be = func->end();
				bi != be; ++bi) {
			BasicBlock &bb = *bi;
			if (!L->contains(&bb)) {
				for (BasicBlock::iterator ii = bb.begin(), ie = bb.end();
						ii != ie; ++ii) {
					replaceUses(&(*ii), replacement_map);
				}
			}
		}
	}
}


void DSWP::loopSplit(Loop *L) {
	//check for each partition, find relevant blocks, set could auto deduplicate

	for (int i = 0; i < MAX_THREAD; i++) {
		// cout << "// Creating function for thread " + itoa(i) << endl;

		// create function body for each thread
		Function *curFunc = allFunc[i];


		/*
		 * figure out which blocks we use or are dependent on in this thread
		 */
		set<BasicBlock *> relbb;
		set<Instruction *> relIns;
		//relbb.insert(header);
		for (vector<int>::iterator ii = part[i].begin(), ie = part[i].end();
				ii != ie; ++ii) {
			int scc = *ii;
			for (vector<Instruction *>::iterator iii = InstInSCC[scc].begin(),
												 iie = InstInSCC[scc].end();
					iii != iie; ++iii) {
				Instruction *inst = *iii;
				relbb.insert(inst->getParent());
				relIns.insert(inst);
				// add blocks which the instruction is dependent on

				const vector<Edge> &edges = rev[inst];
				for (vector<Edge>::const_iterator ei = edges.begin(),
												  ee = edges.end();
						ei != ee; ei++) {
					Instruction *dep = ei->v;
					// errs() << "inserted!\n";
					relbb.insert(dep->getParent());
				}
			}
		}

		if (relbb.empty()) {
			BasicBlock *newBody =
				BasicBlock::Create(*context, "do-nothing", curFunc);
			ReturnInst *newRet = ReturnInst::Create(
				*context,
				Constant::getNullValue(Type::getInt8PtrTy(*context)),
				newBody);
			return;
		}

		/*
		 * Create the new blocks for the new function, including entry and exit
		 */
		map<BasicBlock *, BasicBlock *> BBMap; // map old blocks to new block

		BasicBlock *newEntry =
				BasicBlock::Create(*context, "new-entry", curFunc);
		BasicBlock *newExit = BasicBlock::Create(*context, "new-exit", curFunc);


		//BBMap[header]->getInstList().insert(BBMap[header]->begin(), bi);
		// make copies of the basic blocks
		for (set<BasicBlock *>::iterator bi = relbb.begin(), be = relbb.end();
				bi != be; ++bi) {
			BasicBlock *BB = *bi;
			BBMap[BB] = BasicBlock::Create(*context,
					BB->getName().str() + "_" + itoa(i), curFunc, newExit);
		}
		BBMap[predecessor] = newEntry;
		BBMap[exit] = newExit;

		// errs() << "this must be a error early in dependency analysis stage \n";
		// branch from the entry block to the new header
		BranchInst *newToHeader = BranchInst::Create(BBMap[header], newEntry);

		// return null
		ReturnInst *newRet = ReturnInst::Create(*context,
				Constant::getNullValue(Type::getInt8PtrTy(*context)), newExit);

		/*
		 * copy over the instructions in each block
		 */
		typedef SmallVector<Instruction *, 5> to_point_t;
		to_point_t instructions_to_point;
		for (set<BasicBlock *>::iterator bi = relbb.begin(), be = relbb.end();
				bi != be; bi++) {
			BasicBlock *BB = *bi;
			BasicBlock *NBB = BBMap[BB];
			//errs() << "1 \n";
			for (BasicBlock::iterator ii = BB->begin(), ie = BB->end();
					ii != ie; ii++) {
				Instruction *inst = dyn_cast<Instruction>(ii);
				//errs() << "2 \n";
				if (assigned[sccId[inst]] != i && !isa<Instruction>(inst)) {
					// We're not actually inserting this function, but we want
					// to keep track of where it would have gone. We'll point it
					// at the next instruction we actually do insert. (Note
					// we'll always actually insert these, because at a minimum
					// we're inserting the terminator from this block.)
					instructions_to_point.push_back(inst);
					continue;
				}

				// add code
				if(relIns.find(inst) == relIns.end() && !isa<BranchInst>(inst) ){
					// errs() << " avoid duplicate for ";
					// inst->print(errs());
					// errs() << " \n";
					continue;
				}

				Instruction *newInst = inst->clone();
				if (inst->hasName()) {
					newInst->setName(inst->getName() + "_" + itoa(i));
				}
				//errs() << "3 \n";
				// re-point branches and such to new blocks
				if (BranchInst *newT = dyn_cast<BranchInst>(newInst)) {
					//errs() << "3.5 \n";
					unsigned int num_suc = newT->getNumSuccessors();
					//errs() << "4 \n";
					// re-point successor blocks
					for (unsigned int j = 0; j < num_suc; j++) {
						BasicBlock *oldBB = newT->getSuccessor(j);
						BasicBlock *newBB = BBMap[oldBB];

						if (oldBB != exit && !L->contains(oldBB)) {
							// branching to a block outside the loop that's
							// not the exit. this should be impossible...
							//errs() << "5 \n";
							continue;
						}

						// if we branched to a block not in this thread,
						// go to the next post-dominator
						while (newBB == NULL) {
							oldBB = postidom[oldBB];
							newBB = BBMap[oldBB];
							if (oldBB == NULL) {
								// error("postdominator info seems broken :(");
								break;
							}
						}
						//errs() << "10 \n";
						// replace the target
						newT->setSuccessor(j, newBB);
					}

					// Is this a branch, switch, or indirect branch that now
					// always goes to the same block regardless of the
					// comparison value? If so, make it unconditional.
					if (num_suc > 0 && !isa<InvokeInst>(newT)) {
						BasicBlock *target = newT->getSuccessor(0);
						for (unsigned int j = 1; j < num_suc; j++) {
							BasicBlock *t = newT->getSuccessor(j);
							if (t != target) {
								target = NULL;
								break;
							}
						}
						if (target != NULL) {
							// delete newInst;
							newInst = BranchInst::Create(target);
						}
					}

				} else if (PHINode *phi = dyn_cast<PHINode>(newInst)) {
					// re-point block predecessors of phi nodes
					// values will be re-pointed later on
					for (unsigned int j = 0, je = phi->getNumIncomingValues();
							j < je; j++) {
						BasicBlock *oldBB = phi->getIncomingBlock(j);
						BasicBlock *newBB = BBMap[oldBB];

						// if we're branching from a block not in this thread,
						// go to the previous dominator of that block
						// NOTE: is this the right thing to do?
						while (newBB == NULL) {
							oldBB = idom[oldBB];
							newBB = BBMap[oldBB];
							if (oldBB == NULL) {
								// error("dominator info seems broken :(");
								break;
							}
						}

						// replace the previous target block
						phi->setIncomingBlock(j, newBB);
					}
				}

				// SCC: vector of intrs part[i] 
				// Inst : vector of inst SCC 
				// for (int part[i])

				
				// errs() << "instruction currently dealing with: ";
				// inst->print(errs());
				// errs() << "\n";
				
				instMap[i][inst] = newInst;
				newInstAssigned[newInst] = i;
				newToOld[newInst] = inst;
				// newInst->dump();
				// find part[i]'s instruction
				

				NBB->getInstList().push_back(newInst);
				for (to_point_t::iterator pi = instructions_to_point.begin(),
										  pe = instructions_to_point.end();
						pi != pe; ++pi) {
					Instruction *p = *pi;
					placeEquivalents[i][p] = newInst;
				}
				instructions_to_point.clear();
			}

			// if (!instructions_to_point.empty()) {
			// 	error("didn't point all the instructions we wanted to");
			// }
		}
		/*
		 * Load the arguments, replacing livein variables
		 */

		// if (curFunc->arg_size() != 1) {
		// 	errs() <<"argument size error!";
		// 	error("argument size error!");
		// }
		// errs() << "find arg lists \n";
		Argument *args = curFunc->arg_begin(); //the function only have one argmument

		Function *showPlace = module->getFunction("showPlace");
		CallInst *inHeader = CallInst::Create(showPlace);
		inHeader->insertBefore(newToHeader);

		BitCastInst *castArgs = new BitCastInst(
				args, PointerType::get(argStructTy, 0), "args");
		castArgs->insertBefore(newToHeader);

		for (unsigned int j = 0, je = livein.size(); j < je; j++) {
			// get pointer to the jth argument
			vector<Value *> gep_args;
			gep_args.push_back(ConstantInt::get(Type::getInt64Ty(*context), 0));
			gep_args.push_back(ConstantInt::get(Type::getInt32Ty(*context), j));
			GetElementPtrInst* ele_addr = GetElementPtrInst::Create(
				argStructTy, castArgs, gep_args, livein[j]->getName() + "_arg", newToHeader);

			// load it
			LoadInst *ele_val = new LoadInst(ele_addr->getType(), ele_addr, "", newToHeader);
			ele_val->setAlignment(Align(8)); // TODO: do we want this?
			ele_val->setName(livein[j]->getName().str() + "_val");

			if (ele_val->getType() != livein[j]->getType()) {
				error("broken type for " + livein[j]->getName().str());
			}

			instMap[i][livein[j]] = ele_val;
		}

		/*
		 * Replace the use of instruction def in the function.
		 * reg dep should be finished in insert syn
		 */
		for (inst_iterator ii = inst_begin(curFunc), ie = inst_end(curFunc);
				ii != ie; ++ii) {
			Instruction *inst = &(*ii);
			replaceUses(inst, instMap[i]);
			errs() << "replaced inst";
			inst->print(errs());
			errs() << "\n";
		}

		/*
		 * Store any liveout variables defined in this thread.
		 */
		Type *int32_ty = Type::getInt32Ty(*context),
		     *int64_ty = Type::getInt64Ty(*context);

		// get the pointer to the output structure
		vector<Value *> gep_args;
		gep_args.push_back(ConstantInt::get(int64_ty, 0));
		gep_args.push_back(ConstantInt::get(int32_ty, livein.size()));
		GetElementPtrInst *out_addr = GetElementPtrInst::CreateInBounds(
			argStructTy, castArgs, gep_args, "load_outs", newRet);

		for (unsigned int j = 0; j < liveout.size(); j++) {
			if (getInstAssigned(liveout[j]) == i) {
				// get the pointer where we want to save this
				gep_args.clear();
				gep_args.push_back(ConstantInt::get(int64_ty, 0));
				gep_args.push_back(ConstantInt::get(int32_ty, j));
				GetElementPtrInst *ele_addr = GetElementPtrInst::CreateInBounds(
					out_addr->getType(), out_addr, gep_args, liveout[j]->getName() + "_outptr", newRet);

				// save it
				StoreInst *store = new StoreInst(
					instMap[i][liveout[j]], ele_addr, newRet);
			}
		}
	}
}

void DSWP::getDominators(Loop *L) {
	// TODO: if we're running DSWP on more than one loop in a single function,
	//       this will be invalidated the second time through and segfault when
	//       getNode(BB) returns null for loop-replace.
	DominatorTree &dom_tree = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
	PostDominatorTree &postdom_tree = getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();

	for (Function::iterator bi = func->begin(); bi != func->end(); bi++) {
		BasicBlock *BB = dyn_cast<BasicBlock>(bi);

		DomTreeNode *idom_node = dom_tree.getNode(BB)->getIDom();
		idom[BB] = idom_node == NULL ? NULL : idom_node->getBlock();

		DomTreeNode *postidom_node = postdom_tree.getNode(BB)->getIDom();
		postidom[BB] = postidom_node == NULL ? NULL : postidom_node->getBlock();
	}
}


void DSWP::getLiveinfo(Loop *L) {
	defin.clear();
	livein.clear();
	liveout.clear();

	// Figure out which variables are live.
	// Don't want to use standard liveness analysis....

	// Find variables defined in the loop, and those that are used outside
	// the loop.
	for (Loop::block_iterator bi = L->getBlocks().begin(),
							  be = L->getBlocks().end();
			bi != be; ++bi) {
		BasicBlock *BB = *bi;
		for (BasicBlock::iterator ii = BB->begin(), ie = BB->end();
				ii != ie; ++ii) {
			Instruction *inst = &(*ii);
			if (!inst->getType()->isVoidTy()) {
				defin.push_back(inst);
			}
			bool already_liveouted = false;
			for (Instruction::use_iterator ui = inst->use_begin(), ue = inst->use_end(); ui != ue; ++ui) {
				User *use = dyn_cast<User>(*ui);
				if (Instruction *use_i = dyn_cast<Instruction>(use)) {
					if (!already_liveouted && !L->contains(use_i)) {
						liveout.push_back(inst);
						already_liveouted = true;
					}
				} else {
					error("used by something that's not an instruction???");
				}
			}
		}
	}

	// Anything used in the loop that's not defined in it is a livein variable.
	for (Loop::block_iterator bi = L->getBlocks().begin(),
							  be = L->getBlocks().end();
			bi != be; ++bi) {
		BasicBlock *BB = *bi;
		for (BasicBlock::iterator ii = BB->begin(), ie = BB->end();
				ii != ie; ++ii) {
			Instruction *inst = &(*ii);

			for (Instruction::op_iterator oi = inst->op_begin(),
										  oe = inst->op_end();
					oi != oe; ++oi) {
				Value *op = *oi;
				if ((isa<Instruction>(op) || isa<Argument>(op)) &&
						find(defin.begin(), defin.end(), op) == defin.end() &&
						find(livein.begin(), livein.end(), op) == livein.end()) {
					// the operand is a variable that isn't defined inside the
					// loop and hasn't already been counted as livein
					livein.push_back(op);
				}
			}
		}
	}
}

void DSWP::insertSynchronization(Loop *L) {
	cout << "inserting sychronization" << endl << endl;
	int channel = 0;

	for (unsigned int i = 0; i < allEdges.size(); i++) {
		const Edge &e = allEdges[i];

		int utr = getInstAssigned(e.u);

		for (int vtr = utr + 1; vtr < MAX_THREAD; vtr++) {
			// Legal produce/consume dependences only go *upwards* in thread
			// counts. If it goes the other way, fuhgeddaboutit.

			Value *vu = instMap[utr][e.u];
			Value *vv = instMap[vtr][e.v];
			if (vu == NULL || vv == NULL)
				continue;

			Instruction *nu = dyn_cast<Instruction>(vu);
			Instruction *nv = dyn_cast<Instruction>(vv);
			if (nu == NULL || nv == NULL)
				continue;

			if (isa<BranchInst>(nu)) {
				continue;
			}
			insertProduce(nu, nv, e.dtype, channel, utr, vtr);
			insertConsume(nu, nv, e.dtype, channel, utr, vtr);
			channel++;
		}
	}
}


void DSWP::insertProduce(Instruction *u, Instruction *v, DType dtype,
					     int channel, int uthread, int vthread) {
	Instruction *oldu = dyn_cast<Instruction>(newToOld[u]);	
	Function *fun = module->getFunction("sync_produce");
	vector<Value *> args;

	Instruction *insPos = u->getNextNode();
	Instruction* insPos1 = placeEquivalents[uthread][oldu];

	if (insPos == NULL) {
		error("here cannot be null");
	}

	if (dtype == REG) {	// register dep
		// cast the value to something that the communication guy likes
		CastInst *cast;

		if (u->getType()->isIntegerTy()) {
			cast = new ZExtInst(u, eleType, u->getName().str() + "_64");
		} else if (u->getType()->isFloatingPointTy()) {
			if (u->getType()->isFloatTy()) {
				cout << "WARNING: float sucks?";
			}
			cast = new BitCastInst(u, eleType, u->getName().str() + "_64");
		} else if (u->getType()->isPointerTy()){
			cast = new PtrToIntInst(u, eleType, u->getName().str() + "_64");
		} else {
			error("what's the hell type");
		}

		cast->insertBefore(insPos);
		args.push_back(cast);
	} else if (dtype == DTRUE) { // true dep
		error("check mem dep!!");

		StoreInst *store = dyn_cast<StoreInst>(u);
		if (store == NULL) {
			error("not true dependency!");
		}
		BitCastInst *cast = new BitCastInst(store->getOperand(0),
					Type::getInt8PtrTy(*context), u->getName().str() + "_ptr");
		cast->insertBefore(insPos);
		args.push_back(cast);
	} else { // others
		// just send a dummy value for synchronization
		args.push_back(Constant::getNullValue(Type::getInt64Ty(*context)));
	}

	// channel ID
	args.push_back(ConstantInt::get(Type::getInt32Ty(*context), channel));

	// make the actual call
	CallInst *call = CallInst::Create(fun, args, "", insPos);
}


void DSWP::insertConsume(Instruction *u, Instruction *v, DType dtype,
					     int channel, int uthread, int vthread) {
	Instruction *oldu = dyn_cast<Instruction>(newToOld[u]);

	Instruction *insPos = v;
	Instruction *oldv = dyn_cast<Instruction>(newToOld[v]);
	
	Function *fun = module->getFunction("sync_consume");
	vector<Value *> args;
	args.push_back(ConstantInt::get(Type::getInt32Ty(*context), channel));
	CallInst *call = CallInst::Create(fun, args, "c" + itoa(channel), insPos);

	if (dtype == REG) {
		CastInst *cast;
		string name = call->getName().str() + "_val";

		// add branch dealing
		if (u->getType()->isIntegerTy()) {
			errs() << "isIntegerTy \n";
			cast = new TruncInst(call, u->getType(), name);
		}
		else if (u->getType()->isFloatingPointTy()) {
			errs() << "isFloatingPointTy \n";
			if (u->getType()->isFloatTy())
				error("cannot deal with double");
			cast = new BitCastInst(call, u->getType(), name);
		}
		else if (u->getType()->isPointerTy()){
			errs() << "isPointerTy \n";
			cast = new IntToPtrInst(call, u->getType(), name);
		} else {
			error("what's the hell type");
		}

		cast->insertBefore(insPos);
		// replace the uses
		bool flag = false;

		Value* replace_inst = instMap[vthread][oldu];
		Instruction* replace_inst_use;
		if (replace_inst == NULL) 
			replace_inst_use = oldu;
		else 
			replace_inst_use = dyn_cast<Instruction>(replace_inst);
		
		if(!flag){
			errs() << "33\n";
			for (auto U : replace_inst_use->users()){ // U is of type User *
				if (auto I = dyn_cast<Instruction>(U)) {
					if (I->getParent()->getParent()->getName().back()-'0' == vthread) {
						//an instruction uses V
						map<Value *, Value *> reps;
						// reps[oldu] = cast;
						Value* u_LHS = dyn_cast<Value>(dyn_cast<Instruction>(replace_inst_use));
						reps[u_LHS] = cast;
						replaceUses(I, reps);
					}
				}
			}
		}
	} 
	// TODO: need to handle true memory dependences more than just syncing?
	else if (dtype == DTRUE) {	//READ after WRITE
		if (!isa<LoadInst>(v)) {
			error("not true dependency");
		}
		BitCastInst *cast = new BitCastInst(
			call, v->getType(), call->getName().str() + "_ptr");
		cast->insertBefore(v);

		// replace the v with 'cast' in v's thread:
		// (other thread with be dealed using dependence)
		for (Instruction::use_iterator ui = v->use_begin(), ue = v->use_end();
				ui != ue; ui++) {
			Instruction *user = dyn_cast<Instruction>(*ui);

			if (user == NULL) {
				error("how could it be NULL");
			}

			if (user->getParent()->getParent() != v->getParent()->getParent()) {
				continue;
			}

			for (unsigned i = 0; i < user->getNumOperands(); i++) {
				Value * op = user->getOperand(i);
				if (op == v) {
					user->setOperand(i, cast);
				}
			}
		}
	}
}

void DSWP::cleanup(Loop *L, LPPassManager &LPM) {
	// Move some instructions that may not have been inserted in the right
	// place, delete the old loop, and clean up our aux data structures for this
	// loop.

	/*
	 * move the produce instructions, which have been inserted after the branch,
	 * in front of it
	 */
	for (int i = 0; i < MAX_THREAD; i++) {
		for (Function::iterator bi = allFunc[i]->begin(),
								be = allFunc[i]->end();
				bi != be; ++bi) {
			BasicBlock *bb = dyn_cast<BasicBlock>(bi);
			Instruction *term = NULL;
			for (BasicBlock::iterator ii = bb->begin(), ie = bb->end();
					ii != ie; ++ii) {
				Instruction *inst = dyn_cast<Instruction>(ii);
				inst->print(errs());
				if (isa<Instruction>(inst)) {
					term = dyn_cast<Instruction>(inst);
					break;
				}
			}

			if (term == NULL) {
				errs() << "i:" << i;
				error("term cannot be null");
			}

			while (true) {
				errs() << "while true";
				Instruction *last = &bb->getInstList().back();
				if (isa<Instruction>(last))
					break;
				last->moveBefore(term);
			}
			errs() << "break";
		}
	}

	/*
     * move the phi nodes to the top of the block
	 */
	for (int i = 0; i < MAX_THREAD; i++) {
		for (Function::iterator bi = allFunc[i]->begin(),
								be = allFunc[i]->end();
				bi != be; ++bi) {
			BasicBlock *bb = dyn_cast<BasicBlock>(bi);
			Instruction *first_nonphi = bb->getFirstNonPHI();

			BasicBlock::iterator ii = bb->begin(), ie = bb->end();
			// advance the iterator up to one past first_nonphi
			while (&(*ii) != first_nonphi) { ++ii; errs() << "here";}
			// errs() << "break out";
			++ii;

			// move any phi nodes after the first nonphi to before it
			for (BasicBlock::iterator i_next; ii != ie; ii = i_next) {
				i_next = ii;
				++i_next;

				Instruction *inst = dyn_cast<Instruction>(ii);
				if (isa<PHINode>(inst)) {
					inst->moveBefore(first_nonphi);
				}
			}
		}
	}

	cout << "begin to delete loop" << endl;
	for (Loop::block_iterator bi = L->block_begin(), be = L->block_end();
			bi != be; ++bi) {
		BasicBlock *BB = *bi;
		for (BasicBlock::iterator ii = BB->begin(), i_next, ie = BB->end();
				ii != ie; ii = i_next) {
			i_next = ii;
			++i_next;
			Instruction &inst = *ii;
			inst.replaceAllUsesWith(UndefValue::get(inst.getType()));
			inst.eraseFromParent();
		}
	}

	// Delete the basic blocks only afterwards
	// so that backwards branch instructions don't break
	for (Loop::block_iterator bi = L->block_begin(), be = L->block_end();
			bi != be; ++bi) {
		BasicBlock *BB = *bi;
		BB->eraseFromParent();
	}

	LPM.markLoopAsDeleted(*L);

}

void DSWP::clear() {
	module = NULL;
	func = NULL;
	header = NULL;
	predecessor = NULL;
	exit = NULL;
	context = NULL;
	eleType = NULL;
	replaceBlock = NULL;
	argStructTy = NULL;

	sccNum = 0;

	pdg.clear();
	rev.clear();
	scc_dependents.clear();
	scc_parents.clear();
	dag_added.clear();
	allEdges.clear();
	InstInSCC.clear();
	idom.clear();
	postidom.clear();
	sccId.clear();
	used.clear();
	list.clear();
	assigned.clear();
	for (int i = 0; i < MAX_THREAD; i++) {
		part[i].clear();
		instMap[i].clear();
		placeEquivalents[i].clear();
	}
	sccLatency.clear();
	newToOld.clear();
	newInstAssigned.clear();
	allFunc.clear();
	livein.clear();
	defin.clear();
	liveout.clear();
	dname.clear();
}