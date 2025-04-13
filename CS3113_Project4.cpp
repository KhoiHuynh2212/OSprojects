#include <iostream>
#include <queue>
#include <vector>
#include <unordered_map>
#include <list>
using namespace std;


int globalClock = 0;           // Tracks elapsed time in CPU cycles
int CPUAllocated = 0;          // Maximum CPU cycles before preemption (time quantum)
int contextSwitchTime = 0;     // Overhead cycles for context switching
// Maps to track process timing statistics
unordered_map<int, int> processStartTimes;   // When each process first entered RUNNING state
unordered_map<int, int> processEndTimes;     // When each process terminated


struct Instruction {
    int type{};                // 1=Compute, 2=Print(I/O), 3=Store, 4=Load
    vector<int> parameters;    // Parameters vary by instruction type
};


struct PCB {
    int processID{};           // Unique identifier for the process
    int state{};               // Current process state
    int instructionBase{};     // Logical base address of instructions
    int dataBase{};            // Logical base address of data/parameters
    int memoryLimit{};         // Total memory allocated to the process
    int maxMemoryNeeded{};     // Maximum memory required by the process
    int mainMemoryBase{};      // Physical base address of segment table
    int programCounter{};      // Index of next instruction (logical)
    int cpuCyclesUsed{};       // Total CPU time consumed
    int registerValue{};       // Simulated register value
    int numOfInstruction{};    // Total number of instructions
    vector<Instruction> instructions; // Process instructions
};

/**
 * Process states enumeration
 */
enum processState {
    NEW, READY, RUNNING, TERMINATED, IO_WAITING
};

/**
 * Tracks processes waiting for I/O completion
 */
struct IOProcess {
    int procAddr;      // Physical address of segment table in memory
    int waitStartTime; // Global clock value when process began waiting
    int waitCycles;    // Required wait time in cycles
};

/**
 * Represents a block of memory in the memory management system
 */
struct MemoryBlock {
    int processID;      // Process using this block (-1 if free)
    int startAddress;   // Physical starting address of the block
    int size;           // Size of the block in memory units
};


inline bool areAdjacent(const MemoryBlock& b1, const MemoryBlock& b2) {
    return b1.startAddress + b1.size == b2.startAddress;
}


bool coalesceMemory(list<MemoryBlock>& memoryList) {
    bool coalesced = false;
    auto it = memoryList.begin();

    while (it != memoryList.end()) {
        auto nextIt = next(it);
        if (nextIt == memoryList.end()) break;

        // Check if both blocks are free and adjacent
        if (it->processID == -1 && nextIt->processID == -1 && areAdjacent(*it, *nextIt)) {
            // Merge blocks
            it->size += nextIt->size;
            memoryList.erase(nextIt);
            coalesced = true;
            // Try to merge with next block without advancing
        } else {
            ++it;
        }
    }

    return coalesced;
}


int translateAddress(int logicalAddress, int segmentTableAddress, vector<int>& mainMemory) {
    // Get segment table size and calculate number of segments
    int segmentTableSize = mainMemory[segmentTableAddress];
    int numSegments = segmentTableSize / 2;
    int processID = mainMemory[segmentTableAddress + segmentTableSize + 1]; // Get PID for output

    // Track remaining offset
    int remaining = logicalAddress;

    // Iterate through segments to find the right one
    for (int i = 0; i < numSegments; i++) {
        int startAddr = mainMemory[segmentTableAddress + 1 + 2*i];
        int segSize = mainMemory[segmentTableAddress + 1 + 2*i + 1];

        if (remaining < segSize) {
            // Found the segment
            int physicalAddress = startAddr + remaining;

            // No debug output here - will output only for load/store in executeCPU
            return physicalAddress;
        }

        // Move to next segment
        remaining -= segSize;
    }

    // Address out of bounds
    cout << "Memory violation: address " << logicalAddress << " out of bounds for Process "
         << processID << endl;
    return -1;
}

