#include <iostream>
#include <map>
#include <string.h>
using namespace std;

#define NUM_BUCKETS 150061 

class Bucket {
public:
	int key;
	int value;
	Bucket* next;
};

class Hash {
	Bucket* buckets[NUM_BUCKETS];
	
	int hash(int a) {
	    a = (a ^ 61) ^ (a >> 16);
	    a = a + (a << 3);
	    a = a ^ (a >> 4);
	    a = a * 0x27d4eb2d;
	    a = a ^ (a >> 15);

	    return a % NUM_BUCKETS;
	}

public:
	Hash() {
		memset(&buckets, 0, sizeof(Bucket*) * NUM_BUCKETS);
	}

	int Find(int key) {
		Bucket* bucket = buckets[hash(key)];
		if(!bucket) {
			return -1;
		}
		else {
			while(bucket) {
				if(bucket->key == key) {
					return bucket->value;	
				}
				bucket = bucket->next;
			}
		}

		return -1;
	}

	void Insert(int key, int value) {
		const int hash_key = hash(key);
		Bucket* bucket = buckets[hash_key];
		if(!bucket) {
			bucket = new Bucket;
			bucket->key = key;
			bucket->value = value;
			bucket->next = NULL;	
			buckets[hash_key] = bucket;
		}
		else {
			while(bucket->next) {
				bucket = bucket->next;
			}
			Bucket* temp = new Bucket;
			temp->key = key;
			temp->value = value;
			temp->next = NULL;
			bucket->next = temp;
		}
	}		
};

int main() {
	long start = clock();

	Hash hash;
	for(int i = 0; i < 1000000; i++) {
		hash.Insert(i, i + 3);
	}

	for(int i = 0; i < 1000000; i++) {
		// cout << hash.Find(i) << ',';
		hash.Find(i);
	}
	// cout << endl;

	cout << "Time 0: " << (float)(clock() - start) / CLOCKS_PER_SEC << " second(s)." << endl;

	start = clock();

	map<int, int> values;
	for(int i = 0; i < 1000000; i++) {
		values.insert(pair<int, int>(i, i + 3));
	}	
	
	for(int i = 0; i < 1000000; i++) {
		values.find(i);
	}
	cout << "Time 1: " << (float)(clock() - start) / CLOCKS_PER_SEC << " second(s)." << endl;
}		
