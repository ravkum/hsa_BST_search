/*******************************************************************************
Copyright ©2013 Advanced Micro Devices, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1 Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
2 Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/**
********************************************************************************
* @file <cpu_BST.cpp>
*
* @brief This file contains functions for creating bst in cpu.
*
********************************************************************************
*/

#include <stdio.h>
#include <process.h>
#include <windows.h>
#include "cpu_bst.h"

#define MULTITHREAD


static int iterative_insert(node **root, node *new_node)
{
	node *tmp = NULL;
	int key;
	key = new_node->value;

	if (!(*root)) {
		*root = new_node;
		return 0;
	}

	tmp = *root;

	while (1) {
		if (key < tmp->value) {
			if (tmp->left == NULL) {
				tmp->left = new_node;
				break;
			}
			else {
				tmp = tmp->left;
			}
		}

		else {
			if (tmp->right == NULL) {
				tmp->right = new_node;
				break;
			}
			else {
				tmp = tmp->right;
			}
		}
	}	

	return 0;
}

static void recursive_insert(node **root, node *new_node)
{
	int key = new_node->value;

	if(!(*root)) {
		*root = new_node;
		return;
	}
	else if(key < (*root)->value) {
		recursive_insert(&(*root)->left, new_node);
	}
	else if(key >= (*root)->value) {
		recursive_insert(&(*root)->right, new_node);
	}

	return;
}

node * construct_BST(int num_nodes, node *data)
{
	node *root = NULL;

	/* Inserting nodes into tree */
	for (int i = 0; i < num_nodes; i++)
	{
		iterative_insert(&root, &(data[i]));
		//root = insert_and_balance(root, &(data[i]));
	}

	return root;
}

void initialize_nodes(node *data, long long int num_nodes)
{
	int random_num;
	node *tmp_node;

	for (int i = 0; i < num_nodes; i++)
	{
		tmp_node = &(data[i]);

		random_num = rand();
		tmp_node->value = random_num;
		tmp_node->left = NULL;
		tmp_node->right = NULL;
		tmp_node->height = 1;
	}
}


void print_inorder(node * leaf) 
{
	if (leaf) {
		print_inorder(leaf->left);
		printf("%d ",leaf->value);
		print_inorder(leaf->right);
	}
}

static int node_count = 0;
/* Returns true if the given tree is a BST and its */
static int isBSTUtil(node* node, int min, int max) 
{ 
	if (node==NULL) 
		return 1;

	if (node->value < min || node->value > max) 
		return 0; 

	return isBSTUtil(node->left, min, node->value) && 
			isBSTUtil(node->right, node->value, max);  
}

/* Returns true if a binary tree is a binary search tree */
int isBST(node* node) 
{ 
	return isBSTUtil(node, INT_MIN, INT_MAX); 
} 

int count_node(node *root)
{
	int count = -1;
	if (root)
		count = 1;

	if (root->left)
		count += count_node(root->left);

	if (root->right)
		count += count_node(root->right);

	return count;
}

// Search for an element in the queue
node *search_node(node *root, int key)
{
	node *tmp_node = root;
	
	while (1) {
		if (!tmp_node || (tmp_node->value == key))
			break;

		tmp_node = (key < tmp_node->value) ? tmp_node->left : tmp_node->right;
	}

	return tmp_node;
}

typedef struct _thread_arg
{
	int thread_id;
	node *root;
	int *keys;
	int search_per_keys;
	node **found_keys;
} thread_arg;

unsigned int __stdcall multithread_search(void *arg)
{
	thread_arg *targ = (thread_arg *)arg;

	if (!targ || targ->thread_id < 0)
		_endthread();

	int thread_id = targ->thread_id;
	int init_id = thread_id * targ->search_per_keys;

	for (int i = init_id; i < init_id + targ->search_per_keys; i++) {
		targ->found_keys[i] = search_node(targ->root, targ->keys[i]);
	}

	_endthreadex(0);

	return 0;
}

