#include <iostream>
#include <vector>
#include <cmath>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <string>
#include <sstream>

struct ProcessInfo {
	int index;          // Index in the tree
	int level;          // Level in the tree (root is level 1)
	int position;       // Position from left in the level
	pid_t pid;          // Process ID
	bool isLeaf;        // Whether this is a leaf process
	int startIdx;       // Start index of chunk (for leaf processes)
	int endIdx;         // End index of chunk (for leaf processes)
	int leftChildIndex; // Index of left child
	int rightChildIndex;// Index of right child
	int pipe_to_parent[2]; // Pipe to communicate with parent
	int terminate_pipe[2]; // Pipe for termination signal
};

// Calculate the tree parameters for a process at the given index
void calculateProcessParameters(ProcessInfo& proc) {
	proc.level = static_cast<int>(std::floor(std::log2(proc.index + 1))) + 1;
	proc.position = proc.index - (static_cast<int>(std::pow(2, proc.level - 1)) - 1);

	// Calculate child indices
	proc.leftChildIndex = 2 * proc.index + 1;
	proc.rightChildIndex = 2 * proc.index + 2;
}

// Function to create pipes for a process and its children
void createPipes(std::vector<ProcessInfo>& processes, int index) {
	// Create pipe for communication with parent
	if (pipe(processes[index].pipe_to_parent) == -1) {
		perror("pipe creation failed");
		exit(EXIT_FAILURE);
	}

	// Create pipe for termination signaling
	if (pipe(processes[index].terminate_pipe) == -1) {
		perror("termination pipe creation failed");
		exit(EXIT_FAILURE);
	}
}

// Function to print output atomically from a process
void printAtomically(const std::string& message) {
	// Write the entire message in a single write call to avoid interleaving
	write(STDOUT_FILENO, message.c_str(), message.length());
}

// Function to execute leaf processes and handle computation
void executeLeafProcess(const ProcessInfo& proc, const std::vector<int>& array) {
	int sum = 0;

	// Introduce a small delay based on position to order output
	usleep(100 * proc.position);

	// Calculate the sum of the chunk
	for (int i = proc.startIdx; i <= proc.endIdx; i++) {
		sum += array[i];
	}

	// Create and print output
	std::stringstream ss;
	ss << "[PID " << getpid() << "] [Index " << proc.index << "] [Level "
	   << proc.level << ", Position " << proc.position << "] computed sum: "
	   << sum << "\n";
	printAtomically(ss.str());

	// Send result to parent
	write(proc.pipe_to_parent[1], &sum, sizeof(sum));
	close(proc.pipe_to_parent[1]);

	// Wait for termination signal
	int signal;
	read(proc.terminate_pipe[0], &signal, sizeof(signal));
	close(proc.terminate_pipe[0]);

	// Print termination message
	std::stringstream termSs;
	termSs << "[PID " << getpid() << "] [Index " << proc.index << "] terminated.\n";
	printAtomically(termSs.str());

	exit(0);
}

// Function to execute internal node processes
void executeInternalProcess(const ProcessInfo& proc, std::vector<ProcessInfo>& processes, int totalProcesses) {
	int leftChildIdx = proc.leftChildIndex;
	int rightChildIdx = proc.rightChildIndex;

	// Wait for results from children
	int leftSum = 0, rightSum = 0;

	// Read from left child if it exists
	if (leftChildIdx < totalProcesses) {
		read(processes[leftChildIdx].pipe_to_parent[0], &leftSum, sizeof(leftSum));
		close(processes[leftChildIdx].pipe_to_parent[0]);
	}

	// Read from right child if it exists
	if (rightChildIdx < totalProcesses) {
		read(processes[rightChildIdx].pipe_to_parent[0], &rightSum, sizeof(rightSum));
		close(processes[rightChildIdx].pipe_to_parent[0]);
	}

	// Delay output based on level to group by level
	usleep(500 * (4 - proc.level));  // Higher levels print later

	// Compute and print result
	int sum = leftSum + rightSum;
	std::stringstream ss;

	if (proc.index == 0) { // Root process
		ss << "[PID " << getpid() << "] [Index " << proc.index << "] [Level "
		   << proc.level << ", Position " << proc.position << "] recieved: "
		   << leftSum << ", " << rightSum << " -> Final sum: " << sum << "\n\n";
	} else { // Other internal processes
		ss << "[PID " << getpid() << "] [Index " << proc.index << "] [Level "
		   << proc.level << ", Position " << proc.position << "] received: "
		   << leftSum << ", " << rightSum << " -> sum: " << sum << "\n";
	}
	printAtomically(ss.str());

	// Send result to parent if not root
	if (proc.index != 0) {
		write(proc.pipe_to_parent[1], &sum, sizeof(sum));
		close(proc.pipe_to_parent[1]);
	}

	// Wait for termination signal if not root
	if (proc.index != 0) {
		int signal;
		read(proc.terminate_pipe[0], &signal, sizeof(signal));
		close(proc.terminate_pipe[0]);
	}

	// Allow some time for consistency in termination order
	usleep(100 * proc.index);

	// Signal right child to terminate first (to match sample output)
	if (rightChildIdx < totalProcesses) {
		int signal = 1;
		write(processes[rightChildIdx].terminate_pipe[1], &signal, sizeof(signal));
		close(processes[rightChildIdx].terminate_pipe[1]);
	}

	// Then signal left child
	if (leftChildIdx < totalProcesses) {
		int signal = 1;
		write(processes[leftChildIdx].terminate_pipe[1], &signal, sizeof(signal));
		close(processes[leftChildIdx].terminate_pipe[1]);
	}

	// Wait for children to terminate
	if (rightChildIdx < totalProcesses) {
		waitpid(processes[rightChildIdx].pid, NULL, 0);
	}

	if (leftChildIdx < totalProcesses) {
		waitpid(processes[leftChildIdx].pid, NULL, 0);
	}

	// Print termination message
	std::stringstream termSs;
	termSs << "[PID " << getpid() << "] [Index " << proc.index << "] terminated.\n";
	printAtomically(termSs.str());

	if (proc.index != 0) {
		exit(0);
	}
}

