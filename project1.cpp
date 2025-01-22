
#include <iostream>
#include <queue>
#include <string>
using namespace std;
struct Instruction {
    int type;               // Instruction type : 1 (Compute), 2 (Print), 3 (Store), 4 (Load)
    vector<int> parameters; // parameters for the instruction
};
struct PCB {
    // Define PCB fields as described earlier
    int processID;          // Unique identifier for the process
    int state;              // Process state: NEW, READY, RUNNING, TERMINATED
    int programCounter;     // Index of the next instruction to be executed
    int instructionBase;    // Starting address of instructions in logical memory
    int dataBase;           // Beginning address of the data segment
    int iteConsuming;       // Num of iterations
    int numOfInstruction;   // Number of instructions
    int typeOfOperations;   // type of operations  1 (Compute), 2 (Print), 3 (Store), 4 (Load)
    int memoryLimit;        // Total size of logical memory allocated
    int cpuCyclesUsed;      // Total CPU cycles consumed during execution
    int registerValue;      // Simulated register for intermediate values
    int maxMemoryNeeded;    // Maximum memory required by the process
    int mainMemoryBase;     // Starting address in main memory
};


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
     int* mainMemory;
    // Step 1: Read and parse input file
    // TODO: Implement input parsing and populate newJobQueue
    cin >> maxMemory >> numProcesses;
    vector<int> Memory(maxMemory);


    // Step 2: Load jobs into main memory
    for (int i = 0; i < numProcesses;i++) {
        PCB process{};
        cin >> process.processID >> process.maxMemoryNeeded >> process.numOfInstruction;
        for (int j = 0; j < process.numOfInstruction; j++) {
            cin >> process.typeOfOperations >> process.iteConsuming >> process.cpuCyclesUsed;
        }
        newJobQueue.push(process);
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
        executeCPU(startAddress, mainMemory);
        // Output Job that just completed execution – see example below
    }
    return 0;
}
