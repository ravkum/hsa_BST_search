
#pragma OPENCL EXTENSION cl_amd_c11_atomics : enable
#define SVM_DATA_STRUCT_OPENCL_DEVICE

#include "hsa_BST_search.h"
#include "svm_data_struct.h"

/*
 * This kernel inserts a node on an BST.
 * Arguments:
 *		1. root node of the BST.
 *		2. An array of malloced and key initialized nodes.
 *		3. Number of nodes already added by the CPU in the BST.
 *		4. An array of mutex. (As many as there are threads)
 */

__kernel void bst_insert(
			__global void *root_parm,
			__global void *data_parm,
			__global int *n_cpu_nodes,
			__global svm_mutex *mutex) 
{
	__global svm_mutex *tmp_mutex;
	__global node *tmp_node, *tmp_parent, *new_node; 

	__global node *root = (__global node *)root_parm;
	__global node *data = (__global node *)data_parm;
	
	int i, flag, mutex_id;
	int data_init_id = (*n_cpu_nodes) + (get_group_id(0) * get_local_size(0));
	int lid = get_local_id(0);
	
	/* Search the parent node. 
	 * Multiple work-items in the a work-group run this part. */	
	flag = 0;
	tmp_node = root;
	tmp_parent = (void *)0;
	new_node = &(data[data_init_id + lid]);
	
	while (1) {
		if (!tmp_node) {
			new_node->parent = tmp_parent;
			break;
		}

		tmp_parent = tmp_node;
		flag = (new_node->value - tmp_node->value);
		tmp_node = (flag < 0) ? tmp_node->left : tmp_node->right;
	}
	
	barrier(CLK_GLOBAL_MEM_FENCE);

	/* Node insertion requires gpu-to-gpu mutex.
	 * So, One work-item inserts nodes for all the local work-items in the tree.
	 * work-items in different work-groups still work in parallel.
	 * A small traversal is needed again in the subtree as the 
	 * parent for subsequent nodes may change after the previous node insertions.
	 */
	if (lid == 0) {

		#pragma unroll
		for (i = 0; i < get_local_size(0); i++) {

			flag = 0;
			new_node = &(data[data_init_id + i]);
			tmp_node = new_node->parent;
			tmp_parent = tmp_node ? tmp_node->parent : (void *)0;
		
			while (1) {
				if (!tmp_node) {

					/* Mutex_id is dependednt on the tmp_parent so two global work-items 
					 * inserting node on the same parent do it serially. */
					mutex_id = ((int)tmp_parent / sizeof(node)) % (get_global_size(0));
					tmp_mutex = &(mutex[mutex_id]);
					svm_mutex_lock(tmp_mutex);

					if (!tmp_parent) { /*Empty tree */
						if (!root) { /* Still empty */
							root = new_node;
							svm_mutex_unlock(tmp_mutex);
							break;
						}
						else { /* not empty any more */
							tmp_node = root;
							svm_mutex_unlock(tmp_mutex);
						}
					}

					else if (flag < 0) { /* left child */
						if (!tmp_parent->left) {
							tmp_parent->left = new_node;
							svm_mutex_unlock(tmp_mutex);
							break;
						}
						else {
							tmp_node = tmp_parent->left;
							svm_mutex_unlock(tmp_mutex);
						}
					}

					else {
						if (!tmp_parent->right) {
							tmp_parent-> right = new_node;
							svm_mutex_unlock(tmp_mutex);
							break;
						}
						else {
							tmp_node = tmp_parent->right;
							svm_mutex_unlock(tmp_mutex);
						}
					}
				}

				/* A small traversal is needed again */
				tmp_parent = tmp_node;
				flag = (new_node->value - tmp_node->value);
				tmp_node = (flag < 0) ? tmp_node->left : tmp_node->right;
			}
		}
	}
}



/*
 * This kernel searched a set of nodes on an BST.
 * Arguments:
 *		1. root node of the BST.
 *		2. An array of keys to be searched.
 *		3. An array of nodes pointers found in the searech.
 */

__kernel void bst_search(
			__global void *root_parm,
			__global int *search_keys,
			__global int *n,
			__global void *found_nodes_parm) 
{
	__global node *tmp_node; 
	__global node *root = (__global node *)root_parm;
	//__global node **found_nodes = &found_nodes_parm;
	__global uintptr_t *found_nodes = found_nodes_parm;
	int num_search_keys = *(__global int *)n;
	
	int gid = get_global_id(0);
	int keys_per_wi = (num_search_keys / get_global_size(0));
	int init_id = gid * keys_per_wi;

	int i, key;

	for (i = 0; i < keys_per_wi; i++) {
		key = search_keys[init_id + i];
	
		tmp_node = root;
	
		while (1) {
			if (!tmp_node || (tmp_node->value == key))
				break;

			tmp_node = (key < tmp_node->value) ? tmp_node->left : tmp_node->right;
		}
	
		found_nodes[init_id + i] = tmp_node;
	}
}

