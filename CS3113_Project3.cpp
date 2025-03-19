#include <iostream>
#include <queue>
#include <vector>
#include <unordered_map>
#include <list>
using namespace std;

//======================================================
// GLOBAL VARIABLES
//======================================================
int globalClock = 0;           // Tracks elapsed time in CPU cycles for the entire simulation
int CPUAllocated = 0;          // Maximum CPU cycles a process can use before being preempted (time quantum)
int contextSwitchTime = 0;     // Overhead cycles required when switching between processes

// Maps to track when processes start and finish execution
unordered_map<int, int> processStartTimes;   // Tracks when each process first entered RUNNING state
unordered_map<int, int> processEndTimes;     // Tracks when each process terminated

//======================================================
// DATA STRUCTURES
//======================================================

/**
 * Represents a single CPU instruction with its type and parameters
 *
 * Types:
 * 1 = Compute - simulates CPU computation (params: iterations, CPU cycles)
 * 2 = Print - simulates I/O operation (params: wait cycles)
 * 3 = Store - stores a value in memory (params: value, address)
 * 4 = Load - loads a value from memory (params: address)
 */
struct Instruction {
    int type{};                // Instruction type code
    vector<int> parameters;    // Parameters vary by instruction type
};

/**
 * Process Control Block - Contains all essential information about a process
 * Serves as the main data structure for process management in the OS simulation
 */
struct PCB {
    int processID{};           // Unique identifier for the process
    int state{};               // Current process state (NEW, READY, RUNNING, etc.)
    int instructionBase{};     // Starting address of instructions in main memory
    int dataBase{};            // Starting address of data (parameters) in main memory
    int memoryLimit{};         // Total memory allocated to the process
    int maxMemoryNeeded{};     // Maximum memory required by the process
    int mainMemoryBase{};      // Base address of the process's PCB in main memory
    int programCounter{};      // Index of the next instruction to execute
    int cpuCyclesUsed{};       // Total CPU time consumed by the process
    int registerValue{};       // Simulated register for the process
    int numOfInstruction{};    // Total number of instructions in the process
    vector<Instruction> instructions; // The actual instructions of the process
};

/**
 * Enumeration of possible process states
 * Improves code readability when handling state transitions
 */
enum processState {
    NEW,          // Just created, not yet loaded into memory
    READY,        // Loaded into memory, ready to execute
    RUNNING,      // Currently executing on the CPU
    TERMINATED,   // Execution completed
    IO_WAITING    // Waiting for I/O operation to complete
};

/**
 * Tracks processes waiting for I/O operations to complete
 * Used in the IO_WAITING queue management
 */
struct IOProcess {
    int procAddr;      // Base address of the process's PCB in main memory
    int waitStartTime; // Global clock value when process began waiting
    int waitCycles;    // Number of cycles the I/O operation requires
};

/**
 * Represents a block of memory in the memory management system
 * Used to track both allocated and free memory regions
 */
struct MemoryBlock {
    int processID;      // Process using this block (-1 if free)
    int startAddress;   // Starting address of this memory block
    int size;           // Size of this memory block in units
};

//======================================================
// HELPER FUNCTIONS
//======================================================

/**
 * Checks if two memory blocks are physically adjacent in memory
 * Used for memory coalescing operations
 *
 * @param b1 First memory block
 * @param b2 Second memory block
 * @return True if b2 immediately follows b1 in memory
 */
inline bool areAdjacent(const MemoryBlock& b1, const MemoryBlock& b2) {
    return b1.startAddress + b1.size == b2.startAddress;
}

/**
 * Merges adjacent free memory blocks to reduce fragmentation
 * This is triggered when a process can't fit into available memory
 *
 * @param memoryList The linked list of memory blocks
 * @return True if any blocks were merged, false otherwise
 */
