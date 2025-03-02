#include <iostream>
#include <queue>
#include <vector>
#include <unordered_map>
using namespace std;

// Global variables
int globalClock = 0;
int CPUAllocated = 0;      // Maximum cycles a process may execute in one burst
int contextSwitchTime = 0; // Clock cycles needed for a context switch

// Global maps to track process timing info using their base address as key.
unordered_map<int, int> processStartTimes;
unordered_map<int, int> processEndTimes;

struct Instruction {
    int type{};                // 1: Compute, 2: Print, 3: Store, 4: Load
    vector<int> parameters;
};

struct PCB {
    int processID{};           // Unique identifier for the process
    int state{};               // 0: NEW, 1: READY, 2: RUNNING, 3: TERMINATED
    int instructionBase{};     // Starting address for instructions in mainMemory
    int dataBase{};            // Starting address for parameters (data) in mainMemory
    int memoryLimit{};         // Size of memory allocated to the process
    int maxMemoryNeeded{};
    int mainMemoryBase{};      // Where the PCB is stored in mainMemory
    int programCounter{};      // Next instruction index (relative to instructionBase)
    int cpuCyclesUsed{};       // Total CPU cycles used so far
    int registerValue{};       // Simulated register value
    int numOfInstruction{};    // Number of instructions for the process
    vector<Instruction> instructions;
};

enum processState {
    NEW ,READY ,RUNNING ,TERMINATED, IO_WAITING
};

// Structure for processes waiting for I/O.
struct IOProcess {
    int procAddr;      // Base address of the process in mainMemory
    int waitStartTime; // Global clock when process entered I/O waiting
    int waitCycles;    // Number of cycles the process must wait
};

// Loads processes from newJobQueue into mainMemory and pushes their base addresses into readyQueue.
void loadJobsToMemory(queue<PCB>& newJobQueue, queue<int>& readyQueue,
                        vector<int>& mainMemory, const int maxMemory) {
    int currentMemoryPosition = 0;
    while (!newJobQueue.empty() && currentMemoryPosition < maxMemory) {
        constexpr int PCB_SIZE = 10;
        PCB currentProcess = newJobQueue.front();
        newJobQueue.pop();

        int processBase = currentMemoryPosition;
        int instructionBase = processBase + PCB_SIZE;
        int numInstructions = currentProcess.instructions.size();
        int dataBase = instructionBase + numInstructions;

        // Store PCB info into mainMemory:
        mainMemory[processBase]     = currentProcess.processID;
        mainMemory[processBase + 1] = READY;
        mainMemory[processBase + 2] = 0;                 // Initial programCounter = 0
        mainMemory[processBase + 3] = instructionBase;   // Instruction base
        mainMemory[processBase + 4] = dataBase;          // Data base
        mainMemory[processBase + 5] = currentProcess.memoryLimit;
        mainMemory[processBase + 6] = 0;              // Initial cpuCyclesUsed = 0
        mainMemory[processBase + 7] = 0;              // Initial register value = 0
        mainMemory[processBase + 8] = currentProcess.maxMemoryNeeded;
        mainMemory[processBase + 9] = processBase;    // Save base address

        // Store instructions sequentially.
        for (int i = 0; i < numInstructions; i++) {
            mainMemory[instructionBase + i] = currentProcess.instructions[i].type;
        }
        // Store instruction parameters (data) after instructions.
        int dataIndex = dataBase;
        for (auto& inst : currentProcess.instructions) {
            for (int param : inst.parameters) {
                mainMemory[dataIndex++] = param;
            }
        }
        readyQueue.push(processBase);
        currentMemoryPosition += currentProcess.memoryLimit + PCB_SIZE;
    }
}

// Helper: Computes how many parameter values have been consumed by instructions already executed.
int calculateParamOffset(const vector<int>& mainMemory, int instructionBase, int currentInstructionIndex) {
    int offset = 0;
    for (int i = 0; i < currentInstructionIndex; i++) {
        int op = mainMemory[instructionBase + i];
        if (op == 1 || op == 3)
            offset += 2; // Compute and Store have 2 parameters
        else if (op == 2 || op == 4)
            offset += 1; // Print and Load have 1 parameter
    }
    return offset;
}

// Checks the IOWaitingQueue and moves processes that have waited long enough back to the readyQueue.
void checkIOWaitingQueue(queue<IOProcess>& ioWaitingQueue, queue<int>& readyQueue,  vector<int>& mainMemory) {
    int size = ioWaitingQueue.size();
    for (int i = 0; i < size; i++) {
        IOProcess ioProc = ioWaitingQueue.front();
        ioWaitingQueue.pop();
        if (globalClock - ioProc.waitStartTime >= ioProc.waitCycles) {
            int procID = mainMemory[ioProc.procAddr]; // Process ID stored at base address

            // Add I/O waiting time to the process's CPU cycles
            int cpuCycles = mainMemory[ioProc.procAddr + 6];
            cpuCycles += ioProc.waitCycles;
            mainMemory[ioProc.procAddr + 6] = cpuCycles;

            mainMemory[ioProc.procAddr + 1] = READY;
            cout << "print\n";
            cout << "Process " << procID << " completed I/O and is moved to the ReadyQueue.\n";
            readyQueue.push(ioProc.procAddr);
        } else {
            ioWaitingQueue.push(ioProc);
        }
    }
}

