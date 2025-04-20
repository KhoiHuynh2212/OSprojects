#include <iostream>
#include <queue>
#include <vector>
#include <unordered_map>
#include <list>
#include <algorithm>
using namespace std;

unordered_map<int, int> entryTimeStamps;
unordered_map<int, int> exitTimeStamps;

int systemTimer = 0;
int timeSlice = 0;
int switch_time = 0;

struct Instruction {
    int type{};                // 1=Compute, 2=Print(I/O), 3=Store, 4=Load
    vector<int> parameters;    // Parameters vary by instruction type
};

struct IOtask {
    int procAddr;      // Physical address of segment table in memory
    int waitStartTime; // Global clock value when process began waiting
    int waitCycles;    // Required wait time in cycles
};

struct ProcessCB {
    int processID{};           // Unique identifier for the process
    int state{};               // Current process state
    int instruction_start_base{};     // Logical base address of instructions
    int datalogical_base{};            // Logical base address of data/parameters
    int totalMemoryGiven{};         // Total memory allocated to the process
    int processMemory{};     // Maximum memory required by the process
    int mainMemoryBase{};      // Physical base address of segment table
    int counter{};      // Index of next instruction (logical)
    int cpuCyclesUsed{};       // Total CPU time consumed
    int register_value{};       // Simulated register value
    int numOfInstruction{};    // Total number of instructions
    vector<Instruction> instructions; // Process instructions
};

enum state {
    NEW, READY, RUNNING, TERMINATED, IO_WAITING
};

struct MemoryBlockList {
    int pID;      // Process using this block (-1 if free)
    int startingAddress;   // Physical starting address of the block
    int size;           // Size of the block in memory units
};

bool segmentsAllocation(std::list<MemoryBlockList>& memoryList, ProcessCB& process, int& table_address, std::vector<std::pair<int,int>>& segments);
void setupPCBtable(ProcessCB& process, std::vector<int>& physicalMemory, int table_address, const std::vector<std::pair<int,int>>& segments);
void process_content(ProcessCB& process, std::vector<int>& mainMemory, int segmentTableAddress);
void releaseMainMemory(std::list<MemoryBlockList>& memoryList, int processID, std::vector<int>& mainMemory);
bool merge_free_adjacent(std::list<MemoryBlockList>& memoryList);


// Cache-optimized memory block management
// Instead of linked list, we'll use a vector for better memory locality
bool merge_free_adjacent(list<MemoryBlockList>& memoryList) {
    bool coalesced = false;

    // Convert to vector for better cache locality during sorting and traversal
    vector<MemoryBlockList> blocks(memoryList.begin(), memoryList.end());

    // Sort blocks by address (better cache performance than list sorting)
    sort(blocks.begin(), blocks.end(), [](const MemoryBlockList& a, const MemoryBlockList& b) {
        return a.startingAddress < b.startingAddress;
    });

    // Merge adjacent free blocks with better cache locality
    size_t writeIdx = 0;
    for (size_t readIdx = 1; readIdx < blocks.size(); ++readIdx) {
        MemoryBlockList& current = blocks[writeIdx];
        MemoryBlockList& next = blocks[readIdx];

        // Check if both blocks are free and adjacent
        if (current.pID == -1 && next.pID == -1 &&
            current.startingAddress + current.size == next.startingAddress) {
            // Merge blocks
            current.size += next.size;
            coalesced = true;
        } else {
            // Move to next position and copy if needed
            ++writeIdx;
            if (writeIdx != readIdx) {
                blocks[writeIdx] = blocks[readIdx];
            }
        }
    }

    // Resize vector to remove merged blocks
    if (writeIdx + 1 < blocks.size()) {
        blocks.resize(writeIdx + 1);
    }

    // Convert back to list (required for compatibility with existing code)
    memoryList.clear();
    memoryList.insert(memoryList.begin(), blocks.begin(), blocks.end());

    return coalesced;
}