bool coalesceMemory(list<MemoryBlock>& memoryList) {
    bool isCoalesced = false;
    auto it = memoryList.begin();

    while (it != memoryList.end()) {
        auto nextIt = next(it);
        if (nextIt == memoryList.end()) break;

        // Check if both blocks are free and adjacent
        if (it->processID == -1 && nextIt->processID == -1 && areAdjacent(*it, *nextIt)) {
            // Merge the blocks by extending the first block's size
            it->size += nextIt->size;
            // Remove the second block since it's now part of the first
            memoryList.erase(nextIt);
            isCoalesced = true;
            // Don't increment iterator - try to merge with the next block
        } else {
            ++it;
        }
    }

    return isCoalesced;
}

/**
 * Attempts to allocate memory for a process using first-fit algorithm
 *
 * @param memoryList The linked list of memory blocks
 * @param process The process needing memory allocation
 * @param allocatedStartAddress Will be set to the starting address of allocated memory
 * @param allocatedSize Will be set to the size of allocated memory
 * @return True if allocation succeeded, false if no suitable block found
 */
bool allocateMemory(list<MemoryBlock>& memoryList, PCB& process, int& allocatedStartAddress, int& allocatedSize) {
    const int requiredSize = process.maxMemoryNeeded + 10; // PCB size (10) + process memory

    // First-fit algorithm: use the first free block large enough
    for (auto it = memoryList.begin(); it != memoryList.end(); ++it) {
        if (it->processID == -1 && it->size >= requiredSize) {
            // Store allocation information
            allocatedStartAddress = it->startAddress;
            allocatedSize = requiredSize;

            // Update the memory block to show it's allocated
            it->processID = process.processID;
            // Set the process's base address
            process.mainMemoryBase = allocatedStartAddress;

            // If there's remaining space in this block, create a new free block
            int remainingSize = it->size - requiredSize;
            if (remainingSize > 0) {
                MemoryBlock newFreeBlock{-1, allocatedStartAddress + requiredSize, remainingSize};
                it->size = requiredSize; // Resize current block
                memoryList.insert(next(it), newFreeBlock);
            }

            return true; // Allocation successful
        }
    }

    return false; // No suitable free block found
}

/**
 * Releases memory used by a terminated process
 *
 * @param memoryList The linked list of memory blocks
 * @param processID ID of the process whose memory should be released
 * @param mainMemory The system's main memory
 */
void releaseMemory(list<MemoryBlock>& memoryList, int processID, vector<int>& mainMemory) {
    for (auto& block : memoryList) {
        if (block.processID == processID) {
            cout << "Process " << processID << " terminated and released memory from "
                 << block.startAddress << " to " << (block.startAddress + block.size - 1) << ".\n";

            // Mark block as free
            block.processID = -1;

            // Clear memory values in the main memory array
            for (int i = block.startAddress; i < block.startAddress + block.size; ++i) {
                mainMemory[i] = -1;
            }

            return; // Exit after finding and freeing the block
        }
    }
}

/**
 * Loads a process into main memory at its allocated location
 * Sets up the PCB and copies instructions and parameters
 *
 * @param process The process to load
 * @param mainMemory The system's main memory
 */
void loadProcessToMemory(const PCB& process, vector<int>& mainMemory) {
    constexpr int PCB_SIZE = 10;

    // Calculate memory layout
    int processBase = process.mainMemoryBase;
    int instructionBase = processBase + PCB_SIZE;
    int numInstructions = process.instructions.size();
    int dataBase = instructionBase + numInstructions;

    // Store PCB information in main memory
    mainMemory[processBase]     = process.processID;
    mainMemory[processBase + 1] = READY;                 // Initial state is READY
    mainMemory[processBase + 2] = 0;                     // Initial program counter
    mainMemory[processBase + 3] = instructionBase;       // Where instructions begin
    mainMemory[processBase + 4] = dataBase;              // Where data/parameters begin
    mainMemory[processBase + 5] = process.memoryLimit;   // Memory limit
    mainMemory[processBase + 6] = 0;                     // Initial CPU cycles used
    mainMemory[processBase + 7] = 0;                     // Initial register value
    mainMemory[processBase + 8] = process.maxMemoryNeeded; // Max memory needed
    mainMemory[processBase + 9] = processBase;           // Base address (self-reference)

    // Store instruction opcodes sequentially
    for (int i = 0; i < numInstructions; ++i) {
        mainMemory[instructionBase + i] = process.instructions[i].type;
    }

    // Store instruction parameters after all instructions
    int dataIndex = dataBase;
    for (const auto& inst : process.instructions) {
        for (int param : inst.parameters) {
            mainMemory[dataIndex++] = param;
        }
    }
}

