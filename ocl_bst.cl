#include "ocl_BST_search.h"

/*
 * This kernel searched a set of nodes on an BST.
 * Arguments:
 *		1. root node of the BST.
 *		2. An array of keys to be searched.
 *		3. An array of nodes pointers found in the searech.
 */

__kernel void ocl_search(
			__global ocl_node *tree,
			int root_id,
			__global int *search_keys,
			int num_search_keys,
			__global int *found_nodes_id) 
{
	int tmp_node_id; 
	
	int gid = get_global_id(0);
	int nodes_per_wi = (num_search_keys / get_global_size(0));
	int init_id = gid * nodes_per_wi;
	int i, key;

	for (i = init_id; i <= init_id + nodes_per_wi; i++) {
		key = search_keys[i];
	
		tmp_node_id = root_id;
	
		while (1) {
			if ((tmp_node_id == -1) || (tree[tmp_node_id].value == key))
				break;

			tmp_node_id = (key < tree[tmp_node_id].value) ? tree[tmp_node_id].left : tree[tmp_node_id].right;
		}
	
		found_nodes_id[i] = tmp_node_id;
	}
}