// Simple TLB cache for address translation
struct TLBEntry {
    int logicalAddress;
    int physicalAddress;
    int segmentTableAddress;  // To identify process
    bool valid;
};

// TLB size should be a power of 2 for efficient modulo with mask
const int TLB_SIZE = 16;
TLBEntry tlbCache[TLB_SIZE];

void initTLB() {
    for (int i = 0; i < TLB_SIZE; i++) {
        tlbCache[i].valid = false;
    }
}

int virtualToPhysical(int logicalAddress, int segmentTableAddress, vector<int>& mainMemory) {
    // Check TLB first (simple direct-mapped cache)
    int tlbIndex = (logicalAddress ^ segmentTableAddress) & (TLB_SIZE - 1);
    if (tlbCache[tlbIndex].valid &&
        tlbCache[tlbIndex].logicalAddress == logicalAddress &&
        tlbCache[tlbIndex].segmentTableAddress == segmentTableAddress) {
        return tlbCache[tlbIndex].physicalAddress;
    }

    // TLB miss - perform full translation
    // Get segment table size and calculate number of segments
    int segmentTableSize = mainMemory[segmentTableAddress];
    int numSegments = segmentTableSize / 2;

    // Prefetch segment table entries into local cache
    // This improves spatial locality for the loop
    struct SegmentEntry {
        int startAddr;
        int size;
    };

    // Stack-allocated array for better cache locality
    SegmentEntry segments[numSegments];
    for (int i = 0; i < numSegments; i++) {
        segments[i].startAddr = mainMemory[segmentTableAddress + 1 + 2*i];
        segments[i].size = mainMemory[segmentTableAddress + 1 + 2*i + 1];
    }

    // Track remaining offset
    int remaining = logicalAddress;

    // Iterate through local segment cache (better cache locality)
    for (int i = 0; i < numSegments; i++) {
        if (remaining < segments[i].size) {
            // Found the segment
            int physicalAddress = segments[i].startAddr + remaining;

            // Update TLB
            tlbCache[tlbIndex].logicalAddress = logicalAddress;
            tlbCache[tlbIndex].physicalAddress = physicalAddress;
            tlbCache[tlbIndex].segmentTableAddress = segmentTableAddress;
            tlbCache[tlbIndex].valid = true;

            return physicalAddress;
        }

        // Move to next segment
        remaining -= segments[i].size;
    }

    // Address out of bounds - only retrieve process ID if needed
    int processID = 0;
    int pcbOffset = segmentTableSize + 1;
    if (segmentTableAddress + pcbOffset < mainMemory.size()) {
        processID = mainMemory[segmentTableAddress + pcbOffset];
    }

    cout << "Memory violation: address " << logicalAddress << " out of bounds for Process "
         << processID << endl;
    return -1;
}

