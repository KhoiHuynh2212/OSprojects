#include <iostream>
#include <queue>
#include <vector>
#include <unordered_map>
#include <list>
using namespace std;

//======================================================
// GLOBAL VARIABLES
//======================================================
int globalClock = 0;           // Tracks elapsed time in CPU cycles
int CPUAllocated = 0;          // Maximum CPU cycles before preemption (time quantum)
int contextSwitchTime = 0;     // Overhead cycles for context switching

// Maps to track process timing statistics
unordered_map<int, int> processStartTimes;   // When each process first entered RUNNING state
unordered_map<int, int> processEndTimes;     // When each process terminated

//======================================================
// DATA STRUCTURES
//======================================================

/**
 * Represents a single CPU instruction with its type and parameters
 */
struct Instruction {
    int type{};                // 1=Compute, 2=Print(I/O), 3=Store, 4=Load
    vector<int> parameters;    // Parameters vary by instruction type
};

/**
 * Process Control Block - Contains essential process information
 * In Project 4, PCB structure has changed:
 * - It starts with a segment table (size + entries)
 * - PCB fields follow the segment table
 */
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

//======================================================
// HELPER FUNCTIONS
//======================================================

/**
 * Checks if two memory blocks are physically adjacent
 */
inline bool areAdjacent(const MemoryBlock& b1, const MemoryBlock& b2) {
    return b1.startAddress + b1.size == b2.startAddress;
}

/**
 * Merges adjacent free memory blocks to reduce fragmentation
 *
 * @param memoryList The linked list of memory blocks
 * @return True if any blocks were merged
 */
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

/**
 * Translates a logical address to a physical address using the segment table
 *
 * @param logicalAddress The logical address to translate
 * @param segmentTableAddress Physical address of the segment table
 * @param mainMemory The system's main memory
 * @return Physical address or -1 if address is invalid
 */
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

            // Debug output for address translation
            cout << "Logical address " << logicalAddress << " translated to physical address "
                 << physicalAddress << " for Process " << processID << endl;

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

/**
 * Calculates the parameter offset for an instruction
 * This is more complex with segmented memory since we need to translate addresses
 *
 * @param mainMemory The system's main memory
 * @param segmentTableAddress Address of the segment table
 * @param instrBase Logical base address of instructions
 * @param currentInstructionIndex Current instruction index
 * @return Parameter offset or -1 if an error occurs
 */
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

//======================================================
// MEMORY MANAGEMENT FUNCTIONS
//======================================================

/**
 * Allocates a contiguous block for the segment table
 *
 * @param memoryList The linked list of memory blocks
 * @param process The process needing allocation
 * @param tableAddress Will be set to the address of allocated table
 * @return True if allocation succeeded
 */
bool allocateSegmentTable(list<MemoryBlock>& memoryList, PCB& process, int& tableAddress) {
    // Need at least 13 contiguous integers for segment table (supports up to 6 segments)
    const int MIN_SEGMENT_TABLE_SIZE = 13;

    // Find a free block large enough for segment table
    for (auto it = memoryList.begin(); it != memoryList.end(); ++it) {
        // Check if block is free and large enough
        if (it->processID == -1 && it->size >= MIN_SEGMENT_TABLE_SIZE) {
            tableAddress = it->startAddress;

            // If block is larger than needed, split it
            if (it->size > MIN_SEGMENT_TABLE_SIZE) {
                MemoryBlock newBlock;
                newBlock.processID = -1;  // Free
                newBlock.startAddress = it->startAddress + MIN_SEGMENT_TABLE_SIZE;
                newBlock.size = it->size - MIN_SEGMENT_TABLE_SIZE;

                // Resize current block
                it->size = MIN_SEGMENT_TABLE_SIZE;

                // Insert new block after current
                memoryList.insert(next(it), newBlock);
            }

            // Mark block as allocated
            it->processID = process.processID;
            return true;
        }
    }

    // Could not find a suitable block
    cout << "Process " << process.processID << " could not be loaded due to insufficient contiguous space for segment table." << endl;
    return false;
}

/**
 * Allocates multiple segments for a process
 *
 * @param memoryList The linked list of memory blocks
 * @param process The process needing allocation
 * @param segmentTableAddress Address of the segment table
 * @param segments Will be filled with allocated (start, size) pairs
 * @return True if allocation succeeded
 */