int calculateParamOffset(const vector<int>& mainMemory, int segmentTableAddress,
                         int instrBase, int currentInstructionIndex) {
    int offset = 0;
    vector<int> mutableMemory = mainMemory; // Create non-const copy for translateAddress

    // Add parameter counts for all preceding instructions
    for (int i = 0; i < currentInstructionIndex; i++) {
        // Get instruction opcode
        int logicalAddr = instrBase + i;
        int physicalAddr = translateAddress(logicalAddr, segmentTableAddress, mutableMemory);

        if (physicalAddr == -1) return -1; // Invalid address

        int opcode = mainMemory[physicalAddr];

        // Add parameter counts based on instruction type
        switch (opcode) {
            case 1: // Compute: 2 parameters
            case 3: // Store: 2 parameters
                offset += 2;
                break;
            case 2: // Print: 1 parameter
            case 4: // Load: 1 parameter
                offset += 1;
                break;
            default:
                return -1; // Invalid opcode
        }
    }

    return offset;
}




bool allocateSegments(list<MemoryBlock>& memoryList, PCB& process, int segmentTableAddress,
					  vector<pair<int,int>>& segments) {
	// Define constants for memory overhead components
	const int PCB_SIZE = 10;
	const int SegmentTableSize = 13;

	// Total memory needed includes user memory and PCB (segment table already allocated)
	// We don't add SEGMENT_TABLE_SIZE here because it's allocated separately
	int totalNeeded = process.maxMemoryNeeded + PCB_SIZE + SegmentTableSize;
	int remaining = totalNeeded;
	vector<list<MemoryBlock>::iterator> allocatedBlocks;

	// Try to find up to 6 segments to satisfy memory needs
	for (auto it = memoryList.begin(); it != memoryList.end() && segments.size() < 6; ++it) {
		if (it->processID == -1 && it->size > 0) {  // Free block
			// How much we'll take from this block
			int toAllocate = min(remaining, it->size);

			// If this is the first block, it will contain the segment table
			if (segments.empty()) {
				segmentTableAddress = it->startAddress;
			}

			// Record this segment (store physical address and size)
			segments.push_back({it->startAddress, toAllocate});
			remaining -= toAllocate;

			// Split block if needed
			if (toAllocate < it->size) {
				MemoryBlock newBlock;
				newBlock.processID = -1;  // Free
				newBlock.startAddress = it->startAddress + toAllocate;
				newBlock.size = it->size - toAllocate;

				// Resize current block
				it->size = toAllocate;

				// Insert new block
				memoryList.insert(next(it), newBlock);
			}

			// Mark as allocated
			it->processID = process.processID;

			// Check if we've allocated enough
			if (remaining <= 0) {
				break;
			}
		}
	}

	if (remaining > 0) {
		// Not enough memory, release allocated blocks
		for (auto& segment : segments) {
			for (auto it = memoryList.begin(); it != memoryList.end(); ++it) {
				if (it->startAddress == segment.first && it->processID == process.processID) {
					it->processID = -1;  // Mark as free
					break;
				}
			}
		}
		segments.clear();
		return false;
	}
	// process.mainMemoryBase = segmentTableAddress;
	return true;
}