/**
 * Calculates the offset into the data section for parameters of the current instruction
 * This is needed because different instructions have different numbers of parameters
 *
 * @param mainMemory The system's main memory
 * @param instructionBase Starting address of instructions for the process
 * @param currentInstructionIndex Index of the current instruction
 * @return Offset into the data section for the current instruction's parameters
 */
int calculateParamOffset(const vector<int>& mainMemory, int instructionBase, int currentInstructionIndex) {
    int offset = 0;

    // Add up parameter counts for all preceding instructions
    for (int i = 0; i < currentInstructionIndex; ++i) {
        switch (mainMemory[instructionBase + i]) {
            case 1: // Compute: 2 parameters
            case 3: // Store: 2 parameters
                offset += 2;
                break;
            case 2: // Print: 1 parameter
            case 4: // Load: 1 parameter
                offset += 1;
                break;
        }
    }

    return offset;
}

//======================================================
// MAIN SYSTEM FUNCTIONS
//======================================================

/**
 * Attempts to load processes from the new job queue into memory
 * Will try memory coalescing if allocation fails
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

        // Exit if no more jobs to load
        if (newJobQueue.empty()) break;

        PCB currentProcess = newJobQueue.front();
        int allocatedAddress = 0;
        int allocatedSize = 0;

        // Try to allocate memory for this process
        if (allocateMemory(memoryList, currentProcess, allocatedAddress, allocatedSize)) {
            // Successfully allocated memory
            cout << "Process " << currentProcess.processID << " loaded into memory at address "
                 << allocatedAddress << " with size " << allocatedSize << ".\n";

            newJobQueue.pop();
            loadProcessToMemory(currentProcess, mainMemory);
            readyQueue.push(currentProcess.mainMemoryBase);
            jobLoaded = true;
        } else {
            // Try memory coalescing if allocation failed
            cout << "Insufficient memory for Process " << currentProcess.processID
                 << ". Attempting memory coalescing.\n";

            if (coalesceMemory(memoryList)) {
                // Try allocation again after coalescing
                if (allocateMemory(memoryList, currentProcess, allocatedAddress, allocatedSize)) {
                    // Successfully allocated after coalescing
                    cout << "Memory coalesced. Process " << currentProcess.processID
                         << " can now be loaded.\n";

                    cout << "Process " << currentProcess.processID << " loaded into memory at address "
                         << allocatedAddress << " with size " << allocatedSize << ".\n";

                    newJobQueue.pop();
                    loadProcessToMemory(currentProcess, mainMemory);
                    readyQueue.push(currentProcess.mainMemoryBase);
                    jobLoaded = true;
                } else {
                    // Still no suitable memory even after coalescing
                    cout << "Process " << currentProcess.processID
                         << " waiting in NewJobQueue due to insufficient memory.\n";
                    break;
                }
            } else {
                // No memory blocks could be coalesced
                cout << "Process " << currentProcess.processID
                     << " waiting in NewJobQueue due to insufficient memory.\n";
                break;
            }
        }
    } while (jobLoaded);  // Continue as long as we successfully load jobs
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

    // Check each process in the I/O waiting queue
    for (int i = 0; i < queueSize; ++i) {
        IOProcess ioProc = ioWaitingQueue.front();
        ioWaitingQueue.pop();

        // If the process has waited long enough (elapsed time >= required wait time)
        if (globalClock - ioProc.waitStartTime >= ioProc.waitCycles) {
            int procID = mainMemory[ioProc.procAddr];

            // Add I/O time to the process's CPU cycles record
            mainMemory[ioProc.procAddr + 6] += ioProc.waitCycles;
            // Change state from IO_WAITING to READY
            mainMemory[ioProc.procAddr + 1] = READY;

            cout << "print\n";
            cout << "Process " << procID << " completed I/O and is moved to the ReadyQueue.\n";

            // Add the process to the ready queue
            readyQueue.push(ioProc.procAddr);
        } else {
            // Process still needs to wait longer, put it back in the queue
            ioWaitingQueue.push(ioProc);
        }
    }
}

/**
 * Executes a process on the CPU for its time quantum or until completion/I/O
 * Handles timeouts, I/O operations, and process termination
 *
 * @param startAddress Base address of the process in main memory
 * @param mainMemory The system's main memory
 * @param readyQueue Queue of processes ready to execute
 * @param ioWaitingQueue Queue of processes waiting for I/O
 * @param memoryList List of memory blocks
 * @param newJobQueue Queue of processes waiting to be loaded
 */