int param_offsetCal(const vector<int>& physicalMemory, int segmentTableAddress,
                         int instrBase, int currentInstructionIndex) {
    int offset = 0;

    // Add parameter counts for all preceding instructions
    for (int i = 0; i < currentInstructionIndex; i++) {
        // Get instruction opcode
        int logicalAddr = instrBase + i;
        int physicalAddr = virtualToPhysical(logicalAddr, segmentTableAddress, const_cast<vector<int>&>(physicalMemory));

        if (physicalAddr == -1) return -1; // Invalid address

        int opcode = physicalMemory[physicalAddr];

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

bool segmentsAllocation(list<MemoryBlockList>& memoryList, ProcessCB& process, int& table_address,
                     vector<pair<int,int>>& segments) {
    // Define constants for memory overhead components
    const int processBlockSize = 10;
    const int table_size = 13; // Keeping your original value

    // Calculate total memory needed
    int processTotalNeed = process.processMemory + processBlockSize + table_size;
    int remaining = processTotalNeed;


    // First, sort memory blocks by address to ensure consistent allocation
    memoryList.sort([](const MemoryBlockList& a, const MemoryBlockList& b) {
        return a.startingAddress < b.startingAddress;
    });


    bool isAllocation = false;
    // First-fit allocation with explicit segment table address handling
    table_address = -1; // Reset to invalid value

    for (auto it = memoryList.begin(); it != memoryList.end() && segments.size() < 6 && remaining > 0; ++it) {
        if (it->pID == -1 && it->size > 0) {  // Free block
            // How much we'll take from this block
            int toAllocate = min(remaining, it->size);

            // If this is the first segment, explicitly set it as segment table
            if (segments.empty()) {
                table_address = it->startingAddress;

            }

            if (!isAllocation) {
                // Need at least enough space for segment table + PCB
                if (it->size >= table_size + processBlockSize) {
                    table_address = it->startingAddress;
                    isAllocation = true;
                } else {
                    continue;
                }
            }


            // Record this segment
            segments.push_back({it->startingAddress, toAllocate});
            remaining -= toAllocate;

            // Split block if needed
            if (toAllocate < it->size) {
                MemoryBlockList newBlock;
                newBlock.pID = -1;  // Free
                newBlock.startingAddress = it->startingAddress + toAllocate;
                newBlock.size = it->size - toAllocate;

                // Resize current block
                it->size = toAllocate;

                // Insert new block
                memoryList.insert(next(it), newBlock);
            }

            // Mark as allocated
            it->pID = process.processID;

        }
    }

    if (remaining > 0) {
        // Not enough memory, release allocated blocks

        for (auto& segment : segments) {
            for (auto it = memoryList.begin(); it != memoryList.end(); ++it) {
                if (it->startingAddress == segment.first && it->pID == process.processID) {
                    it->pID = -1;  // Mark as free
                    break;
                }
            }
        }
        segments.clear();
        table_address = -1; // Reset to invalid value
        return false;
    }

    // Double check segment table address is valid
    if (table_address == -1 || segments.empty()) {

        return false;
    }

    // Explicitly update process.mainMemoryBase
    process.mainMemoryBase = table_address;



    return true;
}
void process_content(ProcessCB& process, vector<int>& mainMemory, int segmentTableAddress) {
    // Load instructions directly after PCB fields
    for (int i = 0; i < process.instructions.size(); i++) {
        int logicalAddr = process.instruction_start_base + i;
        int physicalAddr = virtualToPhysical(logicalAddr, segmentTableAddress, mainMemory);

        if (physicalAddr != -1) {
            mainMemory[physicalAddr] = process.instructions[i].type;
        }
    }

    // Load data (instruction parameters)
    int dataOffset = 0;
    for (auto& instr : process.instructions) {
        for (int param : instr.parameters) {
            int logicalAddr = process.datalogical_base + dataOffset++;
            int physicalAddr = virtualToPhysical(logicalAddr, segmentTableAddress, mainMemory);

            if (physicalAddr != -1) {
                mainMemory[physicalAddr] = param;
            }
        }
    }
}

void releaseMainMemory(list<MemoryBlockList>& memoryList, int processID, vector<int>& mainMemory) {

    for (auto it = memoryList.begin(); it != memoryList.end(); ++it) {
        if (it->pID == processID) {
            // Clear memory
            for (int i = 0; i < it->size; i++) {
                mainMemory[it->startingAddress + i] = -1;
            }
            // Mark block as free
            it->pID = -1;
        }
    }

    cout << "Process " << processID << " terminated and freed memory blocks." << endl;

    // Attempt to coalesce free blocks
    merge_free_adjacent(memoryList);
}

void setupPCBtable(ProcessCB& process, vector<int>& physicalMemory, int table_address,
                            const vector<pair<int,int>>& segments) {
    // First, set up segment table
    int table_size = segments.size() * 2;
    physicalMemory[table_address] = table_size;
	process.mainMemoryBase = table_address;

    // Write segment entries
	for (int i = 0; i < segments.size(); i++) {
		physicalMemory[table_address + 1 + 2*i] = segments[i].first;
		physicalMemory[table_address + 1 + 2*i + 1] = segments[i].second;  // Size
	}

	// Calculate PCB offsets
	int offset = table_size + 1;  // PCB follows segment table
	int instrBase = offset + 10;        // Instructions follow PCB
	int dataBase = instrBase + process.numOfInstruction; // Data follows instructions

    // Store PCB fields after segment table
    physicalMemory[table_address + offset + 0] = process.processID;
    physicalMemory[table_address + offset + 1] = READY;  // state
    physicalMemory[table_address + offset + 2] = 0;      // counter
    physicalMemory[table_address + offset + 3] = instrBase;
    physicalMemory[table_address + offset + 4] = dataBase;
    physicalMemory[table_address + offset + 5] = process.totalMemoryGiven;
    physicalMemory[table_address + offset + 6] = 0;      // cpuCyclesUsed
    physicalMemory[table_address + offset + 7] = 0;      // register_value
    physicalMemory[table_address + offset + 8] = process.processMemory;
    physicalMemory[table_address + offset + 9] = table_address;  // mainMemoryBase

    // Update process structure
	process.instruction_start_base = instrBase;
	process.datalogical_base = dataBase;

	// Use address translation to write instructions and data
	for (int i = 0; i < process.numOfInstruction; i++) {
		int logicalAddr = instrBase + i;
		int physicalAddr = virtualToPhysical(logicalAddr, table_address, physicalMemory);
		if (physicalAddr != -1) {
			physicalMemory[physicalAddr] = process.instructions[i].type;
		}
	}

	// Write parameters
	int dataOffset = 0;
	for (auto& instr : process.instructions) {
		for (int param : instr.parameters) {
			int logicalAddr = dataBase + dataOffset++;
			int physicalAddr = virtualToPhysical(logicalAddr, table_address, physicalMemory);
			if (physicalAddr != -1) {
				physicalMemory[physicalAddr] = param;
			}
		}
	}
}

void job_loading_to_memory(queue<ProcessCB>& newJobQueue,
						 queue<int>& readyQueue,
						 vector<int>& physicalMemory,
						 list<MemoryBlockList>& memoryList)
{
	while (!newJobQueue.empty()) {
		ProcessCB currentProcess = newJobQueue.front();

		int segmentTableAddress = 0;
		vector<pair<int, int>> segments;

		// Check if we have enough memory for at least segment table (size ≥ 13)
		bool for_segment_table = false;
		for (auto it = memoryList.begin(); it != memoryList.end(); ++it) {
			if (it->pID == -1 && it->size >= 13) {
				for_segment_table = true;
				break;
			}
		}

		if (!segmentsAllocation(memoryList, currentProcess, segmentTableAddress, segments)) {
			// Free partial allocations (if any).
			for (auto it = memoryList.begin(); it != memoryList.end(); ++it) {
				if (it->pID == currentProcess.processID) {
					it->pID = -1;  // Mark that block as free again
				}
			}

			// Check if the issue is segment table size or overall memory
			if (!for_segment_table) {
				// No hole big enough for segment table (≥ 13)
				cout << "Process " << currentProcess.processID
					 << " could not be loaded due to insufficient contiguous "
					 << "space for segment table." << endl;
				break;
			} else {
				// Has hole for segment table but not enough total memory
				cout << "Insufficient memory for Process "
					 << currentProcess.processID
					 << ". Attempting memory coalescing." << endl;

				// Attempt coalescing
				if (merge_free_adjacent(memoryList)) {
					if (!segmentsAllocation(memoryList, currentProcess, segmentTableAddress, segments)) {
						// Even after coalescing, still not enough memory
						cout << "Process " << currentProcess.processID
							 << " waiting in NewJobQueue due to insufficient memory." << endl;
						break;
					}
				} else {
					// If coalescing not possible
					cout << "Process " << currentProcess.processID
						 << " waiting in NewJobQueue due to insufficient memory." << endl;
					break;
				}
			}
		}


		setupPCBtable(currentProcess, physicalMemory, segmentTableAddress, segments);
		process_content(currentProcess, physicalMemory, segmentTableAddress);

		cout << "Process " << currentProcess.processID
			 << " loaded with segment table stored at physical address "
			 << segmentTableAddress << endl;

		// Remove from newJobQueue and push to readyQueue
		newJobQueue.pop();
		readyQueue.push(segmentTableAddress);
	}
}

void io_waiting_queue(queue<IOtask>& ioWaitingQueue, queue<int>& readyQueue, vector<int>& mainMemory) {
    if (ioWaitingQueue.empty()) return;
    const int queueSize = ioWaitingQueue.size();

    for (int i = 0; i < queueSize; i++) {
        IOtask ioProc = ioWaitingQueue.front();
        ioWaitingQueue.pop();

        if (ioProc.procAddr < 0 || ioProc.procAddr >= mainMemory.size()) {
            continue;
        }

        // Get segment table size and PCB offset
        int table_size = mainMemory[ioProc.procAddr];
        int pcbOffset = table_size + 1;
        int processID = mainMemory[ioProc.procAddr + pcbOffset];

        // Check if I/O wait time has elapsed
        if (systemTimer - ioProc.waitStartTime >= ioProc.waitCycles) {
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


// Cache for parameter offsets to avoid recalculation
struct ParamOffsetCache {
    int instructionIndex;
    int offset;
    bool valid;
};

const int OFFSET_CACHE_SIZE = 32;
ParamOffsetCache offsetCache[OFFSET_CACHE_SIZE];

void initOffsetCache() {
    for (int i = 0; i < OFFSET_CACHE_SIZE; i++) {
        offsetCache[i].valid = false;
    }
}

void cpuExecution(int table_address, vector<int>& physicalMemory, queue<int>& readyQueue,
                queue<IOtask>& ioWaitingQueue, list<MemoryBlockList>& memoryList,
                queue<ProcessCB>& newJobQueue) {
    // Cache frequently used PCB offsets to reduce memory accesses
    // Get segment table size and PCB offset
    int table_size = physicalMemory[table_address];
    int pcb_offset = table_size + 1;

    // Prefetch PCB into local cache (better locality)
    int pcbCache[10]; // Cache for PCB fields
    for (int i = 0; i < 10; i++) {
        pcbCache[i] = physicalMemory[table_address + pcb_offset + i];
    }

    // Reconstruct PCB from local cache - note fields now follow segment table
    ProcessCB process;
    process.processID = pcbCache[0];
    process.mainMemoryBase = pcbCache[9];
    process.cpuCyclesUsed = pcbCache[6];
    process.register_value = pcbCache[7];
    process.instruction_start_base = pcbCache[3];  // Logical base
    process.datalogical_base = pcbCache[4];        // Logical base
    process.totalMemoryGiven = pcbCache[5];
    process.processMemory = pcbCache[8];

    // Set state to RUNNING
    process.state = RUNNING;
    physicalMemory[table_address + pcb_offset + 1] = RUNNING;

    // Get program counter (logical)
    int instruction_index = physicalMemory[table_address + pcb_offset + 2];

    // Calculate number of instructions (logical)
    int numInstructions = process.datalogical_base - process.instruction_start_base;
    int cyclesUsed = 0;  // Cycles used in this CPU burst

    // Apply context switch overhead
    systemTimer += switch_time;

    cout << "Process " << process.processID << " has moved to Running." << endl;

    // Record start time if first execution
    if (entryTimeStamps.find(process.processID) == entryTimeStamps.end()) {
        entryTimeStamps[process.processID] = systemTimer;
    }

    // Execute instructions until time quantum expires or process terminates/blocks
    while (instruction_index < numInstructions && cyclesUsed < timeSlice) {
        // Translate logical instruction address to physical
        int logicalInstrAddr = process.instruction_start_base + instruction_index;
        int physicalInstrAddr = virtualToPhysical(logicalInstrAddr, table_address, physicalMemory);

        if (physicalInstrAddr == -1) {
            // Memory violation - terminate execution
            cout << "Process " << process.processID << " terminated due to memory violation." << endl;
            physicalMemory[table_address + pcb_offset + 1] = TERMINATED;

            // Record end time and free memory
            exitTimeStamps[process.processID] = systemTimer;
            releaseMainMemory(memoryList, process.processID, physicalMemory);

            // Try to load more jobs
            job_loading_to_memory(newJobQueue, readyQueue, physicalMemory, memoryList);
            return;
        }

        // Get the instruction
        int operationCode = physicalMemory[physicalInstrAddr];

        // Calculate parameter offset and translate parameter address
        int offset = param_offsetCal(physicalMemory, table_address,
                                               process.instruction_start_base, instruction_index);

        if (offset == -1) {
            // Parameter calculation error
            cout << "Process " << process.processID << " terminated due to parameter calculation error." << endl;
            physicalMemory[table_address + pcb_offset + 1] = TERMINATED;

            // Record end time and free memory
            exitTimeStamps[process.processID] = systemTimer;
            releaseMainMemory(memoryList, process.processID, physicalMemory);

            // Try to load more jobs
            job_loading_to_memory(newJobQueue, readyQueue, physicalMemory, memoryList);
            return;
        }

        int logical_address = process.datalogical_base + offset;
        int physical_address = virtualToPhysical(logical_address, table_address, physicalMemory);

        if (physical_address == -1) {
            // Memory violation
            cout << "Process " << process.processID << " terminated due to parameter memory violation." << endl;
            physicalMemory[table_address + pcb_offset + 1] = TERMINATED;

            // Record end time and free memory
            exitTimeStamps[process.processID] = systemTimer;
            releaseMainMemory(memoryList, process.processID, physicalMemory);

            // Try to load more jobs
            job_loading_to_memory(newJobQueue, readyQueue, physicalMemory, memoryList);
            return;
        }

        // Execute instruction based on opcode
        switch (operationCode) {
            case 1: {  // Compute
                int comp_cycles = physicalMemory[physical_address + 1];

                // Update cycle counters
                cyclesUsed += comp_cycles;
                systemTimer += comp_cycles;
                process.cpuCyclesUsed += comp_cycles;

                // Update cycles in PCB
                physicalMemory[table_address + pcb_offset + 6] = process.cpuCyclesUsed;

                cout << "compute\n";
                break;
            }
            case 2: {  // Print/I/O
                int ioCycles = physicalMemory[physical_address];

                // Set state to IO_WAITING
                physicalMemory[table_address + pcb_offset + 1] = IO_WAITING;

                // Advance program counter before entering I/O wait
                instruction_index++;
                physicalMemory[table_address + pcb_offset + 2] = instruction_index;

                // Create I/O process record and add to queue
                IOtask ioProc{table_address, systemTimer, ioCycles};

                cout << "Process " << process.processID << " issued an IOInterrupt and moved to the IOWaitingQueue."
                     << endl;
                ioWaitingQueue.push(ioProc);
                return;  // Exit CPU execution - process is now waiting
            }
            case 3: {  // Store
				int value = physicalMemory[physical_address];
				int logical_address = physicalMemory[physical_address + 1];

				// Translate store address
				int physical_address = virtualToPhysical(logical_address, table_address, physicalMemory);

				if (physical_address != -1) {
					// Update memory and register
					physicalMemory[physical_address] = value;
					process.register_value = value;
					physicalMemory[table_address + pcb_offset + 7] = value;  // Update register in PCB

					cout << "stored\n";
				} else {
					cout << "store error!\n";
				}

				cout << "Logical address " << logical_address << " translated to physical address "
					 << physical_address << " for Process " << process.processID << endl;

				// Update cycle counters (1 cycle for store)
				cyclesUsed += 1;
				systemTimer += 1;
				process.cpuCyclesUsed += 1;
				physicalMemory[table_address + pcb_offset + 6] = process.cpuCyclesUsed;
				break;
            }
            case 4: {  // Load
				int logicalLoadAddr = physicalMemory[physical_address];

				// Translate load address
				int physicalLoadAddr = virtualToPhysical(logicalLoadAddr, table_address, physicalMemory);

				if (physicalLoadAddr != -1) {
					// Load value into register
					process.register_value = physicalMemory[physicalLoadAddr];
					physicalMemory[table_address + pcb_offset + 7] = process.register_value;  // Update in PCB

					cout << "loaded\n";
				} else {
					cout << "load error!\n";
				}
				cout << "Logical address " << logicalLoadAddr << " translated to physical address "
					 << physicalLoadAddr << " for Process " << process.processID << endl;
				// Update cycle counters (1 cycle for load)
				cyclesUsed += 1;
				systemTimer += 1;
				process.cpuCyclesUsed += 1;
				physicalMemory[table_address + pcb_offset + 6] = process.cpuCyclesUsed;
				break;
            }
            default:
                cout << "Process " << process.processID << " encountered an invalid instruction." << endl;
                return;
        }

        // Advance program counter
        instruction_index++;
    }

    // Check if process reached its time quantum (timed out)
    if (instruction_index < numInstructions && cyclesUsed >= timeSlice) {
        cout << "Process " << process.processID << " has a TimeOUT interrupt and is moved to the ReadyQueue." << endl;
        physicalMemory[table_address + pcb_offset + 2] = instruction_index;  // Save current PC
        physicalMemory[table_address + pcb_offset + 1] = READY;           // Change state to READY
        readyQueue.push(table_address);                          // Put back in ready queue

        // Check I/O waiting queue
        io_waiting_queue(ioWaitingQueue, readyQueue, physicalMemory);
        return;
    }

    // Check if process completed all instructions (terminated)
    if (instruction_index >= numInstructions) {
        // Set process state to TERMINATED
        physicalMemory[table_address + pcb_offset + 1] = TERMINATED;
        physicalMemory[table_address + pcb_offset + 2] = instruction_index;

        // Record termination time and calculate execution time
        exitTimeStamps[process.processID] = systemTimer;
        int execTime = exitTimeStamps[process.processID] - entryTimeStamps[process.processID];

        // Print PCB information - using logical addresses for PC, InstructionBase, and DataBase
        cout << "Process ID: " << process.processID << "\n";
        cout << "State: TERMINATED\n";
        cout << "Program Counter: " << process.instruction_start_base - 1  << "\n";  // Logical address
        cout << "Instruction Base: " << process.instruction_start_base << "\n";  // Logical address
        cout << "Data Base: " << process.datalogical_base << "\n";  // Logical address
        cout << "Memory Limit: " << process.totalMemoryGiven << "\n";
        cout << "CPU Cycles Used: " << process.cpuCyclesUsed << "\n";
        cout << "Register Value: " << process.register_value << "\n";
        cout << "Max Memory Needed: " << process.processMemory << "\n";
        cout << "Main Memory Base: " << process.mainMemoryBase << "\n";
        cout << "Total CPU Cycles Consumed: " << execTime << "\n";

        // Print termination summary
        cout << "Process " << process.processID << " terminated. Entered running state at: "
             << entryTimeStamps[process.processID] << ". Terminated at: "
             << exitTimeStamps[process.processID] << ". Total Execution Time: "
             << execTime << ".\n";

        // Release memory segments
        releaseMainMemory(memoryList, process.processID, physicalMemory);

        // Try to load more jobs
        job_loading_to_memory(newJobQueue, readyQueue, physicalMemory, memoryList);

        // Check I/O waiting queue
        io_waiting_queue(ioWaitingQueue, readyQueue, physicalMemory);
    } else {
        // Process still has instructions but reached time quantum
        physicalMemory[table_address + pcb_offset + 1] = READY;  // Set state to READY
        physicalMemory[table_address + pcb_offset + 2] = instruction_index;  // Save program counter
        readyQueue.push(table_address);         // Put back in ready queue

        // Check if any I/O waiting processes are ready
        io_waiting_queue(ioWaitingQueue, readyQueue, physicalMemory);
    }
}

void displayMainMemory(const std::vector<int>& mainMemory) {
    for (int i = 0; i < mainMemory.size(); i++) {
        std::cout << i << ": " << mainMemory[i] << "\n";  // Use \n instead of endl for better performance
    }
}


int main() {
    int maxMemory, numProcesses;
    queue<ProcessCB> newJobQueue;         // Processes waiting to be loaded
    queue<int> readyQueue;          // Processes ready to execute (stores segment table address)
    queue<IOtask> ioWaitingQueue;  // Processes waiting for I/O completion

    // Initialize TLB cache
    initTLB();

    // Read simulation parameters
    cin >> maxMemory >> timeSlice >> switch_time;
    cin >> numProcesses;

    // Initialize main memory array and memory block list
    vector<int> physicalMemory(maxMemory, -1);  // Initialize all memory to -1 (unused)
    list<MemoryBlockList> memoryList;           // Linked list to track memory blocks

    // Start with a single free block covering all memory
    MemoryBlockList firstBlock{-1, 0, maxMemory};  // processID -1 means free
    memoryList.push_back(firstBlock);

    // Read process definitions
    for (int i = 0; i < numProcesses; i++) {
        ProcessCB process;
        int processID, memoryNeeded, numInstructions;

        cin >> processID >> memoryNeeded >> numInstructions;

        // Initialize PCB fields
        process.processID = processID;
        process.state = NEW;
        process.counter = 0;
        process.totalMemoryGiven = memoryNeeded;
        process.processMemory = memoryNeeded;
        process.numOfInstruction = numInstructions;
        process.cpuCyclesUsed = 0;
        process.register_value = 0;
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
    job_loading_to_memory(newJobQueue, readyQueue, physicalMemory, memoryList);

    displayMainMemory(physicalMemory);

    // Main simulation loop
    while (!readyQueue.empty() || !ioWaitingQueue.empty() || !newJobQueue.empty()) {
        // Step 1: Check if any I/O waiting processes are ready
        io_waiting_queue(ioWaitingQueue, readyQueue, physicalMemory);

        // Step 2: Try to load more jobs if ready queue is empty
        if (readyQueue.empty() && !newJobQueue.empty()) {
            job_loading_to_memory(newJobQueue, readyQueue, physicalMemory, memoryList);
        }

        // Step 3: Execute process from ready queue if available
        if (!readyQueue.empty()) {
            int segmentTableAddress = readyQueue.front();
            readyQueue.pop();

            // Execute process - note we pass segment table address now
            cpuExecution(segmentTableAddress, physicalMemory, readyQueue,
                       ioWaitingQueue, memoryList, newJobQueue);
        }
            // Step 4: If only I/O processes exist, advance clock
        else if (!ioWaitingQueue.empty()) {
            systemTimer += switch_time;
            io_waiting_queue(ioWaitingQueue, readyQueue, physicalMemory);
        }
            // Step 5: If only waiting jobs exist, try final coalescing
        else if (!newJobQueue.empty()) {
            // Last attempt to coalesce memory and load jobs
            if (merge_free_adjacent(memoryList)) {
                job_loading_to_memory(newJobQueue, readyQueue, physicalMemory, memoryList);

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
    systemTimer += switch_time;

    // Print final statistics
    cout << "Total CPU time used: " << systemTimer << "." << endl;

    return 0;
}