// Replace your existing setupPCB and initializeSegmentTable functions with this combined function:
void setupSegmentTableAndPCB(PCB& process, vector<int>& mainMemory, int segmentTableAddress,
                            const vector<pair<int,int>>& segments) {
    // First, set up segment table
    int segmentTableSize = segments.size() * 2;
    mainMemory[segmentTableAddress] = segmentTableSize;

    // Write segment entries
	for (int i = 0; i < segments.size(); i++) {
		if (i == 0) {
			// For the first segment (segment 0), set the start address to 0
			// (relative to the segment table address)
			mainMemory[segmentTableAddress + 1 + 2*i] = segmentTableAddress;
		} else {
			// For other segments, use the actual physical address
			mainMemory[segmentTableAddress + 1 + 2*i] = segments[i].first;
		}
		mainMemory[segmentTableAddress + 1 + 2*i + 1] = segments[i].second;  // Size
	}

	// Calculate PCB offsets
	int pcbOffset = segmentTableSize + 1;  // PCB follows segment table
	int instrBase = pcbOffset + 10;        // Instructions follow PCB
	int dataBase = instrBase + process.numOfInstruction; // Data follows instructions

    // Store PCB fields after segment table
    mainMemory[segmentTableAddress + pcbOffset + 0] = process.processID;
    mainMemory[segmentTableAddress + pcbOffset + 1] = READY;  // state
    mainMemory[segmentTableAddress + pcbOffset + 2] = 0;      // programCounter
    mainMemory[segmentTableAddress + pcbOffset + 3] = instrBase;
    mainMemory[segmentTableAddress + pcbOffset + 4] = dataBase;
    mainMemory[segmentTableAddress + pcbOffset + 5] = process.memoryLimit;
    mainMemory[segmentTableAddress + pcbOffset + 6] = 0;      // cpuCyclesUsed
    mainMemory[segmentTableAddress + pcbOffset + 7] = 0;      // registerValue
    mainMemory[segmentTableAddress + pcbOffset + 8] = process.maxMemoryNeeded;
    mainMemory[segmentTableAddress + pcbOffset + 9] = process.mainMemoryBase;  // mainMemoryBase

    // Update process structure
	process.instructionBase = instrBase;
	process.dataBase = dataBase;
	process.mainMemoryBase = segmentTableAddress;


	// Use address translation to write instructions and data
	for (int i = 0; i < process.numOfInstruction; i++) {
		int logicalAddr = instrBase + i;
		int physicalAddr = translateAddress(logicalAddr, segmentTableAddress, mainMemory);
		if (physicalAddr != -1) {
			mainMemory[physicalAddr] = process.instructions[i].type;
		}
	}

	// Write parameters
	int dataOffset = 0;
	for (auto& instr : process.instructions) {
		for (int param : instr.parameters) {
			int logicalAddr = dataBase + dataOffset++;
			int physicalAddr = translateAddress(logicalAddr, segmentTableAddress, mainMemory);
			if (physicalAddr != -1) {
				mainMemory[physicalAddr] = param;
			}
		}
	}
}


void loadProcessContent(PCB& process, vector<int>& mainMemory, int segmentTableAddress) {
    // Load instructions directly after PCB fields
    for (int i = 0; i < process.instructions.size(); i++) {
        int logicalAddr = process.instructionBase + i;
        int physicalAddr = translateAddress(logicalAddr, segmentTableAddress, mainMemory);

        if (physicalAddr != -1) {
            mainMemory[physicalAddr] = process.instructions[i].type;
        }
    }

    // Load data (instruction parameters)
    int dataOffset = 0;
    for (auto& instr : process.instructions) {
        for (int param : instr.parameters) {
            int logicalAddr = process.dataBase + dataOffset++;
            int physicalAddr = translateAddress(logicalAddr, segmentTableAddress, mainMemory);

            if (physicalAddr != -1) {
                mainMemory[physicalAddr] = param;
            }
        }
    }
}


void releaseProcessMemory(list<MemoryBlock>& memoryList, int processID, vector<int>& mainMemory) {
    for (auto it = memoryList.begin(); it != memoryList.end(); ++it) {
        if (it->processID == processID) {
            // Clear memory
            for (int i = 0; i < it->size; i++) {
                mainMemory[it->startAddress + i] = -1;
            }

            // Mark block as free
            it->processID = -1;
        }
    }

    cout << "Process " << processID << " terminated and freed memory blocks." << endl;

    // Attempt to coalesce free blocks
    coalesceMemory(memoryList);
}


void tryLoadJobsToMemory(queue<PCB>& newJobQueue, queue<int>& readyQueue,
                         vector<int>& mainMemory, list<MemoryBlock>& memoryList) {
    bool jobLoaded;

    do {
        jobLoaded = false;

        if (newJobQueue.empty()) break;

        PCB currentProcess = newJobQueue.front();

        // Try to allocate segment table
        int segmentTableAddress;

        // Try to allocate segments
        vector<pair<int,int>> segments;
        if (!allocateSegments(memoryList, currentProcess, segmentTableAddress, segments)) {
            // Free segment table
            for (auto it = memoryList.begin(); it != memoryList.end(); ++it) {
                if (it->startAddress == segmentTableAddress && it->processID == currentProcess.processID) {
                    it->processID = -1;
                    break;
                }
            }

            // Before giving up, try memory coalescing
            cout << "Insufficient memory for Process " << currentProcess.processID
                 << ". Attempting memory coalescing." << endl;

            if (coalesceMemory(memoryList)) {
                // Try again after coalescing
                if (allocateSegments(memoryList, currentProcess, segmentTableAddress, segments)) {
                    // Coalescing helped, continue with loading
                } else {
                    // Still not enough memory
                    cout << "Process " << currentProcess.processID
                         << " waiting in NewJobQueue due to insufficient memory." << endl;
                    break;
                }
            } else {
                // No coalescing possible
                cout << "Process " << currentProcess.processID
                     << " waiting in NewJobQueue due to insufficient memory." << endl;
                break;
            }
        }

        // Set up segment table and PCB in one step
        setupSegmentTableAndPCB(currentProcess, mainMemory, segmentTableAddress, segments);

        // Load process content
        loadProcessContent(currentProcess, mainMemory, segmentTableAddress);

        // Success - move to ready queue
        newJobQueue.pop();
        readyQueue.push(segmentTableAddress);

        cout << "Process " << currentProcess.processID
             << " loaded with segment table stored at physical address "
             << segmentTableAddress << endl;

        jobLoaded = true;

    } while (jobLoaded);
}