void executeCPU(int startAddress, vector<int>& mainMemory, queue<int>& readyQueue,
               queue<IOProcess>& ioWaitingQueue, list<MemoryBlock>& memoryList,
               queue<PCB>& newJobQueue) {
    // Reconstruct PCB from memory
    PCB process;
    process.processID = mainMemory[startAddress];
    process.mainMemoryBase = startAddress;
    process.cpuCyclesUsed = mainMemory[startAddress + 6];
    process.registerValue = mainMemory[startAddress + 7];
    process.instructionBase = mainMemory[startAddress + 3];
    process.dataBase = mainMemory[startAddress + 4];
    process.memoryLimit = mainMemory[startAddress + 5];

    // Set state to RUNNING
    process.state = RUNNING;
    mainMemory[startAddress + 1] = RUNNING;

    // Get current program counter and calculate number of instructions
    int programCounter = mainMemory[startAddress + 2];
    const int numInstructions = process.dataBase - process.instructionBase;
    int cyclesUsed = 0;  // Cycles used in this CPU burst

    // Apply context switch overhead
    globalClock += contextSwitchTime;

    cout << "Process " << process.processID << " has moved to Running.\n";

    // Record start time if this is the first time the process is running
    if (processStartTimes.find(process.processID) == processStartTimes.end()) {
        processStartTimes[process.processID] = globalClock;
    }

    // Execute instructions until time quantum expires or process terminates/blocks
    while (programCounter < numInstructions && cyclesUsed < CPUAllocated) {
        const int instrAddr = process.instructionBase + programCounter;
        const int opcode = mainMemory[instrAddr];
        const int paramOffset = calculateParamOffset(mainMemory, process.instructionBase, programCounter);
        const int currentParamPtr = process.dataBase + paramOffset;

        // Update program counter in memory
        mainMemory[startAddress + 2] = programCounter;

        // Execute instruction based on opcode
        switch (opcode) {
            case 1: { // Compute - simulates CPU computation
                int compCycles = mainMemory[currentParamPtr + 1];  // Second parameter is cycles

                // Update cycle counters
                cyclesUsed += compCycles;
                globalClock += compCycles;
                process.cpuCyclesUsed += compCycles;
                mainMemory[startAddress + 6] = process.cpuCyclesUsed;  // Update in memory
                cout << "compute\n";
                break;
            }
            case 2: { // Print - simulates I/O operation
                int ioCycles = mainMemory[currentParamPtr];  // Wait time for I/O

                // Set state to IO_WAITING
                mainMemory[startAddress + 1] = IO_WAITING;

                // Move to next instruction before entering I/O wait
                programCounter++;
                mainMemory[startAddress + 2] = programCounter;

                // Create I/O process record and add to queue
                IOProcess ioProc{startAddress, globalClock, ioCycles};
                ioWaitingQueue.push(ioProc);

                cout << "Process " << process.processID << " issued an IOInterrupt and moved to the IOWaitingQueue.\n";
                return;  // Exit CPU execution - process is now waiting
            }
            case 3: { // Store - stores a value at a memory address
                int value = mainMemory[currentParamPtr];         // Value to store
                int addr = mainMemory[currentParamPtr + 1];      // Address to store at

                // Update register
                process.registerValue = value;
                mainMemory[startAddress + 7] = value;  // Update in memory

                // Perform the store if address is valid
                if (addr >= 0 && addr < process.memoryLimit) {
                    mainMemory[process.mainMemoryBase + addr] = value;
                    cout << "stored\n";
                } else {
                    cout << "store error!\n";  // Address out of bounds
                }

                // Update cycle counters (1 cycle for store)
                cyclesUsed += 1;
                globalClock += 1;
                process.cpuCyclesUsed += 1;
                mainMemory[startAddress + 6] = process.cpuCyclesUsed;
                break;
            }
            case 4: { // Load - loads a value from memory to register
                int addr = mainMemory[currentParamPtr];  // Address to load from

                // Perform the load if address is valid
                if (addr >= 0 && addr < process.memoryLimit) {
                    process.registerValue = mainMemory[process.mainMemoryBase + addr];
                    mainMemory[startAddress + 7] = process.registerValue;  // Update in memory
                    cout << "loaded\n";
                } else {
                    cout << "load error!\n";  // Address out of bounds
                }

                // Update cycle counters (1 cycle for load)
                cyclesUsed += 1;
                globalClock += 1;
                process.cpuCyclesUsed += 1;
                mainMemory[startAddress + 6] = process.cpuCyclesUsed;
                break;
            }
            default:
                cout << "Process " << process.processID << " encountered an invalid instruction.\n";
                return;
        }

        // Move to next instruction
        programCounter++;
    }

    // Check if process reached its time quantum (timed out)
    if (programCounter < numInstructions && cyclesUsed >= CPUAllocated) {
        cout << "Process " << process.processID << " has a TimeOut interrupt and is moved to the ReadyQueue.\n";
        mainMemory[startAddress + 2] = programCounter;  // Save current PC
        mainMemory[startAddress + 1] = READY;           // Change state to READY
        readyQueue.push(startAddress);                  // Put back in ready queue
        return;
    }

    // Check if process completed all instructions (terminated)
    if (programCounter >= numInstructions) {
        // Display PC is adjusted for output formatting
        int displayPC = process.instructionBase - 1;

        // Update process state and final values
        mainMemory[startAddress + 1] = TERMINATED;
        mainMemory[startAddress + 2] = displayPC;
        mainMemory[startAddress + 6] = process.cpuCyclesUsed;
        mainMemory[startAddress + 7] = process.registerValue;

        // Record termination time and calculate total execution time
        processEndTimes[process.processID] = globalClock;
        const int execTime = processEndTimes[process.processID] - processStartTimes[process.processID];

        // Print final PCB information
        cout << "Process ID: " << process.processID << "\n";
        cout << "State: TERMINATED\n";
        cout << "Program Counter: " << displayPC << "\n";
        cout << "Instruction Base: " << process.instructionBase << "\n";
        cout << "Data Base: " << process.dataBase << "\n";
        cout << "Memory Limit: " << process.memoryLimit << "\n";
        cout << "CPU Cycles Used: " << process.cpuCyclesUsed << "\n";
        cout << "Register Value: " << process.registerValue << "\n";
        cout << "Max Memory Needed: " << process.memoryLimit << "\n";
        cout << "Main Memory Base: " << process.mainMemoryBase << "\n";
        cout << "Total CPU Cycles Consumed: " << execTime << "\n";

        // Print termination summary
        cout << "Process " << process.processID << " terminated. Entered running state at: "
             << processStartTimes[process.processID] << ". Terminated at: "
             << processEndTimes[process.processID] << ". Total execution time: "
             << execTime << ".\n";

        // Release memory and try to load waiting jobs
        releaseMemory(memoryList, process.processID, mainMemory);
        tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);

        // Check if any I/O waiting processes are ready
        checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);
    } else {
        // Process still has instructions but reached time quantum
        mainMemory[startAddress + 1] = READY;  // Set state to READY
        readyQueue.push(startAddress);         // Put back in ready queue

        // Check if any I/O waiting processes are ready
        checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);
    }
}

