#include <pthread.h>
#include <iostream>
#include <cmath>
#include <vector>

using namespace std;

struct ThreadArg {
	pthread_t thread;
	int index;
	int level;

	int position;

	// thread type
	bool isLeaf;

	// for leaf threads - array chunk information
	int startIndex;
	int endIndex;


	// computation phase synchronization
	pthread_mutex_t compute_mutex;
	pthread_cond_t compute_cond;
	bool compute_ready;

	// Termination phase synchronization
	pthread_mutex_t terminate_mutex;
	pthread_cond_t terminate_cond;
	bool terminate_ready;

	// Parent-child relationships (optional but helpful)
	int parentIndex;            // Index of parent thread (-1 for root)
	int leftChildIndex;         // Index of left child (-1 if none)
	int rightChildIndex;        // Index of right child (-1 if none)
};

void initThreadArgs(ThreadArg * args, int totalThreads, int height,int *inputArray, int *results,int arraySize) {

}


int main() {
	// Step 1: Read inputs
	int H, M;
	cin >> H >> M;
	// Step 2: Compute tree parameters

	int N = pow(2,H-1);
	int M_prime = M;

	if(M % N != 0) {
		M_prime =  M + (N - (M % N));
	}

	int chunkSize = M_prime/N;
	int totalThreads = pow(2,H) -1;

	// Step 3: Allocate arrays
	std::vector<ThreadArg> threadArgs(totalThreads);
	std::vector<int> results(totalThreads, 0);
	std::vector<int> input(M_prime, 0);

	// Read input array
	std::cout << "Enter " << M << " integers:" << std::endl;
	for (int i = 0; i < M; i++) {
		std::cin >> input[i];
	}


	// Step 4: Initialize each thread's data
	// - For each index i in 0 to totalThreads - 1:
	// - Compute level and position
	// - Assign index, level, position
	// - Assign array, results pointers
	// - For leaf threads:
	// - Set isLeaf = true
	// - Assign start and end indices in input array
	// - Initialize mutexes and condition variables
	// Step 5: Create all threads (loop)
	// - For each index i, call pthread_create(&threadArgs[i].thread, ...,
	//computeSum, &threadArgs[i])
	// Step 6: Trigger leaf threads to begin computation
	// - For each leaf thread (index from 2^(H-1)-1 to 2^H-2):
	// - Lock compute_mutex
	// - Set compute_ready = true
	// - Signal compute_cond
	// - Unlock compute_mutex
	// Step 7: Wait for root thread to finish computation
	// - Wait on a condition variable OR
	// - Use polling to detect when result[0] is ready
	// Step 8: Trigger root thread to begin termination
	// - Lock terminate_mutex for root
	// - Set terminate_ready = true
	// - Signal terminate_cond
	// - Unlock terminate_mutex
	// Step 9: Join all threads
	// - For all threads in threadArgs, call pthread_join
	// Step 10: Destroy mutexes and condition variables
	// - For each threadArg, destroy compute/terminate mutexes and conds
	return 0;
}