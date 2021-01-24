#ifndef CPU_BST_H_
#define CPU_BST_H_

#include "hsa_BST_search.h"

node * construct_BST(int num_nodes, node *data);
void initialize_nodes(node *data, long long int num_nodes);
node * search_node(node *data, int key);
void print_inorder(node * leaf);
int isBST(node* root);
int count_node(node *root);
void multithreaded_search(node *root, int *keys, int key_array_size, int num_thread, node **found_keys);
node * insert_and_balance(node *leaf, node *new_node);

#endif
