#include <pthread.h>
#include <iostream>
#include <cmath>
#include <vector>
#include <stdexcept>

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
	ThreadArg* all_threads;  // Base pointer to thread array for easier access

	// computation phase synchronization
	pthread_mutex_t compute_mutex;
	pthread_cond_t compute_cond;
	bool compute_ready;

	// Termination phase synchronization
	pthread_mutex_t terminate_mutex;
	pthread_cond_t terminate_cond;
	bool terminate_ready;

	// Parent-child relationships
	int parentIndex;            // Index of parent thread (-1 for root)
	int leftChildIndex;         // Index of left child (-1 if none)
	int rightChildIndex;        // Index of right child (-1 if none)
};

pthread_mutex_t cout_mutex = PTHREAD_MUTEX_INITIALIZER;
void* computeSum(void* arg) {
	auto* t = static_cast<ThreadArg *>(arg);
	ThreadArg* base_threads = t->all_threads;  // Get base array pointer

	// wait until signaled to start computation
	pthread_mutex_lock(&t->compute_mutex);
	while (!t->compute_ready) {
		pthread_cond_wait(&t->compute_cond, &t->compute_mutex);
	}
	pthread_mutex_unlock(&t->compute_mutex);

	// PHASE 1: Computation
	// if leaf node, compute local sum
	if (t->isLeaf) {
		int sum = 0;
		for (int i = t->startIndex; i <= t->endIndex; i++) {
			sum += (*t->input_array)[i];
		}
		(*t->results)[t->index] = sum;

		// Print computation information
		// For leaf thread output
		pthread_mutex_lock(&cout_mutex);
		cout << "[Thread Index " << t->index << "] [Level " << t->level
			 << ", Position " << t->position << "] [TID " << pthread_self()
			 << "] computed leaf sum: " << sum << endl;
		pthread_mutex_unlock(&cout_mutex);

		if (t->parentIndex >= 0) {
			ThreadArg* parent = &base_threads[t->parentIndex];
			pthread_mutex_lock(&parent->compute_mutex);
			parent->compute_ready = true;
			pthread_cond_signal(&parent->compute_cond);
			pthread_mutex_unlock(&parent->compute_mutex);
		}
	} else {
		if (t->leftChildIndex >= 0) {
			ThreadArg* leftChild = &base_threads[t->leftChildIndex];
			pthread_mutex_lock(&leftChild->compute_mutex);
			leftChild->compute_ready = true;
			pthread_cond_signal(&leftChild->compute_cond);
			pthread_mutex_unlock(&leftChild->compute_mutex);
		}

		if (t->rightChildIndex >= 0) {
			ThreadArg* rightChild = &base_threads[t->rightChildIndex];
			pthread_mutex_lock(&rightChild->compute_mutex);
			rightChild->compute_ready = true;
			pthread_cond_signal(&rightChild->compute_cond);
			pthread_mutex_unlock(&rightChild->compute_mutex);
		}

		// get the results from both children
		int leftResult = (*t->results)[t->leftChildIndex];
		int rightResult = (*t->results)[t->rightChildIndex];

		// calculate sum
		int sum = leftResult + rightResult;
		(*t->results)[t->index] = sum;

		// Print information - exactly matching your original format
		pthread_mutex_lock(&cout_mutex);
		cout << "[Thread Index " << t->index << "] [Level " << t->level
			 << ", Position " << t->position << "] [TID " << pthread_self()
			 << "] received: " << endl;
		pthread_mutex_unlock(&cout_mutex);

		ThreadArg* leftChild = &base_threads[t->leftChildIndex];
		ThreadArg* rightChild = &base_threads[t->rightChildIndex];

		cout << "Left Child [Index " << t->leftChildIndex << ", Level "
		   << leftChild->level << ", Position " << leftChild->position
		   << ", TID " << leftChild->thread << "]: " << leftResult << endl;

		cout << "Right Child [Index " << t->rightChildIndex << ", Level "
			 << rightChild->level << ", Position " << rightChild->position
			 << ", TID " << rightChild->thread << "]: " << rightResult << endl;


		// Replace your current sum printing with:
		if (t->index == 0) {
			cout << "[Thread Index " << t->index << "] computed final sum: " << sum << endl;
			cout << endl;
			cout << "[Thread Index " << t->index << "] now initiates tree cleanup." << endl;
			cout << endl;
		} else {
			cout << "[Thread Index " << t->index << "] computed sum: " << sum << endl;
		}

		// Signal parent that computation is complete
		if (t->parentIndex >= 0) {
			ThreadArg* parent = &base_threads[t->parentIndex];
			pthread_mutex_lock(&parent->compute_mutex);
			parent->compute_ready = true;
			pthread_cond_signal(&parent->compute_cond);
			pthread_mutex_unlock(&parent->compute_mutex);
		}
	}

	// Phase 2 : Termination
	pthread_mutex_lock(&t->terminate_mutex);
	while (!t->terminate_ready) {
		pthread_cond_wait(&t->terminate_cond, &t->terminate_mutex);
	}
	pthread_mutex_unlock(&t->terminate_mutex);

	if (!t->isLeaf) {
		// First terminate left child
		if (t->leftChildIndex >= 0) {
			ThreadArg* leftChild = &base_threads[t->leftChildIndex];
			pthread_mutex_lock(&leftChild->terminate_mutex);
			leftChild->terminate_ready = true;
			pthread_cond_signal(&leftChild->terminate_cond);
			pthread_mutex_unlock(&leftChild->terminate_mutex);

			// Wait for left child to terminate
			pthread_join(leftChild->thread, nullptr);
		}

		// Print root termination message
		pthread_mutex_lock(&cout_mutex);
		cout << "[Thread Index " << t->index << "] [Level " << t->level
			 << ", Position " << t->position << "] terminated." << endl;
		pthread_mutex_unlock(&cout_mutex);

		// Then terminate right child
		if (t->rightChildIndex >= 0) {
			ThreadArg* rightChild = &base_threads[t->rightChildIndex];
			pthread_mutex_lock(&rightChild->terminate_mutex);
			rightChild->terminate_ready = true;
			pthread_cond_signal(&rightChild->terminate_cond);
			pthread_mutex_unlock(&rightChild->terminate_mutex);

			// Wait for right child to terminate
			pthread_join(rightChild->thread, nullptr);
		}

		// We've already printed the termination message, so return
		return nullptr;
	} else {
		// For leaf nodes, just print termination message
		pthread_mutex_lock(&cout_mutex);
		cout << "[Thread Index " << t->index << "] [Level " << t->level
			 << ", Position " << t->position << "] terminated." << endl;
		pthread_mutex_unlock(&cout_mutex);
	}

	return nullptr;
}