void checkIOWaitingQueue(queue<IOProcess>& ioWaitingQueue, queue<int>& readyQueue, vector<int>& mainMemory) {
    const int queueSize = ioWaitingQueue.size();

    for (int i = 0; i < queueSize; i++) {
        IOProcess ioProc = ioWaitingQueue.front();
        ioWaitingQueue.pop();

        // Get segment table size and PCB offset
        int segmentTableSize = mainMemory[ioProc.procAddr];
        int pcbOffset = segmentTableSize + 1;
        int processID = mainMemory[ioProc.procAddr + pcbOffset];

        // Check if I/O wait time has elapsed
        if (globalClock - ioProc.waitStartTime >= ioProc.waitCycles) {
            // Add I/O time to the process's CPU cycles record
            int cpuCycles = mainMemory[ioProc.procAddr + pcbOffset + 6];
            cpuCycles += ioProc.waitCycles;
            mainMemory[ioProc.procAddr + pcbOffset + 6] = cpuCycles;

            // Change state from IO_WAITING to READY
            mainMemory[ioProc.procAddr + pcbOffset + 1] = READY;

            cout << "print\n";
            cout << "Process " << processID << " completed I/O and is moved to the ReadyQueue." << endl;

            // Add to ready queue
            readyQueue.push(ioProc.procAddr);
        } else {
            // Process still needs to wait
            ioWaitingQueue.push(ioProc);
        }
    }
}