void multithreaded_search(node *root, int *keys, int key_array_size, int num_thread, node **found_keys)
{
	HANDLE *hthread = (HANDLE *) malloc(sizeof(HANDLE) * num_thread);
	if (hthread == NULL) {
		printf("Error allocating memory for hthread.\n");
		exit(1);
	}

	thread_arg *tmp; 
	
	if ((tmp = (thread_arg *)malloc(sizeof(thread_arg) * num_thread)) == NULL) {
		printf("error allocating memory for thrad_arg.\n");
		exit(1);
	}		
	
	for (int i = 0; i < num_thread; i++) {
		tmp[i].thread_id = i;
		tmp[i].root = root;
		tmp[i].keys = keys;
		tmp[i].search_per_keys = (key_array_size / num_thread);
		tmp[i].found_keys = found_keys;
		
		//hthread[i] = (HANDLE) _beginthread(multithread_search, 0, &tmp[i]);
		hthread[i] = (HANDLE) _beginthreadex(NULL, 0, multithread_search, &tmp[i], 0, NULL);
		if (hthread[i] == 0) {
			printf("Error creating thread. Error is: %s\n", strerror(errno));
			exit(1);
		}
	}

	WaitForMultipleObjects(num_thread, hthread, TRUE, INFINITE);

	for (int i = 0; i < num_thread; i++)
		CloseHandle(hthread[i]);

	if (hthread)
		free(hthread);

	if (tmp)
		free(tmp);
}


// A utility function to get maximum of two integers
int max_val(int a, int b)
{
    return (a > b) ? a : b;
}

// Find the height of the tree
int height(node *t)
{
    if (t == NULL)
    {
        return 0;
    }
     return t->height;
}

// Get Balance factor of node N
int getBalance(node *N)
{
    if (N == NULL)
        return 0;
    return height(N->left) - height(N->right);
}

// A utility function to right rotate subtree rooted with y
node *rightRotate(node *y)
{
    node *x = y->left;
    node *T2 = x->right;
 
    // Perform rotation
    x->right = y;
    y->left = T2;
 
    // Update heights
    y->height = max_val(height(y->left), height(y->right))+1;
    x->height = max_val(height(x->left), height(x->right))+1;
 
    // Return new root
    return x;
}
 
// A utility function to left rotate subtree rooted with x
node *leftRotate(node *x)
{
    node *y = x->right;
    node *T2 = y->left;
 
    // Perform rotation
    y->left = x;
    x->right = T2;
 
    //  Update heights
    x->height = max_val(height(x->left), height(x->right))+1;
    y->height = max_val(height(y->left), height(y->right))+1;
 
    // Return new root
    return y;
}

// Insert an element in the queue as per BST logic
// After insertion, only nodes that are on the path from the insertion point
// to the root might have their balance altered because only those nodes have their subtrees altered. 
// As we follow up the path and update the balancing information, we may find a node whose new balance voilates the AVL (balance) condition.
// We need to rebalance the tree to satisfy AVL property. 
node * insert_and_balance(node *leaf, node *new_node)
{
    // Perform BST
    // If the node does not exist, then create a new node and insert the
    // element into the node.
	
	int key = new_node->value;

    if( leaf == NULL )
    {
        return(new_node);
    }

    if(key < leaf->value)
    {
        leaf->left = insert_and_balance(leaf->left, new_node);
    }
    else
    {
        leaf->right = insert_and_balance(leaf->right, new_node);
    }

    /* Update height of this ancestor node */
    leaf->height = max_val(height(leaf->left), height(leaf->right)) + 1;

    ////////////////////////////////////////////////
    // Balance the tree in case it is not balanced
    ////////////////////////////////////////////////
    /* Get the balance factor of this ancestor node to check whether
       this node became unbalanced */
    int balance = getBalance(leaf);
 
    // If this node becomes unbalanced, then there are 4 cases
    // Left Left Case
    if (balance > 1 && key < leaf->left->value)
        return rightRotate(leaf);
 
    // Right Right Case
    if (balance < -1 && key >= leaf->right->value)
        return leftRotate(leaf);
 
    // Left Right Case
    if (balance > 1 && key >= leaf->left->value)
    {
        leaf->left =  leftRotate(leaf->left);
        return rightRotate(leaf);
    }
 
    // Right Left Case
    if (balance < -1 && key < leaf->right->value)
    {
        leaf->right = rightRotate(leaf->right);
        return leftRotate(leaf);
    }

    /* return the (unchanged) node pointer */
    return leaf;
}
