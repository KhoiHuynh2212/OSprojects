
#include <iostream>
#include <queue>
#include <string>
using namespace std;
struct Instruction {
    int type;               // Instruction type : 1 (Compute), 2 (Print), 3 (Store), 4 (Load)
    vector<int> parameters; // parameters for the instruction
};
struct PCB {
    int processID;
    int state;              // 0:NEW, 1:READY, 2:RUNNING, 3:TERMINATED
    int programCounter;
    int instructionBase;
    int dataBase;
    int memoryLimit;
    int cpuCyclesUsed;
    int registerValue;
    int maxMemoryNeeded;
    int mainMemoryBase;
    int numOfInstruction;
    vector<Instruction> instructions;
};

void initializePCB(PCB& process, int pid, int memLimit, int numInstructions) {
    process.processID = pid;
    process.state = 0;  // NEW state
    process.programCounter = 0;
    process.instructionBase = 0;

    // Calculate instruction space and set data base
    int instructionSpace = numInstructions * 4;  // 4 units per instruction
    process.dataBase = instructionSpace;

    process.memoryLimit = memLimit;
    process.maxMemoryNeeded = memLimit;
    process.mainMemoryBase = -1;  // Not yet loaded
    process.cpuCyclesUsed = 0;
    process.registerValue = 0;
    process.numOfInstruction = numInstructions;
    process.instructions.clear();
}
void loadJobsToMemory(queue<PCB>& newJobQueue, queue<int>& readyQueue,
vector<int>& mainMemory, int maxMemory) {
    // TODO: Implement loading jobs into main memory
}
void executeCPU(int startAddress, int* mainMemory) {
    // TODO: Implement CPU instruction execution
}

string getOperationType(int operationCode) {
    switch (operationCode) {
        case 1: return "Compute";
        case 2: return "Print";
        case 3: return "Store";
        case 4: return "Load";
        default: return "Unknown Operation";
    }
}
int main() {
    int maxMemory;
    int numProcesses;
    queue<PCB> newJobQueue;
    queue<int> readyQueue;
    //int* mainMemory;
    // Step 1: Read and parse input file
    // TODO: Implement input parsing and populate newJobQueue
    cin >> maxMemory >> numProcesses;
    vector<int> mainMemory(maxMemory,-1);


    // Read each process
    for (int i = 0; i < numProcesses; i++) {
        PCB process;
        int pid, memLimit, numInstructions;

        // Read process basic information
        cin >> pid >> memLimit >> numInstructions;

        // Initialize PCB with basic info
        initializePCB(process, pid, memLimit, numInstructions);

        cout << "Reading Process " << pid << ":\n";
        cout << "Memory Limit: " << memLimit << "\n";
        cout << "Number of Instructions: " << numInstructions << "\n";

        // Read instructions
        for (int j = 0; j < numInstructions; j++) {
            Instruction inst;
            cin >> inst.type;

            cout << "  Instruction " << j + 1 << ": ";
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
                        cout << "Load (address=" << address << ")\n";
                    }
                    break;
            }
            process.instructions.push_back(inst);
        }

        // Add process to queue
        newJobQueue.push(process);
        cout << "\nProcess " << pid << " PCB Initialized:\n";
        cout << "  Instruction Base: " << process.instructionBase << "\n";
        cout << "  Data Base: " << process.dataBase << "\n";
        cout << "  Memory Layout: " << process.memoryLimit << " units total\n";
        cout << "  Initial State: NEW (0)\n\n";
    }
    // loadJobsToMemory(newJobQueue, readyQueue, mainMemory, maxMemory);
    // Step 3: After you load the jobs in the queue go over the main memory
    // and print the content of mainMemory. It will be in the table format
    // three columns as I had provided you earlier.
    // Step 4: Process execution
    while (!readyQueue.empty()) {
        int startAddress = readyQueue.front();
        //readyQueue contains start addresses w.r.t main memory for jobs
        readyQueue.pop();
        // Execute job
        //executeCPU(startAddress, mainMemory);
        // Output Job that just completed execution – see example below
    }
    return 0;
}