bool allocateSegments(list<MemoryBlock>& memoryList, PCB& process, int segmentTableAddress,
                      vector<pair<int,int>>& segments) {
    int totalNeeded = process.maxMemoryNeeded;
    int allocated = 0;
    vector<list<MemoryBlock>::iterator> allocatedBlocks;

    // Try to find up to 6 segments to satisfy memory needs
    for (auto it = memoryList.begin(); it != memoryList.end() && segments.size() < 6; ++it) {
        // Skip if block is already allocated or is the segment table
        if (it->processID != -1 || it->startAddress == segmentTableAddress) {
            continue;
        }

        // Calculate how much to take from this block
        int toAllocate = min(totalNeeded - allocated, it->size);

        if (toAllocate > 0) {
            // Add this segment
            segments.push_back({it->startAddress, toAllocate});
            allocatedBlocks.push_back(it);
            allocated += toAllocate;

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
            if (allocated >= totalNeeded) {
                break;
            }
        }
    }

    // Check if we allocated enough memory
    if (allocated < totalNeeded) {
        // Not enough memory, free the blocks we allocated
        for (auto blockIt : allocatedBlocks) {
            blockIt->processID = -1;  // Mark as free
        }
        return false;
    }

    return true;
}

/**
 * Initialize the segment table in memory
 *
 * @param mainMemory The system's main memory
 * @param tableAddress Address of the segment table
 * @param segments List of (start, size) pairs for segments
 */
void initializeSegmentTable(vector<int>& mainMemory, int tableAddress,
                            const vector<pair<int,int>>& segments) {
    // Write segment table size (2 * number of segments)
    int segmentTableSize = segments.size() * 2;
    mainMemory[tableAddress] = segmentTableSize;

    // Write segment entries
    for (int i = 0; i < segments.size(); i++) {
        mainMemory[tableAddress + 1 + 2*i] = segments[i].first;      // Start address
        mainMemory[tableAddress + 1 + 2*i + 1] = segments[i].second;  // Size
    }
}

/**
 * Setup PCB fields in memory
 *
 * @param process The process being loaded
 * @param mainMemory The system's main memory
 * @param segmentTableAddress Address of the segment table
 */
void setupPCB(PCB& process, vector<int>& mainMemory, int segmentTableAddress) {
    int segmentTableSize = mainMemory[segmentTableAddress];
    int pcbStartOffset = segmentTableSize + 1;

    // Write PCB fields after segment table
    mainMemory[segmentTableAddress + pcbStartOffset + 0] = process.processID;
    mainMemory[segmentTableAddress + pcbStartOffset + 1] = READY;  // state
    mainMemory[segmentTableAddress + pcbStartOffset + 2] = 0;      // programCounter

    // Calculate logical bases for instructions and data
    int totalPCBSize = 10;  // All PCB fields excluding segment table
    int instrBase = pcbStartOffset + totalPCBSize;
    int dataBase = instrBase + process.instructions.size();

    mainMemory[segmentTableAddress + pcbStartOffset + 3] = instrBase;
    mainMemory[segmentTableAddress + pcbStartOffset + 4] = dataBase;
    mainMemory[segmentTableAddress + pcbStartOffset + 5] = process.memoryLimit;
    mainMemory[segmentTableAddress + pcbStartOffset + 6] = 0;      // cpuCyclesUsed
    mainMemory[segmentTableAddress + pcbStartOffset + 7] = 0;      // registerValue
    mainMemory[segmentTableAddress + pcbStartOffset + 8] = process.maxMemoryNeeded;
    mainMemory[segmentTableAddress + pcbStartOffset + 9] = segmentTableAddress;  // mainMemoryBase
}

/**
 * Load process content (instructions and data) into allocated segments
 *
 * @param process The process being loaded
 * @param mainMemory The system's main memory
 * @param segmentTableAddress Address of the segment table
 */