/**
 * Main function - initializes the system and runs the simulation
 * Handles input parsing, memory initialization, and the main execution loop
 */
int main() {
    int maxMemory, numProcesses;
    queue<PCB> newJobQueue;     // Processes waiting to be loaded into memory
    queue<int> readyQueue;      // Processes ready to execute
    queue<IOProcess> ioWaitingQueue;  // Processes waiting for I/O completion

    // Read simulation parameters from input
    cin >> maxMemory >> CPUAllocated >> contextSwitchTime;
    cin >> numProcesses;

    // Initialize main memory and memory management list
    vector<int> mainMemory(maxMemory, -1);  // -1 indicates unused memory
    list<MemoryBlock> memoryList{{-1, 0, maxMemory}};  // Start with a single free block

    // Read process definitions
    for (int i = 0; i < numProcesses; ++i) {
        PCB process;
        int pid, memLimit, numInstructions;

        cin >> pid >> memLimit >> numInstructions;

        // Initialize PCB fields
        process.processID = pid;
        process.state = NEW;
        process.programCounter = 0;
        process.memoryLimit = memLimit;
        process.maxMemoryNeeded = memLimit;
        process.numOfInstruction = numInstructions;
        process.cpuCyclesUsed = 0;
        process.registerValue = 0;
        process.mainMemoryBase = -1;  // Will be set during memory allocation

        // Read all instructions for this process
        for (int j = 0; j < numInstructions; ++j) {
            Instruction inst;
            cin >> inst.type;

            // Read parameters based on instruction type
            switch(inst.type) {
                case 1: { // Compute instruction: iterations, cpuCycles
                    int iterations, cpuCycles;
                    cin >> iterations >> cpuCycles;
                    inst.parameters = {iterations, cpuCycles};
                    break;
                }
                case 2: { // Print/I/O instruction: cycles
                    int cycles;
                    cin >> cycles;
                    inst.parameters = {cycles};
                    break;
                }
                case 3: { // Store instruction: value, address
                    int value, address;
                    cin >> value >> address;
                    inst.parameters = {value, address};
                    break;
                }
                case 4: { // Load instruction: address
                    int address;
                    cin >> address;
                    inst.parameters = {address};
                    break;
                }
                default:
                    cout << "Invalid Instruction Type.\n";
            }
            process.instructions.push_back(inst);
        }

        // Add process to new job queue
        newJobQueue.push(process);
    }

    // Try to load initial processes into memory
    tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);

    // Print memory contents (for debugging)
    for (int i = 0; i < mainMemory.size(); ++i) {
        cout << i << ": " << mainMemory[i] << "\n";
    }

    // Main simulation loop
    // Continue until all queues are empty (no more work to do)
    while (!readyQueue.empty() || !ioWaitingQueue.empty() || !newJobQueue.empty()) {
        // First check if any I/O waiting processes are ready
        checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);

        // Try to load more jobs if ready queue is empty but new jobs exist
        if (readyQueue.empty() && !newJobQueue.empty()) {
            tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);
        }

        if (!readyQueue.empty()) {
            // Execute the next process in the ready queue
            int procAddr = readyQueue.front();
            readyQueue.pop();
            executeCPU(procAddr, mainMemory, readyQueue, ioWaitingQueue, memoryList, newJobQueue);
        } else if (!ioWaitingQueue.empty()) {
            // If only I/O waiting processes exist, advance the clock
            globalClock += contextSwitchTime;
        } else if (!newJobQueue.empty()) {
            // Try memory coalescing as a last resort
            if (coalesceMemory(memoryList)) {
                tryLoadJobsToMemory(newJobQueue, readyQueue, mainMemory, memoryList);

                // If still can't load jobs after coalescing
                if (readyQueue.empty() && !newJobQueue.empty()) {
                    break;  // Exit - we can't make further progress
                }
            } else {
                // No memory can be coalesced and no jobs can run
                break;  // Exit - we can't make further progress
            }
        }
    }

    // Apply final context switch time
    globalClock += contextSwitchTime;

    // Print total simulation time
    cout << "Total CPU time used: " << globalClock << ".\n";
    return 0;
}