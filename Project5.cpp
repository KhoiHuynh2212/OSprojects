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

	// Pointers to shared data
	vector<int>* input_array;
	vector<int>* results;

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


void* computeSum(void* arg);


int main() {
	// Step 1: Read inputs
	int H, M;
	cin >> H >> M;

	// Step 2: Compute tree parameters
	vector<int>inputArray(M);
	for (int i = 0; i < M; i++) {
		cin >> inputArray[i];
	}

	int N = 1 << (H - 1);
	int padding = (N - (M % N)) % N;
	int m_Prime = padding + M;
	if(M % N != 0) {
		inputArray.resize(m_Prime,0); // pad input array with zeros
	}

	int chunkSize = m_Prime / N;
	int totalThreads = (1 << H) - 1;

	// Step 3: Allocate arrays
	vector<ThreadArg> threadArgs(totalThreads);
	vector<int> results(totalThreads, 0);
	// Step 4: Initialize each thread's data
	for(int i = 0; i < totalThreads - 1; ++i) {
		ThreadArg &t = threadArgs[i];

		// Compute level and position
		t.level = static_cast<int>(std::floor(std::log2(i + 1)));
		int firstAtLevel = (1 << t.level) - 1;
		t.position = i - firstAtLevel;

        // Set up pointers to shared data
		t.input_array = &inputArray;
		t.results = &results;

		t.isLeaf = (t.level == H - 1);

		if(t.isLeaf) {
			t.startIndex = t.position * chunkSize;
			t.endIndex = t.startIndex + chunkSize;
		}

		// 6) init compute‑phase sync
		t.compute_ready = false;
		pthread_mutex_init(&t.compute_mutex, nullptr);
		pthread_cond_init (&t.compute_cond,  nullptr);

		// 7) init terminate‑phase sync
		t.terminate_ready = false;
		pthread_mutex_init(&t.terminate_mutex, nullptr);
		pthread_cond_init (&t.terminate_cond,  nullptr);

		// 8) parent & children
		t.parentIndex = (i == 0 ? -1 : (i - 1) / 2);
		int left  = 2 * i + 1;
		int right = 2 * i + 2;
		t.leftChildIndex  = (left  < totalThreads ? left  : -1);
		t.rightChildIndex = (right < totalThreads ? right : -1);
	}



	// Step 5: Create all threads (loop)
	for(int i = 0; i < totalThreads;i++) {
		pthread_create( &threadArgs[i].thread,
						nullptr,
						computeSum,              // your worker
						&threadArgs[i]);
	}

	// Step 6: Trigger leaf threads to begin computation
	int firstLeaf = (1 << (H-1)) - 1;
	int lastLeaf  = (1 << H) - 2;

	for (int i = firstLeaf; i <= lastLeaf; ++i) {
		ThreadArg &leaf = threadArgs[i];
		pthread_mutex_lock(&leaf.compute_mutex);
		leaf.compute_ready = true;
		pthread_cond_signal(&leaf.compute_cond);
		pthread_mutex_unlock(&leaf.compute_mutex);
	}
	// Step 7: Wait for root thread to finish computation
	{
		ThreadArg &root = threadArgs[0];
		pthread_mutex_lock(&root.compute_mutex);
		while (!root.compute_ready) {
			pthread_cond_wait(&root.compute_cond, &root.compute_mutex);
		}
		pthread_mutex_unlock(&root.compute_mutex);
	}
	// Step 8: Trigger root thread to begin termination
	{
		ThreadArg &root = threadArgs[0];
		pthread_mutex_lock(&root.terminate_mutex);
		root.terminate_ready = true;
		pthread_cond_signal(&root.terminate_cond);
		pthread_mutex_unlock(&root.terminate_mutex);
	}
	// Step 9: Join all threads
	// - For all threads in threadArgs, call pthread_join

	for (int i = 0; i < totalThreads; ++i) {
		pthread_join(threadArgs[i].thread, nullptr);
	}
	// Step 10: Destroy mutexes and condition variables
	// - For each threadArg, destroy compute/terminate mutexes and conds.
	for (int i = 0; i < totalThreads; ++i) {
		ThreadArg &t = threadArgs[i];
		pthread_mutex_destroy(&t.compute_mutex);
		pthread_cond_destroy (&t.compute_cond);
		pthread_mutex_destroy(&t.terminate_mutex);
		pthread_cond_destroy (&t.terminate_cond);
	}

    // Finally, print your result
	std::cout << "Total sum = " << results[0] << std::endl;
	return 0;
}