void executeCPU(int segmentTableAddress, vector<int>& mainMemory, queue<int>& readyQueue,
                queue<IOProcess>& ioWaitingQueue, list<MemoryBlock>& memoryList,
                queue<PCB>& newJobQueue) {
    // Get segment table size and PCB offset
    int segmentTableSize = mainMemory[segmentTableAddress];
    int pcbOffset = segmentTableSize + 1;

    // Reconstruct PCB from memory - note fields now follow segment table
    PCB process;
    process.processID = mainMemory[segmentTableAddress + pcbOffset];
	process.mainMemoryBase = mainMemory[segmentTableAddress + pcbOffset + 9];
    process.cpuCyclesUsed = mainMemory[segmentTableAddress + pcbOffset + 6];
    process.registerValue = mainMemory[segmentTableAddress + pcbOffset + 7];
    process.instructionBase = mainMemory[segmentTableAddress + pcbOffset + 3];  // Logical base
    process.dataBase = mainMemory[segmentTableAddress + pcbOffset + 4];         // Logical base
    process.memoryLimit = mainMemory[segmentTableAddress + pcbOffset + 5];
    process.maxMemoryNeeded = mainMemory[segmentTableAddress + pcbOffset + 8];

    // Set state to RUNNING
    process.state = RUNNING;
    mainMemory[segmentTableAddress + pcbOffset + 1] = RUNNING;

    // Get program counter (logical)
    int programCounter = mainMemory[segmentTableAddress + pcbOffset + 2];

    // Calculate number of instructions (logical)
    int numInstructions = process.dataBase - process.instructionBase;
    int cyclesUsed = 0;  // Cycles used in this CPU burst

    // Apply context switch overhead
    globalClock += contextSwitchTime;

    cout << "Process " << process.processID << " has moved to Running." << endl;

    // Record start time if first execution
    if (processStartTimes.find(process.processID) == processStartTimes.end()) {
        processStartTimes[process.processID] = globalClock;
    }

    // Execute instructions until time quantum expires or process terminates/blocks
    while (programCounter < numInstructions && cyclesUsed < CPUAllocated) {
        // Translate logical instruction address to physical
        int logicalInstrAddr = process.instructionBase + programCounter;
        int physicalInstrAddr = translateAddress(logicalInstrAddr, segmentTableAddress, mainMemory);

        if (physicalInstrAddr == -1) {
            // Memory violation - terminate execution
            cout << "Process " << process.processID << " terminated due to memory violation." << endl;
            mainMemory[segmentTableAddress + pcbOffset + 1] = TERMINATED;

            // Record end time and free memory
            processEndTimes[process.processID] = globalClock;
            releaseProcessMemory(memoryList, process.processID, mainMemory);

            // Try to load more jobs
            tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);
            return;
        }

        // Get the instruction
        int opcode = mainMemory[physicalInstrAddr];

        // Calculate parameter offset and translate parameter address
        int paramOffset = calculateParamOffset(mainMemory, segmentTableAddress,
                                               process.instructionBase, programCounter);

        if (paramOffset == -1) {
            // Parameter calculation error
            cout << "Process " << process.processID << " terminated due to parameter calculation error." << endl;
            mainMemory[segmentTableAddress + pcbOffset + 1] = TERMINATED;

            // Record end time and free memory
            processEndTimes[process.processID] = globalClock;
            releaseProcessMemory(memoryList, process.processID, mainMemory);

            // Try to load more jobs
            tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);
            return;
        }

        int logicalParamAddr = process.dataBase + paramOffset;
        int physicalParamAddr = translateAddress(logicalParamAddr, segmentTableAddress, mainMemory);

        if (physicalParamAddr == -1) {
            // Memory violation
            cout << "Process " << process.processID << " terminated due to parameter memory violation." << endl;
            mainMemory[segmentTableAddress + pcbOffset + 1] = TERMINATED;

            // Record end time and free memory
            processEndTimes[process.processID] = globalClock;
            releaseProcessMemory(memoryList, process.processID, mainMemory);

            // Try to load more jobs
            tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);
            return;
        }

        // Execute instruction based on opcode
        switch (opcode) {
            case 1: {  // Compute
                int iterations = mainMemory[physicalParamAddr];  // Not used in simulation
                int compCycles = mainMemory[physicalParamAddr + 1];

                // Update cycle counters
                cyclesUsed += compCycles;
                globalClock += compCycles;
                process.cpuCyclesUsed += compCycles;

                // Update cycles in PCB
                mainMemory[segmentTableAddress + pcbOffset + 6] = process.cpuCyclesUsed;

                cout << "compute\n";
                break;
            }
            case 2: {  // Print/I/O
                int ioCycles = mainMemory[physicalParamAddr];

                // Set state to IO_WAITING
                mainMemory[segmentTableAddress + pcbOffset + 1] = IO_WAITING;

                // Advance program counter before entering I/O wait
                programCounter++;
                mainMemory[segmentTableAddress + pcbOffset + 2] = programCounter;

                // Create I/O process record and add to queue
                IOProcess ioProc{segmentTableAddress, globalClock, ioCycles};

                cout << "Process " << process.processID << " issued an IOInterrupt and moved to the IOWaitingQueue."
                     << endl;
                ioWaitingQueue.push(ioProc);
                return;  // Exit CPU execution - process is now waiting
            }
            case 3: {  // Store
				int value = mainMemory[physicalParamAddr];
				int logicalStoreAddr = mainMemory[physicalParamAddr + 1];

				// Translate store address
				int physicalStoreAddr = translateAddress(logicalStoreAddr, segmentTableAddress, mainMemory);



				if (physicalStoreAddr != -1) {
					// Update memory and register
					mainMemory[physicalStoreAddr] = value;
					process.registerValue = value;
					mainMemory[segmentTableAddress + pcbOffset + 7] = value;  // Update register in PCB

					cout << "stored\n";
				} else {
					cout << "store error!\n";
				}


				cout << "Logical address " << logicalStoreAddr << " translated to physical address "
					 << physicalStoreAddr << " for Process " << process.processID << endl;

				// Update cycle counters (1 cycle for store)
				cyclesUsed += 1;
				globalClock += 1;
				process.cpuCyclesUsed += 1;
				mainMemory[segmentTableAddress + pcbOffset + 6] = process.cpuCyclesUsed;
				break;

            }
            case 4: {  // Load
				int logicalLoadAddr = mainMemory[physicalParamAddr];

				// Translate load address
				int physicalLoadAddr = translateAddress(logicalLoadAddr, segmentTableAddress, mainMemory);

				if (physicalLoadAddr != -1) {
					// Load value into register
					process.registerValue = mainMemory[physicalLoadAddr];
					mainMemory[segmentTableAddress + pcbOffset + 7] = process.registerValue;  // Update in PCB

					cout << "loaded\n";
				} else {
					cout << "load error!\n";
				}
				cout << "Logical address " << logicalLoadAddr << " translated to physical address "
					 << physicalLoadAddr << " for Process " << process.processID << endl;
				// Update cycle counters (1 cycle for load)
				cyclesUsed += 1;
				globalClock += 1;
				process.cpuCyclesUsed += 1;
				mainMemory[segmentTableAddress + pcbOffset + 6] = process.cpuCyclesUsed;
				break;
            }
            default:
                cout << "Process " << process.processID << " encountered an invalid instruction." << endl;
                return;
        }

        // Advance program counter
        programCounter++;
    }

    // Check if process reached its time quantum (timed out)
    if (programCounter < numInstructions && cyclesUsed >= CPUAllocated) {
        cout << "Process " << process.processID << " has a TimeOUT interrupt and is moved to the ReadyQueue." << endl;
        mainMemory[segmentTableAddress + pcbOffset + 2] = programCounter;  // Save current PC
        mainMemory[segmentTableAddress + pcbOffset + 1] = READY;           // Change state to READY
        readyQueue.push(segmentTableAddress);                          // Put back in ready queue

        // Check I/O waiting queue
        checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);
        return;
    }

    // Check if process completed all instructions (terminated)
    if (programCounter >= numInstructions) {
        // Set process state to TERMINATED
        mainMemory[segmentTableAddress + pcbOffset + 1] = TERMINATED;
        mainMemory[segmentTableAddress + pcbOffset + 2] = programCounter;

        // Record termination time and calculate execution time
        processEndTimes[process.processID] = globalClock;
        int execTime = processEndTimes[process.processID] - processStartTimes[process.processID];

        // Print PCB information - using logical addresses for PC, InstructionBase, and DataBase
        cout << "Process ID: " << process.processID << "\n";
        cout << "State: TERMINATED\n";
        cout << "Program Counter: " << process.instructionBase - 1  << "\n";  // Logical address
        cout << "Instruction Base: " << process.instructionBase << "\n";  // Logical address
        cout << "Data Base: " << process.dataBase << "\n";  // Logical address
        cout << "Memory Limit: " << process.memoryLimit << "\n";
        cout << "CPU Cycles Used: " << process.cpuCyclesUsed << "\n";
        cout << "Register Value: " << process.registerValue << "\n";
        cout << "Max Memory Needed: " << process.maxMemoryNeeded << "\n";
        cout << "Main Memory Base: " << process.mainMemoryBase << "\n";
        cout << "Total CPU Cycles Consumed: " << execTime << "\n";

        // Print termination summary
        cout << "Process " << process.processID << " terminated. Entered running state at: "
             << processStartTimes[process.processID] << ". Terminated at: "
             << processEndTimes[process.processID] << ". Total Execution Time: "
             << execTime << ".\n";

        // Release memory segments
        releaseProcessMemory(memoryList, process.processID, mainMemory);

        // Try to load more jobs
        tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);

        // Check I/O waiting queue
        checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);
    } else {
        // Process still has instructions but reached time quantum
        mainMemory[segmentTableAddress + pcbOffset + 1] = READY;  // Set state to READY
        mainMemory[segmentTableAddress + pcbOffset + 2] = programCounter;  // Save program counter
        readyQueue.push(segmentTableAddress);         // Put back in ready queue

        // Check if any I/O waiting processes are ready
        checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);
    }


}