void loadProcessContent(PCB& process, vector<int>& mainMemory, int segmentTableAddress) {
    int segmentTableSize = mainMemory[segmentTableAddress];
    int pcbOffset = segmentTableSize + 1;

    // Get logical bases for instructions and data
    int instrBase = mainMemory[segmentTableAddress + pcbOffset + 3];
    int dataBase = mainMemory[segmentTableAddress + pcbOffset + 4];

    // Load instructions
    for (int i = 0; i < process.instructions.size(); i++) {
        int logicalAddr = instrBase + i;
        int physicalAddr = translateAddress(logicalAddr, segmentTableAddress, mainMemory);

        if (physicalAddr != -1) {
            mainMemory[physicalAddr] = process.instructions[i].type;
        }
    }

    // Load data (instruction parameters)
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

/**
 * Release all memory segments of a process
 *
 * @param memoryList The linked list of memory blocks
 * @param processID ID of the process to release
 * @param mainMemory The system's main memory
 */
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

//======================================================
// PROCESS LOADING AND EXECUTION FUNCTIONS
//======================================================

/**
 * Try to load processes from NewJobQueue into memory using segmented allocation
 *
 * @param newJobQueue Queue of processes waiting to be loaded
 * @param readyQueue Queue of processes ready to execute
 * @param mainMemory The system's main memory
 * @param memoryList List of memory blocks (both allocated and free)
 */
void tryLoadJobsToMemory(queue<PCB>& newJobQueue, queue<int>& readyQueue,
                         vector<int>& mainMemory, list<MemoryBlock>& memoryList) {
    bool jobLoaded;

    do {
        jobLoaded = false;

        if (newJobQueue.empty()) break;

        PCB currentProcess = newJobQueue.front();

        // Step 1: Coalesce memory to maximize space
        coalesceMemory(memoryList);

        // Step 2: Try to allocate segment table
        int segmentTableAddress;
        if (!allocateSegmentTable(memoryList, currentProcess, segmentTableAddress)) {
            break;  // Can't allocate segment table
        }

        // Step 3: Try to allocate segments
        vector<pair<int,int>> segments;
        if (!allocateSegments(memoryList, currentProcess, segmentTableAddress, segments)) {
            // Free segment table
            for (auto it = memoryList.begin(); it != memoryList.end(); ++it) {
                if (it->startAddress == segmentTableAddress && it->processID == currentProcess.processID) {
                    it->processID = -1;
                    break;
                }
            }

            cout << "Process " << currentProcess.processID
                 << " waiting in NewJobQueue due to insufficient memory." << endl;
            break;
        }

        // Step 4: Initialize segment table
        initializeSegmentTable(mainMemory, segmentTableAddress, segments);

        // Step 5: Setup PCB
        setupPCB(currentProcess, mainMemory, segmentTableAddress);

        // Step 6: Load process content
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

/**
 * Checks the I/O waiting queue and moves processes that have completed
 * their I/O wait time back to the ready queue
 *
 * @param ioWaitingQueue Queue of processes waiting for I/O
 * @param readyQueue Queue of processes ready to execute
 * @param mainMemory The system's main memory
 */
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

/**
 * Executes a process on the CPU
 * Modified to handle segmented memory and address translation
 *
 * @param segmentTableAddress Address of the process's segment table
 * @param mainMemory The system's main memory
 * @param readyQueue Queue of processes ready to execute
 * @param ioWaitingQueue Queue of processes waiting for I/O
 * @param memoryList List of memory blocks
 * @param newJobQueue Queue of processes waiting to be loaded
 */
void executeCPU(int segmentTableAddress, vector<int>& mainMemory, queue<int>& readyQueue,
                queue<IOProcess>& ioWaitingQueue, list<MemoryBlock>& memoryList,
                queue<PCB>& newJobQueue) {
    // Get segment table size and PCB offset
    int segmentTableSize = mainMemory[segmentTableAddress];
    int pcbOffset = segmentTableSize + 1;

    // Reconstruct PCB from memory - note fields now follow segment table
    PCB process;
    process.processID = mainMemory[segmentTableAddress + pcbOffset];
    process.mainMemoryBase = segmentTableAddress;
    process.cpuCyclesUsed = mainMemory[segmentTableAddress + pcbOffset + 6];
    process.registerValue = mainMemory[segmentTableAddress + pcbOffset + 7];
    process.instructionBase = mainMemory[segmentTableAddress + pcbOffset + 3];  // Logical base
    process.dataBase = mainMemory[segmentTableAddress + pcbOffset + 4];         // Logical base
    process.memoryLimit = mainMemory[segmentTableAddress + pcbOffset + 5];

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
        cout << "Process " << process.processID << " has a TimeOut interrupt and is moved to the ReadyQueue." << endl;
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

        // Print PCB information
        cout << "Process ID: " << process.processID << "\n";
        cout << "State: TERMINATED\n";
        cout << "Program Counter: " << programCounter << "\n";
        cout << "Instruction Base: " << process.instructionBase << "\n";
        cout << "Data Base: " << process.dataBase << "\n";
        cout << "Memory Limit: " << process.memoryLimit << "\n";
        cout << "CPU Cycles Used: " << process.cpuCyclesUsed << "\n";
        cout << "Register Value: " << process.registerValue << "\n";
        cout << "Max Memory Needed: " << process.maxMemoryNeeded << "\n";
        cout << "Main Memory Base: " << process.mainMemoryBase << "\n";
        cout << "Total CPU Cycles Consumed: " << process.cpuCyclesUsed << "\n";

        // Print termination summary
        cout << "Process " << process.processID << " terminated. Entered running state at: "
             << processStartTimes[process.processID] << ". Terminated at: "
             << processEndTimes[process.processID] << ". Total execution time: "
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
            // First try coalescing memory to maximize available space
            coalesceMemory(memoryList);
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