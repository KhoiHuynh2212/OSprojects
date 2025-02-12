
#include <iostream>
#include <queue>
#include <string>
using namespace std;
struct Instruction {
    int type{};                // Instruction type : 1 (Compute), 2 (Print), 3 (Store), 4 (Load)
    vector<int> parameters;   // parameters for the instruction
};
struct PCB {
    // Process identification
    int processID{};           // Unique identifier for the process

    // Process state management
    int state{};              // Process states:
                            // 0: NEW - Just created
                            // 1: READY - Ready for execution
                            // 2: RUNNING - Currently executing
                            // 3: TERMINATED - Execution completed

    // Memory management
    int instructionBase{};    // Starting address of instructions in logical memory
    int dataBase{};           // Starting address of data segment in logical memory
    int memoryLimit{};        // Total size of logical memory allocated
    int maxMemoryNeeded{};    // Maximum memory required (from input)
    int mainMemoryBase{};     // Starting address in physical memory

    // Execution management
    int programCounter{};     // Index of next instruction to execute
    int cpuCyclesUsed{};      // Total CPU cycles consumed
    int registerValue{};      // Simulated register for operations

    // Instruction management
    int numOfInstruction{};   // Number of instructions to execute
    vector<Instruction> instructions;  // Store actual instructions
};
enum processState{
    NEW = 0,
    READY = 1,
    RUNNING = 2,
    TERMINATED = 3
};
void initializePCB(PCB& process, const int pid, const int memLimit, const int numInstructions) {
    process.processID = pid;
    process.state = 0;  // NEW state
    process.programCounter = 0;
    process.instructionBase = 0;
    // First, we store all instruction types sequentially
    int instructionTypeSpace = numInstructions;

    // Then calculate space for all parameters
    // We'll calculate this when we actually read the instructions
    // Compute (2 params), Print (1 param), Store (2 params), Load (1 param)
    process.dataBase = instructionTypeSpace;  // Parameters start after instruction types
    process.memoryLimit = memLimit;
    process.maxMemoryNeeded = memLimit;
    process.mainMemoryBase = 0;
    process.cpuCyclesUsed = 0;
    process.registerValue = 0;
    process.numOfInstruction = numInstructions;
    process.instructions.clear();
}

void loadJobsToMemory(queue<PCB>& newJobQueue, queue<int>& readyQueue,
                      vector<int>& mainMemory, const int maxMemory) {
    // TODO: Implement loading jobs into main memory
    int currentMemoryPosition = 0;

    while (!newJobQueue.empty() && currentMemoryPosition < maxMemory) {
        PCB currentProcess = newJobQueue.front();
        newJobQueue.pop();

        // Calculate segments
        const int PCB_SIZE = 10;
        const int processBase = currentMemoryPosition;
        const int instructionBase = processBase + PCB_SIZE;
        const int numInstructions = currentProcess.instructions.size();
        const int dataBase = instructionBase + numInstructions;

        // Store PCB
        mainMemory[processBase] = currentProcess.processID;
        mainMemory[processBase + 1] = READY;
        mainMemory[processBase + 2] = 0;  // PC points before instruction base
        mainMemory[processBase + 3] = instructionBase;
        mainMemory[processBase + 4] = dataBase;
        mainMemory[processBase + 5] = currentProcess.memoryLimit;
        mainMemory[processBase + 6] = currentProcess.cpuCyclesUsed;  // CPU cycles
        mainMemory[processBase + 7] = 0;  // Register
        mainMemory[processBase + 8] = currentProcess.maxMemoryNeeded;
        mainMemory[processBase + 9] = processBase;

        // Debugging memory storage
        /*cout << "Loading Process ID: " << currentProcess.processID << " at memory base: " << processBase << endl;
        cout << "Instruction base: " << instructionBase << ", Data base: " << dataBase << endl;
        cout << "Maximum memory size: " << currentProcess.maxMemoryNeeded << endl;*/

        // First store all instructions
        for (int i = 0; i < numInstructions; i++) {
            mainMemory[instructionBase + i] = currentProcess.instructions[i].type;
        }

        // Then store all parameters in data section
        int dataIndex = dataBase;
        for (const auto& inst : currentProcess.instructions) {
            for (int param : inst.parameters) {
                mainMemory[dataIndex++] = param;
            }
        }
        readyQueue.push(processBase);
        currentMemoryPosition += currentProcess.memoryLimit + 10;
    }
}

