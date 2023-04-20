// 5th step - inserting thread synchronization

#include "DSWP.h"

using namespace llvm;
using namespace std;

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

			cout << "SYN: channel " << channel << ": "
			     << dname[e.u]
				 << " -> " << dname[e.v]
				 << " [" << e.dtype << "]" << endl;
			
			if (isa<BranchInst>(nu)) {
				continue;
			}
			errs() << "produce u\n";
			nu->print(errs());
			errs() << "v\n";
			nv->print(errs());
			errs() << "\n";
			insertProduce(nu, nv, e.dtype, channel, utr, vtr);
			errs() << "consume\n";

			insertConsume(nu, nv, e.dtype, channel, utr, vtr);
			channel++;
		}
	}
}


void DSWP::insertProduce(Instruction *u, Instruction *v, DType dtype,
					     int channel, int uthread, int vthread) {
	Function *fun = module->getFunction("sync_produce");
	vector<Value *> args;

	errs() << "output";
	Instruction *insPos = u->getNextNode();
	
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
	// TODO: for true memory dependences, need to send anything or just sync?
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
	Instruction *insPos = placeEquivalents[vthread][oldu];
	if (insPos == NULL) {
		if (instMap[vthread][oldu] == NULL) {
			errs() << "no old u";
			return;
		}
		insPos = dyn_cast<Instruction>(instMap[vthread][oldu]);
		if (insPos == NULL) {
			error("can't insert nowhere");
		}
	}

	// call sync_consume(channel)
	Function *fun = module->getFunction("sync_consume");
	vector<Value *> args;
	args.push_back(ConstantInt::get(Type::getInt32Ty(*context), channel));
	CallInst *call = CallInst::Create(fun, args, "c" + itoa(channel), insPos);

	if (dtype == REG) {
		CastInst *cast;
		string name = call->getName().str() + "_val";


		// add branch dealing
		if (isa<BranchInst>(v)) {
			errs() << "Branch instruction cast!!!!! \n";
			cast = new TruncInst(call, Type::getInt1Ty(*context), name);
			// cast->print(errs());
		}
		else if (u->getType()->isIntegerTy()) {
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

		errs() << "All the cast ";
		cast->print(errs());
		errs() << "\n";
		errs() << "Where u is ";
		u->print(errs());
		errs() << "\n";
		// errs() << "And type of u is " << u->getType()->print() << "\n";
		cast->insertBefore(insPos);
		
		errs() << "cast is ";
		cast->print(errs()); 
		errs() << "\n";
		errs() << "insPos is ";
		insPos->print(errs()); 
		errs() << "\n";

		// replace the uses
		for (auto U : v->users()){ // U is of type User *
			if (auto I = dyn_cast<Instruction>(U)) {
				cout << "entered\n";
				errs() << "v " << *v << "\n";
				errs() << "\nFunction" << *I << "\n";
				errs() << "users information";
				I->print(errs());
				//an instruction uses V
				map<Value *, Value *> reps;
				// reps[oldu] = cast;
				Value* u_LHS = dyn_cast<Value>(oldu);
				errs() << "u_LHS is ";
				u_LHS->print(errs());
				errs() << "\n";

				reps[u_LHS] = cast;
				replaceUses(I, reps);
			}
		}

		// for (Instruction::use_iterator ui = v->use_begin(),
		// 							   ue = v->use_end();
		// 		ui != ue; ++ui) {
		// 	cout << "entered" << endl;
		// 	Instruction *user = dyn_cast<Instruction>(*ui);
		// 	if (user == NULL) {
		// 		error("used by a non-instruction?");
		// 	}

		// 	// make sure it's in the same function...
		// 	// cout << "user func name" << u->getParent()->getParent()->getName().str() << endl;
		// 	// cout << "v func name" << v->getParent()->getParent()->getName().str() << endl;
		// 	if (user->getParent()->getParent() != v->getParent()->getParent()) {
		// 		continue;
		// 	}

		// 	// call replaceUses so that it handles phi nodes
		// 	// errs() << "replacing\n";
		// 	// errs() << *user << "\n";
		// 	// errs() << *u << "\n";
		// 	// errs() << *v << "\n";
		// 	map<Value *, Value *> reps;
		// 	reps[oldu] = cast;
		// 	// reps[dyn_cast<Value>(v->getOperand(0))] = cast;
		// 	replaceUses(user, reps);
		// }

	} 
	// TODO: need to handle true memory dependences more than just syncing?
	else if (dtype == DTRUE) {	//READ after WRITE
		error("check mem dep!!");

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

		//	int userthread = this->getNewInstAssigned(user);
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
	} else {
		// nothing to do
	}
}

// TODO: clean up redundant synchronization
//       especially due to control dependences...


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
