#ifndef OCL_BST_SEARCH_H_
#define OCL_BST_SEARCH_H_

typedef struct ocl_bin_tree
{
    int value;     // Value at a node
	int height;
	int found;
    int left;      // index to the left node
    int right;     // index to the right node
	int parent;
} ocl_node;

#endif