int main() {
    int maxMemory, numProcesses;
    queue<PCB> newJobQueue;         // Processes waiting to be loaded
    queue<int> readyQueue;          // Processes ready to execute (stores segment table address)
    queue<IOProcess> ioWaitingQueue;  // Processes waiting for I/O completion

    // Read simulation parameters
    cin >> maxMemory >> CPUAllocated >> contextSwitchTime;
    cin >> numProcesses;

    // Initialize main memory array and memory block list
    vector<int> mainMemory(maxMemory, -1);  // Initialize all memory to -1 (unused)
    list<MemoryBlock> memoryList;           // Linked list to track memory blocks

    // Start with a single free block covering all memory
    MemoryBlock initialBlock{-1, 0, maxMemory};  // processID -1 means free
    memoryList.push_back(initialBlock);

    // Read process definitions
    for (int i = 0; i < numProcesses; i++) {
        PCB process;
        int pid, memoryNeeded, numInstructions;

        cin >> pid >> memoryNeeded >> numInstructions;

        // Initialize PCB fields
        process.processID = pid;
        process.state = NEW;
        process.programCounter = 0;
        process.memoryLimit = memoryNeeded;
        process.maxMemoryNeeded = memoryNeeded;
        process.numOfInstruction = numInstructions;
        process.cpuCyclesUsed = 0;
        process.registerValue = 0;
        process.mainMemoryBase = -1;  // Will be set during loading

        // Read instructions for this process
        for (int j = 0; j < numInstructions; j++) {
            Instruction inst;
            cin >> inst.type;

            // Read parameters based on instruction type
            switch(inst.type) {
                case 1: {  // Compute: iterations, cpuCycles
                    int iterations, cycles;
                    cin >> iterations >> cycles;
                    inst.parameters = {iterations, cycles};
                    break;
                }
                case 2: {  // Print: waitCycles
                    int cycles;
                    cin >> cycles;
                    inst.parameters = {cycles};
                    break;
                }
                case 3: {  // Store: value, address
                    int value, addr;
                    cin >> value >> addr;
                    inst.parameters = {value, addr};
                    break;
                }
                case 4: {  // Load: address
                    int addr;
                    cin >> addr;
                    inst.parameters = {addr};
                    break;
                }
                default:
                    cout << "Invalid instruction type: " << inst.type << endl;
            }

            process.instructions.push_back(inst);
        }

        // Add process to new job queue
        newJobQueue.push(process);
    }

    // Try to load initial jobs into memory with segmented allocation
    tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);

    // Print initial memory state for debugging
    for (int i = 0; i < mainMemory.size(); i++) {
        cout << i << ": " << mainMemory[i] << endl;
    }

    // Main simulation loop
    while (!readyQueue.empty() || !ioWaitingQueue.empty() || !newJobQueue.empty()) {
        // Step 1: Check if any I/O waiting processes are ready
        checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);

        // Step 2: Try to load more jobs if ready queue is empty
        if (readyQueue.empty() && !newJobQueue.empty()) {
            tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);
        }

        // Step 3: Execute process from ready queue if available
        if (!readyQueue.empty()) {
            int segmentTableAddress = readyQueue.front();
            readyQueue.pop();

            // Execute process - note we pass segment table address now
            executeCPU(segmentTableAddress, mainMemory, readyQueue,
                       ioWaitingQueue, memoryList, newJobQueue);
        }
            // Step 4: If only I/O processes exist, advance clock
        else if (!ioWaitingQueue.empty()) {
            globalClock += contextSwitchTime;
            checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);
        }
            // Step 5: If only waiting jobs exist, try final coalescing
        else if (!newJobQueue.empty()) {
            // Last attempt to coalesce memory and load jobs
            if (coalesceMemory(memoryList)) {
                tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);

                // If still can't load any jobs, we're stuck
                if (readyQueue.empty()) {
                    cout << "Cannot make further progress - insufficient memory for remaining jobs." << endl;
                    break;
                }
            }
            else {
                cout << "Cannot make further progress - memory fragmentation cannot be resolved." << endl;
                break;
            }
        }
    }

    // Apply final context switch
    globalClock += contextSwitchTime;

    // Print final statistics
    cout << "Total CPU time used: " << globalClock << "." << endl;

    return 0;
}