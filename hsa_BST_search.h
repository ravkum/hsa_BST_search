#ifndef HSA_BST_SEARCH_H_
#define HSA_BST_SEARCH_H_


#ifndef SVM_DATA_STRUCT_OPENCL_DEVICE
#include <atomic>
#define __global 
#endif

typedef struct bin_tree
{
    int value;                  // Value at a node
	int height; 
	int found;
    __global struct bin_tree *left;      // Pointer to the left node
    __global struct bin_tree *right;     // Pointer to the right node
	__global struct bin_tree *parent;
} node;


#endif