// Modified executeCPU that uses a while loop, handles timeouts and I/O, and prints PCB details upon termination.
void executeCPU(int startAddress, vector<int>& mainMemory, queue<int>& readyQueue, queue<IOProcess>& ioWaitingQueue) {
    // Reconstruct PCB info from mainMemory.
    PCB process;
    process.processID = mainMemory[startAddress];
    process.mainMemoryBase = startAddress;
    process.cpuCyclesUsed = mainMemory[startAddress + 6];
    process.registerValue = mainMemory[startAddress + 7];
    process.instructionBase = mainMemory[startAddress + 3];
    process.dataBase = mainMemory[startAddress + 4];
    process.memoryLimit = mainMemory[startAddress + 5];

    // Set process state to RUNNING
    process.state = RUNNING;
    mainMemory[startAddress + 1] = RUNNING;

    // Read the cumulative program counter from mainMemory.
    int programCounter = mainMemory[startAddress + 2];
    // Determine the total number of instructions.
    int numInstructions = process.dataBase - process.instructionBase;
    int cyclesUsed = 0;  // Cycles used in this CPU burst

    // Apply context switch time
    globalClock += contextSwitchTime;

    cout << "Process " << process.processID << " has moved to Running.\n";

    // Record start time for process if this is first time running
    if (processStartTimes.find(process.processID) == processStartTimes.end()) {
        processStartTimes[process.processID] = globalClock;
    }

    // Execute instructions while there are instructions left and CPUAllocated cycles remain.
    while (programCounter < numInstructions && cyclesUsed < CPUAllocated) {
        int instrAddr = process.instructionBase + programCounter;
        int opcode = mainMemory[instrAddr];
        int paramOffset = calculateParamOffset(mainMemory, process.instructionBase, programCounter);
        int currentParamPtr = process.dataBase + paramOffset;

        // Save program counter to memory
        mainMemory[startAddress + 2] = programCounter;

        switch (opcode) {
            case 1: { // Compute: expects 2 parameters; second parameter is the cycle cost.
                int iterations = mainMemory[currentParamPtr];     // Not used in this implementation
                int compCycles = mainMemory[currentParamPtr + 1];
                cyclesUsed += compCycles;
                globalClock += compCycles;
                process.cpuCyclesUsed += compCycles;
                mainMemory[startAddress + 6] = process.cpuCyclesUsed;  // Update in memory
                cout << "compute\n";
                break;
            }
            case 2: { // Print (I/O): expects 1 parameter (cycles to wait).
                int ioCycles = mainMemory[currentParamPtr];

                // Set state to IO_WAITING
                mainMemory[startAddress + 1] = IO_WAITING;

                // Move to next instruction before I/O
                programCounter++;
                mainMemory[startAddress + 2] = programCounter;

                // Create IO process record
                IOProcess ioProc;
                ioProc.procAddr = startAddress;
                ioProc.waitStartTime = globalClock;
                ioProc.waitCycles = ioCycles;

                cout << "Process " << process.processID << " issued an IOInterrupt and moved to the IOWaitingQueue.\n";
                ioWaitingQueue.push(ioProc);
                return;  // Exit so process can resume later.
            }
            case 3: { // Store: expects 2 parameters.
                int value = mainMemory[currentParamPtr];
                int addr = mainMemory[currentParamPtr + 1];
                process.registerValue = value;
                mainMemory[startAddress + 7] = value;  // Update register in memory

                if (addr >= 0 && addr < process.memoryLimit) {
                    mainMemory[process.mainMemoryBase + addr] = value;
                    cout << "stored\n";
                } else {
                    cout << "store error!\n";
                }
                cyclesUsed += 1;
                globalClock += 1;
                process.cpuCyclesUsed += 1;
                mainMemory[startAddress + 6] = process.cpuCyclesUsed;  // Update in memory
                break;
            }
            case 4: { // Load: expects 1 parameter.
                int addr = mainMemory[currentParamPtr];
                if (addr >= 0 && addr < process.memoryLimit) {
                    process.registerValue = mainMemory[process.mainMemoryBase + addr];
                    mainMemory[startAddress + 7] = process.registerValue;  // Update register in memory
                    cout << "loaded\n";
                } else {
                    cout << "load error!\n";
                }
                cyclesUsed += 1;
                globalClock += 1;
                process.cpuCyclesUsed += 1;
                mainMemory[startAddress + 6] = process.cpuCyclesUsed;  // Update in memory
                break;
            }
            default:
                cout << "Process " << process.processID << " encountered an invalid instruction.\n";
                return;
        }
        // Increment cumulative PC.
        programCounter++;
    }

    // Check for timeout (only if we broke out of the loop due to CPU allocation)
    if (programCounter < numInstructions && cyclesUsed >= CPUAllocated) {
        cout << "Process " << process.processID << " has a TimeOut interrupt and is moved to the ReadyQueue.\n";
        mainMemory[startAddress + 2] = programCounter; // Save cumulative PC
        mainMemory[startAddress + 1] = READY;          // Set state back to READY
        readyQueue.push(startAddress);
        return;
    }

    // If all instructions have been executed, terminate the process.
    if (programCounter >= numInstructions) {
        int displayPC = process.instructionBase - 1;
        mainMemory[startAddress + 1] = TERMINATED;
        mainMemory[startAddress + 2] = displayPC ;

        // update all CPU cycles in memory before reporting
        mainMemory[startAddress + 6] = process.cpuCyclesUsed;
        mainMemory[startAddress + 7] = process.registerValue;
        processEndTimes[process.processID] = globalClock;
        int execTime = processEndTimes[process.processID] - processStartTimes[process.processID];
        cout << "Process ID: " << process.processID << "\n";
        cout << "State: TERMINATED\n";
        cout << "Program Counter: " << displayPC  << "\n";
        cout << "Instruction Base: " << process.instructionBase << "\n";
        cout << "Data Base: " << process.dataBase << "\n";
        cout << "Memory Limit: " << process.memoryLimit << "\n";
        cout << "CPU Cycles Used: " << process.cpuCyclesUsed << "\n";
        cout << "Register Value: " << process.registerValue << "\n";
        cout << "Max Memory Needed: " << process.memoryLimit << "\n";
        cout << "Main Memory Base: " << process.mainMemoryBase << "\n";
        cout << "Total CPU Cycles Consumed: " << execTime << "\n";


        cout << "Process " << process.processID << " terminated. Entered running state at: "
             << processStartTimes[process.processID] << ". Terminated at: "
             << processEndTimes[process.processID] << ". Total execution time: "
             << execTime << ".\n";

        checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);

    } else {
        // Process still has instructions left; update state and requeue.
        mainMemory[startAddress + 1] = READY;  // Set state back to READY
        readyQueue.push(startAddress);

        checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);
    }
}
int main() {
    int maxMemory, numProcesses;
    queue<PCB> newJobQueue;
    queue<int> readyQueue;
    queue<IOProcess> ioWaitingQueue;

    // Input format: first line contains maxMemory, CPUAllocated, and contextSwitchTime,
    // followed by the number of processes.
    cin >> maxMemory >> CPUAllocated >> contextSwitchTime;
    cin >> numProcesses;

    vector<int> mainMemory(maxMemory, -1);

    // Read each process.
    for (int i = 0; i < numProcesses; i++) {
        PCB process;
        int pid, memLimit, numInstructions;
        cin >> pid >> memLimit >> numInstructions;
        process.processID = pid;
        process.state = NEW;
        process.programCounter = 0;
        process.memoryLimit = memLimit;
        process.maxMemoryNeeded = memLimit;
        process.numOfInstruction = numInstructions;
        process.cpuCyclesUsed = 0;
        process.registerValue = 0;
        process.mainMemoryBase = -1; // To be set in loadJobsToMemory

        for (int j = 0; j < numInstructions; j++) {
            Instruction inst;
            cin >> inst.type;
            switch(inst.type) {
                case 1: {
                    int iterations, cpuCycles;
                    cin >> iterations >> cpuCycles;
                    inst.parameters = {iterations, cpuCycles};
                    break;
                }
                case 2: {
                    int cycles;
                    cin >> cycles;
                    inst.parameters = {cycles};
                    break;
                }
                case 3: {
                    int value, address;
                    cin >> value >> address;
                    inst.parameters = {value, address};
                    break;
                }
                case 4: {
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
        newJobQueue.push(process);
    }

    // Load processes into mainMemory.
    loadJobsToMemory(newJobQueue, readyQueue, mainMemory, maxMemory);

    for (int i = 0; i < mainMemory.size(); i++) {
        cout << i << ": " << mainMemory[i] << "\n";
    }

    // Main simulation loop: run until both readyQueue and ioWaitingQueue are empty.
    while (!readyQueue.empty() || !ioWaitingQueue.empty()) {
        checkIOWaitingQueue(ioWaitingQueue, readyQueue, mainMemory);
        if (!readyQueue.empty()) {
            int procAddr = readyQueue.front();
            readyQueue.pop();
            executeCPU(procAddr, mainMemory, readyQueue, ioWaitingQueue);
        } else if (!ioWaitingQueue.empty()) {
            globalClock += contextSwitchTime;
        }
    }

    globalClock += contextSwitchTime;
    cout << "Total CPU time used: " << globalClock << ".\n";
    return 0;
}