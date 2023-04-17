//3rd step: thread partition

#include "DSWP.h"

using namespace llvm;
using namespace std;

void DSWP::threadPartition(Loop *L) {
	/* get total latencies for each SCC */
	int totalLatency = 0;
	sccLatency.clear();
	sccLatency.resize(sccNum, 0);

	for (Loop::block_iterator bi = L->getBlocks().begin(),
							  be = L->getBlocks().end();
			bi != be; ++bi) {
		BasicBlock *BB = *bi;
		for (BasicBlock::iterator ii = BB->begin(), ie = BB->end();
				ii != ie; ++ii) {
			Instruction *inst = &(*ii);
			int latency = getLatency(inst);
			sccLatency[sccId[inst]] += latency;
			totalLatency += latency;
		}
	}

	cout<<">>Latencies by SCC:"<<endl;
	for (int i = 0; i < sccNum; i++)
		cout<<"SCC #"<<i<<": "<<sccLatency[i]<<endl;

	int averLatency = totalLatency / MAX_THREAD;

	cout << "latency info:" << totalLatency << " " << averLatency << endl;

	assigned.clear();
	assigned.resize(sccNum, -2);
	// -2: not assigned, and not in queue
	// -1: not assigned, but in queue
	// 0 <= i < MAX_THREAD: already been assigned, not in queue

	int estLatency[MAX_THREAD] = {};

	// keep a queue of candidate SCCs whose predecessors have all been scheduled
	priority_queue<QNode> Q;
	for (unsigned int scc = 0; scc < sccNum; scc++) {
		if (scc_parents[scc].empty()) {
			Q.push(QNode(scc, sccLatency[scc]));
		}
	}

	for (int i = 0; i < MAX_THREAD; i++) {
		while (!Q.empty()) {
			QNode top = Q.top(); Q.pop();
			assigned[top.u] = i;
			estLatency[i] += top.latency;

			// update the queue: add any children whose parents have now all
			// been scheduled
			const vector<int> &deps = scc_dependents[top.u];
			for (vector<int>::const_iterator j = deps.begin(), je = deps.end();
					j != je; ++j) {
				int v = *j;
				if (assigned[v] == -2) {
					bool can_sched = true;
					const vector<int> &pars = scc_parents[v];
					for (vector<int>::const_iterator k = pars.begin(),
											         ke = pars.end();
							k != ke; ++k) {
						if (assigned[*k] < 0) {
							can_sched = false;
							break;
						}
					}
					if (can_sched) {
						Q.push(QNode(v, sccLatency[v]));
					}
				}
			}

			// check load balance
			if (estLatency[i] >= averLatency && i != MAX_THREAD - 1) {
				// we've filled up this thread, so move on.
				// don't do this for the last one, so everything gets scheduled.
				break;
			}

			// TODO: think this breaks ties arbitrarily (maybe in insertion
			// order?), while the original paper broke ties by choosing the one
			// that results in fewest outgoing dependencies.
		}
	}

	// fill the "part" arrays with the assignments of SCCs
	for (int i = 0; i < MAX_THREAD; i++)
		part[i].clear();

	for (int i = 0; i < sccNum; i++) {
		if (assigned[i] < 0 || assigned[i] >= MAX_THREAD) {
			error("scc " + itoa(i) + " assigned to " + itoa(assigned[i]));
		} else {
			part[assigned[i]].push_back(i);
		}
	}

	// debug: print number of SCCs assigned to each thread
	for (int i = 0; i < MAX_THREAD; i++) {
		cout << "part " << i << " size: " << part[i].size() << endl;
	}
}

int DSWP::getLatency(Instruction *I) {	// Instruction Latency for Core 2, 65nm
	int opcode = I->getOpcode();
	int cost;
	switch (opcode) {
		// Terminator instructions
		case Instruction::Ret:    cost=1; break;
		case Instruction::Br:     cost=0; break;
		case Instruction::Switch: cost=0; break;

		// Standard binary operators
		case Instruction::Add:  cost=1; break;
		case Instruction::FAdd: cost=4; break;
		case Instruction::Sub:  cost=1; break;
		case Instruction::FSub: cost=4; break;
		case Instruction::Mul:  cost=3; break;
		case Instruction::FMul: cost=4; break;
		case Instruction::UDiv: cost=17; break;
		case Instruction::SDiv: cost=17; break;
		case Instruction::FDiv: cost=24; break;
		case Instruction::URem: cost=17; break;
		case Instruction::SRem: cost=17; break;
		case Instruction::FRem: cost=24; break;

		// logical operators (integer operands)
		case Instruction::Shl:  cost=7; break;
		case Instruction::LShr: cost=7; break;
		case Instruction::AShr: cost=7; break;
		case Instruction::And:  cost=1; break;
		case Instruction::Or:   cost=1; break;
		case Instruction::Xor:  cost=1; break;

		// Vector ops
		case Instruction::ExtractElement: cost=0; break; // TODO
		case Instruction::InsertElement:  cost=0; break; // TODO
		case Instruction::ShuffleVector:  cost=0; break; // TODO

		// Aggregate ops
		case Instruction::ExtractValue: cost=0; break; // TODO
		case Instruction::InsertValue:  cost=0; break; // TODO

		// Memory ops
		case Instruction::Alloca:        cost=2; break;
		case Instruction::Load:          cost=2; break;
		case Instruction::Store:         cost=2; break;
		case Instruction::Fence:         cost=0; break; // TODO
		case Instruction::AtomicCmpXchg: cost=0; break; // TODO
		case Instruction::AtomicRMW:     cost=0; break; // TODO
		case Instruction::GetElementPtr: cost=1; break;

		// Cast operators
		case Instruction::Trunc:    cost=1; break;
		case Instruction::ZExt:     cost=1; break;
		case Instruction::SExt:     cost=1; break;
		case Instruction::FPTrunc:  cost=4; break;
		case Instruction::FPExt:    cost=4; break;
		case Instruction::FPToUI:   cost=4; break;
		case Instruction::FPToSI:   cost=4; break;
		case Instruction::UIToFP:   cost=4; break;
		case Instruction::SIToFP:   cost=4; break;
		case Instruction::PtrToInt: cost=2; break;
		case Instruction::IntToPtr: cost=2; break;
		case Instruction::BitCast:  cost=1; break;

		// Other
		case Instruction::ICmp:       cost=1; break;
		case Instruction::FCmp:       cost=1; break;
		case Instruction::PHI:        cost=1; break;
		case Instruction::Select:     cost=0; break; // TODO
		case Instruction::Call:       cost=50; break; // really variable...
		case Instruction::VAArg:      cost=0; break; // TODO
		case Instruction::LandingPad: cost=0; break; // TODO

		default: cost=1;
	}

	return cost;
}


int DSWP::getInstAssigned(Value *inst) {
	return assigned[sccId[dyn_cast<Instruction>(inst)]];
}


int DSWP::getNewInstAssigned(Value *inst) {
	if (isa<Instruction>(inst)) {
		error("cannot be term");
	}
	return getInstAssigned(newToOld[inst]);
}