void executeCPU(int startAddress, vector<int>& mainMemory) {
    // TODO: Implement the CPU execution
    // Create and initialize PCB for the process
    PCB process;
    process.processID = mainMemory[startAddress];
    process.state = *"RUNNING";
    process.mainMemoryBase = startAddress;
    process.cpuCyclesUsed = 0;
    process.registerValue = -1;
    process.instructionBase = mainMemory[startAddress + 3];
    process.dataBase = mainMemory[startAddress + 4];
    process.memoryLimit = mainMemory[startAddress + 5];
    process.programCounter = startAddress + 9;

    // Set process to RUNNING state
    mainMemory[startAddress + 1] = RUNNING;

    // Calculate number of instructions
    int numInstructions = process.dataBase - process.instructionBase;
    int pc = process.dataBase;

    // Execute instructions
    for(int i = 0; i < numInstructions; i++) {
        const int opcode = mainMemory[process.instructionBase + i];
        if (opcode == 0) break;

        switch (opcode) {
            case 1: { // Compute
                mainMemory[pc++];
                int cycles = mainMemory[pc++];
                process.cpuCyclesUsed += cycles;
                cout << "compute\n";
                break;
            }
            case 2: { // Print
                cout << "print\n";
                int cycles = mainMemory[pc++];
                process.cpuCyclesUsed += cycles;
                break;
            }
            case 3: { // Store
                process.cpuCyclesUsed++;
                int value = mainMemory[pc++];
                int address = mainMemory[pc++];
                process.registerValue = value;
                if (address >= 0 && address < process.memoryLimit) {
                    mainMemory[address + process.mainMemoryBase] = value;
                    cout << "stored\n";
                } else {
                    cout << "store error!\n";
                }
                break;
            }
            case 4: { // Load
                process.cpuCyclesUsed++;
                int address = mainMemory[pc++];
                if (address >= 0 && address < process.memoryLimit) {
                    cout << "loaded\n";
                    process.registerValue = mainMemory[address + process.mainMemoryBase];
                } else {
                    cout << "load error!\n";
                }
                break;
            }
            default:
                cout << "Process " << process.processID << ": Invalid instruction." << endl;
                return;
        }
    }

    // Update PCB in memory after execution
    mainMemory[startAddress + 1] = TERMINATED;
    mainMemory[startAddress + 6] = process.cpuCyclesUsed;
    mainMemory[startAddress + 7] = process.registerValue;
    // Print final process state
    cout << "Process ID: " << process.processID << "\n";
    cout << "State: ";
    switch(mainMemory[startAddress + 1]) {
        case NEW: cout << "NEW"; break;
        case READY: cout << "READY"; break;
        case RUNNING: cout << "RUNNING"; break;
        case TERMINATED: cout << "TERMINATED\n"; break;
        default: cout << "UNKNOWN"; break;
    }

    cout << "Program Counter: " << process.programCounter << "\n";
    cout << "Instruction Base: " << process.instructionBase << "\n";
    cout << "Data Base: " << process.dataBase << "\n";
    cout << "Memory Limit: " << process.memoryLimit << "\n";
    cout << "CPU Cycles Used: " << process.cpuCyclesUsed << "\n";
    cout << "Register Value: " << process.registerValue << "\n";
    cout << "Max Memory Needed: " <<process.memoryLimit << "\n";
    cout << "Main Memory Base: " << process.mainMemoryBase << "\n";
    cout << "Total CPU Cycles Consumed: " << process.cpuCyclesUsed << "\n";
}

int main() {
    int maxMemory;
    int numProcesses;
    queue<PCB> newJobQueue;
    queue<int> readyQueue;

    // Read initial configuration
    cin >> maxMemory >> numProcesses;
    vector mainMemory(maxMemory, -1);

    // Read each process
    for (int i = 0; i < numProcesses; i++) {
        PCB process;
        int pid, memLimit, numInstructions;

        // Read process basic information
        cin >> pid >> memLimit >> numInstructions;

        // Initialize PCB with basic info
        process.processID = pid;
        process.state = NEW;  // NEW state
        process.programCounter = 0;
        process.memoryLimit = memLimit;
        process.maxMemoryNeeded = memLimit;
        process.numOfInstruction = numInstructions;
        process.cpuCyclesUsed = 0;
        process.registerValue = 0;
        process.mainMemoryBase = -1;  // Will be set in loadJobsToMemory

        // Read instructions
        for (int j = 0; j < numInstructions; j++) {
            Instruction inst;
            cin >> inst.type;

            switch(inst.type) {
                case 1: // Compute
                    {
                        int iterations, cpuCycles;
                        cin >> iterations >> cpuCycles;
                        inst.parameters = {iterations, cpuCycles};
                    }
                    break;
                case 2: // Print
                    {
                        int cycles;
                        cin >> cycles;
                        inst.parameters = {cycles};
                    }
                    break;
                case 3: // Store
                    {
                        int value, address;
                        cin >> value >> address;
                        inst.parameters = {value, address};
                    }
                    break;
                case 4: // Load
                    {
                        int address;
                        cin >> address;
                        inst.parameters = {address};
                    }
                    break;
                default:
                    cout << "Invalid Instruction Type: \n";
            }
            process.instructions.push_back(inst);
        }
        newJobQueue.push(process);
    }

    // Load processes into memory
    loadJobsToMemory(newJobQueue, readyQueue, mainMemory, maxMemory);

    // Print memory state
    for (int i = 0; i < maxMemory; i++) {
        cout << i << " : " << mainMemory[i] << "\n";
    }

    // Execute processes
    while (!readyQueue.empty()) {
        int startAddress = readyQueue.front();
        readyQueue.pop();
        // Execute process
        executeCPU(startAddress, mainMemory);
    }

    return 0;
}