// Main process creation and execution function
void createAndRunProcesses(std::vector<ProcessInfo>& processes, const std::vector<int>& array, int totalProcesses) {
	for (int i = 0; i < totalProcesses; i++) {
		if (i == 0) {
			// Root process
			processes[i].pid = getpid();
		} else {
			pid_t pid = fork();
			if (pid == 0) {
				// Child process
				processes[i].pid = getpid();

				// Execute based on whether it's a leaf or internal node
				if (processes[i].isLeaf) {
					executeLeafProcess(processes[i], array);
				} else {
					executeInternalProcess(processes[i], processes, totalProcesses);
				}
				exit(0); // Safety exit
			} else {
				// Parent process
				processes[i].pid = pid;
			}
		}
	}

	// Root process continues here
	executeInternalProcess(processes[0], processes, totalProcesses);
}

int main() {
	int H, M;

	// Read inputs
	std::cin >> H >> M;

	// Compute tree parameters
	int numLeafProcesses = (1 << (H - 1));
	int totalProcesses =  (1 << H) - 1;

	// Calculate chunk size and padding
	int paddedSize = M;
	if (M % numLeafProcesses != 0) {
		paddedSize = (M / numLeafProcesses + 1) * numLeafProcesses;
	}
	int chunkSize = paddedSize / numLeafProcesses;

	// Read array elements
	std::vector<int> inputArray(paddedSize, 0); // Initialize with zeros for padding
	for (int i = 0; i < M; i++) {
		std::cin >> inputArray[i];
	}

	// Initialize process information
	std::vector<ProcessInfo> processes(totalProcesses);

	// Set up process parameters
	for (int i = 0; i < totalProcesses; i++) {
		processes[i].index = i;
		calculateProcessParameters(processes[i]);

		// Check if this is a leaf process
		processes[i].isLeaf = (processes[i].leftChildIndex >= totalProcesses);

		// If it's a leaf, calculate its chunk bounds
		if (processes[i].isLeaf) {
			int leafIndex = i - (numLeafProcesses - 1);
			processes[i].startIdx = leafIndex * chunkSize;
			processes[i].endIdx = std::min((leafIndex + 1) * chunkSize - 1, paddedSize - 1);
		}

		// Create pipes for each process
		createPipes(processes, i);
	}

	// Prevent race conditions by ensuring atomic writes
	setvbuf(stdout, NULL, _IONBF, 0);

	// Create and run processes
	createAndRunProcesses(processes, inputArray, totalProcesses);

	// Clean up remaining pipes (root process only)
	for (int i = 0; i < totalProcesses; i++) {
		if (processes[i].pipe_to_parent[0] >= 0) close(processes[i].pipe_to_parent[0]);
		if (processes[i].pipe_to_parent[1] >= 0) close(processes[i].pipe_to_parent[1]);
		if (processes[i].terminate_pipe[0] >= 0) close(processes[i].terminate_pipe[0]);
		if (processes[i].terminate_pipe[1] >= 0) close(processes[i].terminate_pipe[1]);
	}
	// Command to run
	// g++ CS3113_Bonus_Project.cpp -o CS3113_Bonus_Project
    //./CS3113_Bonus_Project <input1.txt
	return 0;
}