int main() {
	// Step 1: Read inputs
	int H, M;
	cin >> H >> M;

	// Step 2: Compute tree parameters
	vector<int> inputArray(M);
	for (int i = 0; i < M; i++) {
		cin >> inputArray[i];
	}

	int N = 1 << (H - 1);
	int padding = (N - (M % N)) % N;
	int m_Prime = padding + M;
	if(M % N != 0) {
		inputArray.resize(m_Prime, 0); // pad input array with zeros
	}

	int chunkSize = m_Prime / N;
	int totalThreads = (1 << H) - 1;

	// Step 3: Allocate arrays
	vector<ThreadArg> threadArgs(totalThreads);
	vector<int> results(totalThreads, 0);

	// Step 4: Initialize each thread's data
	for(int i = 0; i < totalThreads; ++i) {  // Fixed: initialize ALL threads
		ThreadArg &t = threadArgs[i];
		t.index = i;

		// Compute level and position
		t.level = static_cast<int>(std::floor(std::log2(i + 1))) + 1;  // Add 1 to match spec
		int firstAtLevel = (1 << (t.level - 1)) - 1;
		t.position = i - firstAtLevel;

        // Set up pointers to shared data
		t.input_array = &inputArray;
		t.results = &results;
		t.all_threads = threadArgs.data();  // Store base pointer to thread array

		t.isLeaf = (t.level == H);

		if(t.isLeaf) {
			int leafIndex = i - ((1 << (H-1)) - 1); // Convert thread index to leaf position (j)
			t.startIndex = leafIndex * chunkSize;
			t.endIndex = (leafIndex + 1) * chunkSize - 1;
			// Special handling for uneven distribution
			if (leafIndex == 0) {
				// First leaf gets ceiling(M/N) elements
				int firstChunkSize = (M + N - 1) / N;  // Ceiling division
				t.startIndex = 0;
				t.endIndex = firstChunkSize - 1;
			} else {
				// Other leaves divide the remaining elements
				int firstChunkSize = (M + N - 1) / N;  // Ceiling division
				t.startIndex = firstChunkSize;
				t.endIndex = M - 1;
			}
		}

		// init compute‑phase sync
		t.compute_ready = false;
		pthread_mutex_init(&t.compute_mutex, nullptr);
		pthread_cond_init(&t.compute_cond, nullptr);

		// init terminate‑phase sync
		t.terminate_ready = false;
		pthread_mutex_init(&t.terminate_mutex, nullptr);
		pthread_cond_init(&t.terminate_cond, nullptr);

		// parent & children
		t.parentIndex = (i == 0 ? -1 : (i - 1) / 2);
		int left  = 2 * i + 1;
		int right = 2 * i + 2;
		t.leftChildIndex  = (left  < totalThreads ? left  : -1);
		t.rightChildIndex = (right < totalThreads ? right : -1);
	}

	// Step 5: Create all threads (loop)
	for(int i = 0; i < totalThreads; i++) {
		pthread_create(&threadArgs[i].thread,
						nullptr,
						computeSum,
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
	for (int i = 0; i < totalThreads; ++i) {
		pthread_join(threadArgs[i].thread, nullptr);
	}

	// Step 10: Destroy mutexes and condition variables
	for (int i = 0; i < totalThreads; ++i) {
		ThreadArg &t = threadArgs[i];
		pthread_mutex_destroy(&t.compute_mutex);
		pthread_cond_destroy(&t.compute_cond);
		pthread_mutex_destroy(&t.terminate_mutex);
		pthread_cond_destroy(&t.terminate_cond);
	}

	return 0;
}