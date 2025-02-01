
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
    int processID;           // Unique identifier for the process

    // Process state management
    int state;              // Process states:
                            // 0: NEW - Just created
                            // 1: READY - Ready for execution
                            // 2: RUNNING - Currently executing
                            // 3: TERMINATED - Execution completed

    // Memory management
    int instructionBase;    // Starting address of instructions in logical memory
    int dataBase;           // Starting address of data segment in logical memory
    int memoryLimit;        // Total size of logical memory allocated
    int maxMemoryNeeded;    // Maximum memory required (from input)
    int mainMemoryBase;     // Starting address in physical memory

    // Execution management
    int programCounter;     // Index of next instruction to execute
    int cpuCyclesUsed;      // Total CPU cycles consumed
    int registerValue;      // Simulated register for operations

    // Instruction management
    int numOfInstruction;   // Number of instructions to execute
    vector<Instruction> instructions;  // Store actual instructions
};

void initializePCB(PCB& process, const int pid, const int memLimit, const int numInstructions) {
    process.processID = pid;
    process.state = 0;  // NEW state
    process.programCounter = 0;
    process.instructionBase = 0;

    // First, we store all instruction types sequentially
    int instructionTypeSpace = numInstructions;

    // Then calculate space for all parameters
    int parameterSpace = 0;
    // We'll calculate this when we actually read the instructions
    // Compute (2 params), Print (1 param), Store (2 params), Load (1 param)

    process.dataBase = instructionTypeSpace;  // Parameters start after instruction types
    process.memoryLimit = memLimit;
    process.maxMemoryNeeded = memLimit;
    process.mainMemoryBase = -1;
    process.cpuCyclesUsed = 0;
    process.registerValue = 0;
    process.numOfInstruction = numInstructions;
    process.instructions.clear();
}
void loadJobsToMemory(queue<PCB>& newJobQueue, queue<int>& readyQueue,
vector<int>& mainMemory, int maxMemory) {
    // TODO: Implement loading jobs into main memory
    int currentMemoryPosition = 0;  // Tracks position in main memory

    // Process each job in the new job queue
    while (!newJobQueue.empty()) {
        PCB currentProcess = newJobQueue.front();
        newJobQueue.pop();

        // Calculate total space needed for this process
        const int instructionTypeSpace = currentProcess.numOfInstruction;  // Space for instruction types
        
        // Calculate parameter space
        int parameterSpace = 0;
        for (const auto&[type, parameters] : currentProcess.instructions) {
            parameterSpace += parameters.size();  // Add space for each parameter
        }

        int totalSpaceNeeded = instructionTypeSpace + parameterSpace;

        // Check if we have enough memory
        if (currentMemoryPosition + totalSpaceNeeded <= maxMemory) {
            // Set main memory base for this process
            currentProcess.mainMemoryBase = currentMemoryPosition;
            
            // 1. Load instruction types
            int parameterStartPosition = currentMemoryPosition + instructionTypeSpace;
            int currentParamPosition = parameterStartPosition;

            for (int i = 0; i < currentProcess.numOfInstruction; i++) {
                // Store instruction type
                mainMemory[currentMemoryPosition + i] = currentProcess.instructions[i].type;
                
                // Store parameters for this instruction
                for (int param : currentProcess.instructions[i].parameters) {
                    mainMemory[currentParamPosition++] = param;
                }
            }

            // Update process state to READY
            currentProcess.state = 1;  // READY state

            // Add base address to ready queue
            readyQueue.push(currentProcess.mainMemoryBase);

            // Print loading details
            cout << "Process " << currentProcess.processID << " loaded into memory:\n";
            cout << "Base Address: " << currentProcess.mainMemoryBase << "\n";
            cout << "Instruction Types: " << instructionTypeSpace << " units\n";
            cout << "Parameter Space: " << parameterSpace << " units\n";
            cout << "Total Space: " << totalSpaceNeeded << " units\n";
            
            // Print memory layout
            cout << "\nMemory Layout:\n";
            cout << "Instruction Types:\n";
            for (int i = 0; i < instructionTypeSpace; i++) {
                cout << "Address " << (currentProcess.mainMemoryBase + i) 
                     << ": Type " << mainMemory[currentProcess.mainMemoryBase + i] << "\n";
            }
            
            cout << "\nParameters:\n";
            for (int i = 0; i < parameterSpace; i++) {
                cout << "Address " << (parameterStartPosition + i) 
                     << ": Value " << mainMemory[parameterStartPosition + i] << "\n";
            }
            cout << "\n";

            // Update current memory position
            currentMemoryPosition = currentParamPosition;
        } else {
            cout << "Error: Not enough memory for Process " << currentProcess.processID << "\n";
            cout << "Required: " << totalSpaceNeeded << " units\n";
            cout << "Available: " << (maxMemory - currentMemoryPosition) << " units\n\n";
            
            // Put process back in queue (or handle error differently if needed)
            newJobQueue.push(currentProcess);
            break;
        }
    }

    // Print final memory state
    cout << "\nFinal Memory State:\n";
    cout << "Used Memory: " << currentMemoryPosition << " units\n";
    cout << "Free Memory: " << (maxMemory - currentMemoryPosition) << " units\n";
    cout << "Ready Queue Size: " << readyQueue.size() << " processes\n";
}
void executeCPU(int startAddress, int* mainMemory) {
    // TODO: Implement CPU instruction execution
    int processID = mainMemory[startAddress + 0];
    int state = mainMemory[startAddress + 1];
    int programCounter = mainMemory[startAddress + 2];
    int instructionBase = mainMemory[startAddress + 3];
    int dataBase = mainMemory[startAddress + 4];
    int memoryLimit = mainMemory[startAddress + 5];
    int cpuCyclesUsed = mainMemory[startAddress + 6];
    int registerValue = mainMemory[startAddress + 7];
    int maxMemoryNeeded = mainMemory[startAddress + 8];
    int mainMemoryBase = mainMemory[startAddress + 9];
    int numOfInstruction = mainMemory[startAddress + 10];

    // Current instruction pointer starts at instructionBase
    int currentInst = instructionBase;
    int paramPtr = dataBase;  // Parameter pointer starts at dataBase

    // Execute each instruction
    for(int i = 0; i < numOfInstruction; i++) {
        switch(mainMemory[currentInst]) {
            case 1: // Compute
                {
                    int iterations = mainMemory[paramPtr];
                    int cycles = mainMemory[paramPtr + 1];
                    cpuCyclesUsed += iterations * cycles;
                    paramPtr += 2;
                    cout << "compute\n";
                }
                break;

            case 2: // Print
                {
                    int cycles = mainMemory[paramPtr];
                    cpuCyclesUsed += cycles;
                    paramPtr += 1;
                    cout << "print\n";
                }
                break;

            case 3: // Store
                {
                    int value = mainMemory[paramPtr];
                    int address = mainMemory[paramPtr + 1];
                    mainMemory[mainMemoryBase + address] = value;
                    registerValue = value;
                    cpuCyclesUsed += 1;
                    paramPtr += 2;
                    cout << "stored\n";
                }
                break;

            case 4: // Load
                {
                    int address = mainMemory[paramPtr];
                    registerValue = mainMemory[mainMemoryBase + address];
                    cpuCyclesUsed += 1;
                    paramPtr += 1;
                    cout << "loaded\n";
                }
                break;
        }
        currentInst++;
    }

    // Update PCB in memory
    mainMemory[startAddress + 1] = 3;  // Set state to TERMINATED
    mainMemory[startAddress + 2] = instructionBase - 1;  // Set PC to before instructionBase
    mainMemory[startAddress + 6] = cpuCyclesUsed;
    mainMemory[startAddress + 7] = registerValue;

    // Print final PCB state
    cout << "Process ID: " << processID << "\n";
    cout << "State: TERMINATED\n";
    cout << "Program Counter: " << (instructionBase - 1) << "\n";
    cout << "Instruction Base: " << instructionBase << "\n";
    cout << "Data Base: " << dataBase << "\n";
    cout << "Memory Limit: " << memoryLimit << "\n";
    cout << "CPU Cycles Used: " << cpuCyclesUsed << "\n";
    cout << "Register Value: " << registerValue << "\n";
    cout << "Max Memory Needed: " << maxMemoryNeeded << "\n";
    cout << "Main Memory Base: " << mainMemoryBase << "\n";
    cout << "Total CPU Cycles Consumed: " << cpuCyclesUsed << "\n";
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
        process.state = 0;  // NEW state
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
                        int iterations, cycles;
                        cin >> iterations >> cycles;
                        inst.parameters = {iterations, cycles};
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
        executeCPU(startAddress, mainMemory.data());
    }

    return 0;
}