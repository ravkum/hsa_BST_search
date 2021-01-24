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
* @file <hsa_BST.cpp>
*
* @brief This file contains functions for initializing HSA CU.
* It creates a binary search tree in shared virtual memory and also 
* enqueues work to the CU for creating node and inserting in the same Binary Search Tree
*
* This shows SVM and atomics functionality of HSA.
********************************************************************************
*/

#include <stdlib.h>
#include <stdio.h>
#include <search.h>
#include <process.h>  
#include <windows.h>
#include "hsa_helper.h"
#include "hsa_BST_search.h"
#include "ocl_BST_search.h"
#include "cpu_BST.h"
#include "svm_data_struct.h"
#include "SDKUtil.hpp"
using namespace appsdk;

//#define USE_OCL_PATH

#define BUILD_LOG_SIZE 10240

static double time_spent;

static DeviceSVMFunctions dF;
static long long int num_nodes = 1024000;
static long long int num_search_keys;
static node *root = NULL;
static node *data = NULL;
static ocl_node *ocl_tree = NULL;

static node **found_key_nodes = NULL;
static int use_ocl = 0;
static svm_mutex *mutex = NULL;
static int *found_keys = NULL;
static int *found_nodes_id = NULL;

static int *search_keys = NULL;
static SDKTimer *sdk_timer = NULL;
static int timer; 
static int found_count = 0;

static int num_cpu_nodes = 256;
static long long int num_gpu_nodes; 
static long long int num_gpu_wi; 
static size_t globalSize;

static cl_context context;

static int compare(void *context, const void *num1, const void *num2)
{
	int val1 = *(const int *)num1;
	int val2 = *(const int *)num2;

	if (val1 < val2)
		return -1;

	else if (val1 == val2)
		return 0;

	else 
		return 1;
}

static void sort_search_array(int *array, int num_elem)
{
	qsort_s(array, num_elem, sizeof(int), compare, NULL);
	//printf("Using sorted serach keys...\n");
}


static void initialize_search_keys(int *search_keys, long long int num_search_keys)
{

	static int rand_seed_it = 0;
	if (rand_seed_it)
		srand(time(NULL));
	else
		srand(1);

	rand_seed_it = (rand_seed_it + 1) % 2;

	for (int i = 0; i < num_search_keys; i++)
	{
		search_keys[i] = rand();		
	}

	//sort_search_array(search_keys, num_search_keys);
}

static void initialize_mutex_array(svm_mutex *mutex, long long int n)
{
	for (int i = 0; i < n; i++) {
		svm_mutex_init(&mutex[i], SVM_MUTEX_UNLOCK);
	}
}

static int get_next_search_per_wi(int *val)
{
	printf("Enter 0 to continue and do cpu search. \nElse enter the next search keys per wi: ");

	scanf("%d", val);

	return *val;
}

static int get_next_num_cpu_threads(int *val) 
{
	printf("Enter 0 to exit. Else enter the number of cpu threads for search: ");

	scanf("%d", val);

	if (*val > 64) {
		printf("Currently only 64 threads are supported. Running with 64 threads.\n");
		*val = 64;
	}

	return *val;
}

static void construct_bst_tree()
{
	num_cpu_nodes = num_nodes;
	
	/* Construct initial BST in the cpu */
	root = construct_BST(num_cpu_nodes, data);

#if 0
	globalSize = (size_t)num_gpu_nodes;
	/* Gpu work enqueue */

	size_t local_preferredLocalSize = 1;

	status  = dF.clSetKernelArgSVMPointer(insert_kernel, 0, root);
	status |= dF.clSetKernelArgSVMPointer(insert_kernel, 1, data);
	status |= dF.clSetKernelArgSVMPointer(insert_kernel, 2, &num_cpu_nodes);
	status |= dF.clSetKernelArgSVMPointer(insert_kernel, 3, mutex);
	ASSERT_CL(status, "Error set insert_kernel arg.");

	status = clEnqueueNDRangeKernel(queue, insert_kernel, 1, NULL, &globalSize, &local_preferredLocalSize, 0, NULL, NULL);
	ASSERT_CL(status, "Error when enqueuing insert_kernel");

	clFinish(queue);
#endif
	
	if (!isBST(root)) {
		printf("error creating bst.\n");
		exit(1);
	}

	/**** Tree is created *****/

}

static void initialize_ocl_nodes(ocl_node *ocl_tree, long long int num_nodes)
{
	for (int i = 0; i < num_nodes; i++)
	{
		ocl_tree[i].left = -1;
		ocl_tree[i].right = -1;
	}

}


static void init_globals_and_create_tree()
{
	if (!use_ocl) {
		/* Data allocation and initialization */
		int flags = CL_MEM_READ_ONLY; // | CL_MEM_SVM_FINE_GRAIN_BUFFER_AMD;
		if ((data = (node *) dF.clSVMAlloc(context, flags, num_nodes * sizeof(node), 0)) == NULL) {
			printf("Error allocating memory for nodes.\n");
			exit(1);
		}
		initialize_nodes(data, num_nodes);

		//Used only by the insert_kernel.
		num_gpu_nodes = num_nodes - num_cpu_nodes;
		num_gpu_wi = num_gpu_nodes;
		if ((mutex = (svm_mutex *) dF.clSVMAlloc(context, flags, num_gpu_wi * sizeof(svm_mutex), 0)) == NULL) {
			printf("Error allocating memory for mutex.\n");
			exit(1);
		}
		initialize_mutex_array(mutex, num_gpu_wi);

		flags = CL_MEM_READ_ONLY; 
		if ((search_keys = (int *) dF.clSVMAlloc(context, flags, num_search_keys * sizeof(int), 0)) == NULL) {
			printf("Error allocating memory for search keys.\n");
			exit(1);
		}
		initialize_search_keys(search_keys, num_search_keys);

		flags = CL_MEM_WRITE_ONLY; 
		if ((found_key_nodes = (node **) dF.clSVMAlloc(context, flags, num_search_keys * sizeof(node *), 0)) == NULL) {
			printf("Error allocating memory for found keys.\n");
			exit(1);
		}

		memset(found_key_nodes, 0, num_search_keys * sizeof(node *));
	}
	else {
		/* Data allocation and initialization */
		if ((data = (node *)malloc(num_nodes * sizeof(node))) == NULL) {
			printf("Error allocating memory for nodes.\n");
			exit(1);
		}
		initialize_nodes(data, num_nodes);

		if ((search_keys = (int *)malloc(num_search_keys * sizeof(int))) == NULL) {
			printf("Error allocating memory for search keys.\n");
			exit(1);
		}
		initialize_search_keys(search_keys, num_search_keys);

		if ((found_keys = (int *)malloc(num_search_keys * sizeof(int))) == NULL) {
			printf("Error allocating memory for found keys.\n");
			exit(1);
		}

		if ((found_key_nodes = (node **) malloc(num_search_keys * sizeof(node *))) == NULL) {
			printf("Error allocating memory for found keys nodes.\n");
			exit(1);
		}

		memset(found_key_nodes, 0, num_search_keys * sizeof(node *));

		if ((ocl_tree = (ocl_node *)malloc(num_nodes * sizeof(ocl_node))) == NULL) {
			printf("Error allocating memory for nodes.\n");
			exit(1);
		}
		initialize_ocl_nodes(ocl_tree, num_nodes);

		memset(found_keys, 0, num_search_keys * sizeof(int));
	}


	construct_bst_tree();

}

static void update_found_nodes(node *root, node *tree, int *found_nodes_id, int num_search_keys)
{
#if 0
	node *tmp;
	for (int i = 0; i < num_search_keys; i++) {
		if (found_nodes_id[i] != -1) {
			tmp = tree + (found_nodes_id[i]);
			tmp->found = 1;
		}
	}
#endif
}

static void convert_tree_to_array(node *root, ocl_node *ocl_tree, int *root_id)
{

#if 0
	/* Method 1 : Uses the fact that the nodes are pre-malloced and they are in contigous memory.
	 * Will fail in case malloc couldnt return continous memory region.
	 */
	int id;

	for (int i = 0; i < num_nodes;	 i++) {
		ocl_tree[i].value = tree[i].value;
		ocl_tree[i].height = tree[i].height;
		ocl_tree[i].right  = (!tree[i].right) ? -1 : (int)(((uintptr_t)tree[i].right - (uintptr_t)tree) / sizeof(node));
		ocl_tree[i].left   = (!tree[i].left) ? -1 : (int)(((uintptr_t)tree[i].left - (uintptr_t)tree) / sizeof(node));
		ocl_tree[i].parent = -1;
	}

	*root_id = (!root) ? -1 : (int)((uintptr_t)root - (uintptr_t)tree);		
#endif
	node **tree_queue;
	node *tmp;

	if ((tree_queue = (node **)calloc(num_nodes, sizeof(node *))) == NULL) {
		printf("Error creating tree queue.\n");
		exit(1);
	}

	long long int front = 0;
	long long int rear  = 0;

	tree_queue[rear] = root;
	ocl_tree[rear].value = root->value;
	ocl_tree[rear].height = root->height; 
	ocl_tree[rear].parent = -1;
	rear++;

	*root_id = 0;

	while (front != rear) {
		tmp = tree_queue[front];
		if (!tmp)
			break;

		if (tmp->left) {
			tree_queue[rear] = tmp->left;
			ocl_tree[rear].value = tmp->left->value;
			ocl_tree[rear].height = tmp->left->height;
			ocl_tree[rear].parent = -1;
			ocl_tree[front].left = rear;
			rear++;
		}

		if (tmp->right) {
			tree_queue[rear] = tmp->right;
			ocl_tree[rear].value = tmp->right->value;
			ocl_tree[rear].height = tmp->right->height;
			ocl_tree[rear].parent = -1;
			ocl_tree[front].right = rear;
			rear++;
		}

		front++;
	}

	if (tree_queue)
		free(tree_queue);

}

static int count_ocl_nodes(ocl_node *ocl_tree, int id)
{
	int count = -1;
	if (id != -1)
		count = 1;

	if (ocl_tree[id].left != -1)
		count += count_ocl_nodes(ocl_tree, ocl_tree[id].left);

	if (ocl_tree[id].right != -1)
		count += count_ocl_nodes(ocl_tree, ocl_tree[id].right);

	return count;
}

static int verify_ocl_tree(ocl_node *ocl_tree, int num_nodes)
{
	return (num_nodes == count_ocl_nodes(ocl_tree, 0));
}

static void run_ocl_path(int iteration, int search_per_wi, size_t preferredLocalSize)
{
	/*Step1: Getting platforms and choose an available one.*/
	cl_uint numPlatforms;	//the NO. of platforms
	cl_platform_id platform = NULL;	//the chosen platform
	cl_int	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	if (status != CL_SUCCESS) {
		printf("Error: Getting platforms!");
		exit(1);
	}

	/*For clarity, choose the first available platform. */
	if(numPlatforms > 0) {
		cl_platform_id* platforms = (cl_platform_id* )malloc(numPlatforms* sizeof(cl_platform_id));
		status = clGetPlatformIDs(numPlatforms, platforms, NULL);
		platform = platforms[0];
		free(platforms);
	}

	/*Step 2:Query the platform and choose the first GPU device if has one.Otherwise use the CPU as device.*/
	cl_uint				numDevices = 0;
	cl_device_id        *devices;
	status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);	
	if (numDevices == 0) {	//no GPU available.
		printf("No GPU device available.");
		printf("Choose CPU as default device.");
		status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 0, NULL, &numDevices);	
		devices = (cl_device_id*)malloc(numDevices * sizeof(cl_device_id));
		status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, numDevices, devices, NULL);
	}
	else {
		devices = (cl_device_id*)malloc(numDevices * sizeof(cl_device_id));
		status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices, NULL);
	}

	/*Step 3: Create context.*/
	context = clCreateContext(NULL,1, devices,NULL,NULL,NULL);

	/*Step 4: Creating command queue associate with the context.*/
	cl_command_queue queue = clCreateCommandQueue(context, devices[0], 0, NULL);

	/*Step 5: Create program object */
	std::string kernelString = readCLFile("ocl_bst.cl");
	const char* kernelCString = kernelString.c_str();
	cl_program program = clCreateProgramWithSource(context, 1, &kernelCString, NULL, &status);
	ASSERT_CL(status, "Error when creating CL program");

	status = clBuildProgram(program, 1, &devices[0], "-I . ", NULL, NULL);
	if (status != CL_SUCCESS) {
		char buildLog[BUILD_LOG_SIZE];
		status = clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, BUILD_LOG_SIZE, buildLog, NULL);
		printf("Build log: %s\n", buildLog);
		ASSERT_CL(status, "Error when building CL program");
	}

	cl_kernel search_kernel = clCreateKernel(program, "ocl_search", &status);
	ASSERT_CL(status, "Error creating kernel.\n");

	cl_mem cl_ocl_tree = clCreateBuffer(context, CL_MEM_READ_ONLY, num_nodes * sizeof(ocl_node), NULL, &status);
	ASSERT_CL(status, "Error creating cl_ocl_tree\n");

	cl_mem cl_search_keys = clCreateBuffer(context, CL_MEM_READ_ONLY, num_search_keys * sizeof(int), NULL, &status);
	ASSERT_CL(status, "Error creating cl_search_keys\n");

	cl_mem cl_found_nodes_id = clCreateBuffer(context, CL_MEM_WRITE_ONLY, num_search_keys * sizeof(int), NULL, &status);
	ASSERT_CL(status, "Error creating cl_search_keys\n");

	init_globals_and_create_tree();

	/* Search begins */
	int root_id;

	sdk_timer->resetTimer(timer);
	sdk_timer->startTimer(timer);

	/* Covert tree to array and send the data to device */
	convert_tree_to_array(root, ocl_tree, &root_id);
	//convert_tree_to_array(root, ocl_tree, 0);


	sdk_timer->stopTimer(timer);
	time_spent = sdk_timer->readTimer(timer);
	printf("Time to convert tree to array in the CPU took %.10f ms\n", 1000 * time_spent);


	sdk_timer->resetTimer(timer);
	sdk_timer->startTimer(timer);
	
	status = clEnqueueWriteBuffer(queue, cl_ocl_tree, CL_TRUE, 0, num_nodes * sizeof(ocl_node), ocl_tree, 0, NULL, NULL); 
	ASSERT_CL(status, "Error clEnqueueWriteBuffer for cl_ocl_tree\n");

	sdk_timer->stopTimer(timer);
	time_spent = sdk_timer->readTimer(timer);
	printf("Time to send the tree the CPU took %.10f ms\n", 1000 * time_spent);


	long long int tree_creation_time = 1000  * time_spent;

	if (!verify_ocl_tree(ocl_tree, num_nodes)) {
		printf("ocl_tree could not be verified.\n");
		exit(1);
	}

	globalSize = (size_t)(num_search_keys / search_per_wi); 
	preferredLocalSize = 256; //64 or 256 gave worse performance! May be because of the diveregnce in the kernel.
	cl_uint arg = 0;

	/* Gpu work enqueue */
	status  = clSetKernelArg(search_kernel, arg++, sizeof(cl_ocl_tree), &cl_ocl_tree);
	status |= clSetKernelArg(search_kernel, arg++, sizeof(cl_int), &root_id);
	status |= clSetKernelArg(search_kernel, arg++, sizeof(cl_search_keys), &cl_search_keys);
	status |= clSetKernelArg(search_kernel, arg++, sizeof(cl_int), &num_search_keys);
	status |= clSetKernelArg(search_kernel, arg++, sizeof(cl_found_nodes_id), &cl_found_nodes_id);
	ASSERT_CL(status, "Error set search_kernel arg.");

	printf("Warming up the device..... \n");

	//Warmup run.
	for (int i = 0; i < 1; i++) {
		initialize_search_keys(search_keys, num_search_keys);
		status = clEnqueueWriteBuffer(queue, cl_search_keys, CL_FALSE, 0, num_search_keys * sizeof(int), search_keys, 0, NULL, NULL); 
		ASSERT_CL(status, "Error clEnqueueWriteBuffer for cl_search_keys\n");

		status = clEnqueueNDRangeKernel(queue, search_kernel, 1, NULL, &globalSize, &preferredLocalSize, 0, NULL, NULL);
		ASSERT_CL(status, "Error when enqueuing search_kernel");

		status = clEnqueueReadBuffer(queue, cl_found_nodes_id, CL_TRUE, 0, num_search_keys * sizeof(int), found_keys, 0, NULL, NULL); 
		ASSERT_CL(status, "Error clEnqueueWriteBuffer for cl_ocl_tree\n");
	}

	printf("Device warm up done...... \n\nNow running kernel to measure performance..\n");

	float search_time = 0;
	float deserialize_time = 0;

	do {

		search_time = 0;
		deserialize_time = 0;


		globalSize = (size_t)(num_search_keys / search_per_wi); 
		
		for (int i = 0; i < iteration; i++) {
		
			initialize_search_keys(search_keys, num_search_keys);

			sdk_timer->resetTimer(timer);
			sdk_timer->startTimer(timer);

			status = clEnqueueWriteBuffer(queue, cl_search_keys, CL_FALSE, 0, num_search_keys * sizeof(int), search_keys, 0, NULL, NULL); 
			ASSERT_CL(status, "Error clEnqueueWriteBuffer for cl_search_keys\n");

			status = clEnqueueNDRangeKernel(queue, search_kernel, 1, NULL, &globalSize, &preferredLocalSize, 0, NULL, NULL);
			ASSERT_CL(status, "Error when enqueuing search_kernel");

			status = clEnqueueReadBuffer(queue, cl_found_nodes_id, CL_TRUE, 0, num_search_keys * sizeof(int), found_keys, 0, NULL, NULL); 
			ASSERT_CL(status, "Error clEnqueueWriteBuffer for cl_ocl_tree\n");

			sdk_timer->stopTimer(timer);
			search_time += sdk_timer->readTimer(timer);

			sdk_timer->resetTimer(timer);
			sdk_timer->startTimer(timer);

			update_found_nodes(root, data, found_keys, num_search_keys);

			sdk_timer->stopTimer(timer);
			deserialize_time += sdk_timer->readTimer(timer);

		}

		
		printf("Avg time to search %d nodes on the GPU = %.10f ms\n", num_search_keys, (1000 * (search_time / iteration)));
		printf("Avg time to deserialize_tree on the CPU = %.10f ms\n", (1000 * (deserialize_time / iteration)));

		found_count = 0;

		for (int i = 0; i < num_search_keys; i++) {
			if (found_keys[i])
				found_count++;
		}

		printf ("Total keys found: %d\n\n", found_count);

	}while (get_next_search_per_wi(&search_per_wi));


	if (ocl_tree)
		free(ocl_tree);

	clReleaseKernel(search_kernel);
	clReleaseCommandQueue(queue);
	clReleaseProgram(program);
	clReleaseContext(context);
}

static void run_hsa_path(int iteration, int search_per_wi, size_t preferredLocalSize)
{
	cl_int status;
	int i;

	/* HSA init */
	cl_platform_id platform = initializePlatform();
	if (platform == 0) {
		printf("No OpenCL platform found!\n");
		exit(!CL_SUCCESS);
	}

	context = createHSAContext(platform);

	cl_device_id  device = selectHSADevice(context);
	if (device == 0) {
		printf("No SVM device found\n");
		exit(1);
	}

	cl_command_queue gpuQueue;
	gpuQueue = clCreateCommandQueue(context, device, 0, &status);
	ASSERT_CL(status, "Error creating command queue");

	cl_device_svm_capabilities_amd svmCaps;
	status = clGetDeviceInfo(device, CL_DEVICE_SVM_CAPABILITIES_AMD, sizeof(cl_device_svm_capabilities_amd), &svmCaps, NULL);
	ASSERT_CL(status, "Error when getting the device info");
	if (!(svmCaps & CL_DEVICE_SVM_ATOMICS_AMD)) {
		printf("Device doesn't support SVM atomics!\n");
		exit(1);
	}

	DeviceSVMMode deviceSVM = detectSVM(device);
	setDeviceSVMFunctions(platform, deviceSVM, &dF);

	std::string kernelString = readCLFile("bst.cl");
	const char* kernelCString = kernelString.c_str();
	cl_program program = clCreateProgramWithSource(context, 1, &kernelCString, NULL, &status);
	ASSERT_CL(status, "Error when creating CL program");

	status = clBuildProgram(program, 1, &device, "-I . -Wf,--support_all_extension", NULL, NULL);
	if (status != CL_SUCCESS) {
		char buildLog[BUILD_LOG_SIZE];
		status = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, BUILD_LOG_SIZE, buildLog, NULL);
		printf("Build log: %s\n", buildLog);
		ASSERT_CL(status, "Error when building CL program");
	}

	cl_kernel insert_kernel = clCreateKernel(program, "bst_insert", &status);
	ASSERT_CL(status, "Error when creating bst_insert kernel");

	cl_kernel search_kernel = clCreateKernel(program, "bst_search", &status);
	ASSERT_CL(status, "Error when creating bst_search kernel");

	cl_command_queue queue = clCreateCommandQueue(context, device, 0, &status);
	ASSERT_CL(status, "Error when creating a command queue");

	init_globals_and_create_tree();

	/* Search begins */

	globalSize = (size_t)(num_search_keys / search_per_wi); 
	preferredLocalSize = 10; //64 or 256 gave worse performance! May be because of the diveregnce in the kernel.

	/* Gpu work enqueue */
	status  = dF.clSetKernelArgSVMPointer(search_kernel, 0, root);
	status |= dF.clSetKernelArgSVMPointer(search_kernel, 1, search_keys);
	status |= dF.clSetKernelArgSVMPointer(search_kernel, 2, &num_search_keys);
	status |= dF.clSetKernelArgSVMPointer(search_kernel, 3, found_key_nodes);
	ASSERT_CL(status, "Error set search_kernel arg.");

	printf("Warming up the device..... \n");

	//Warmup run.
	for (i = 0; i < 10; i++) {
		status = clEnqueueNDRangeKernel(queue, search_kernel, 1, NULL, &globalSize, &preferredLocalSize, 0, NULL, NULL);
		ASSERT_CL(status, "Error when enqueuing search_kernel");
		clFinish(queue);
	}

	printf("Device warm up done...... \n\nNow running kernel to measure performance..\n");

	do {

		cl_event kernel_event;
		globalSize = (size_t)(num_search_keys / search_per_wi); 
		preferredLocalSize = 256; //64 or 256 gave worse performance! May be because of the diveregnce in the kernel.

		sdk_timer->resetTimer(timer);
		sdk_timer->startTimer(timer);


		for (i = 0; i < iteration; i++) {
			sdk_timer->stopTimer(timer);
			initialize_search_keys(search_keys, num_search_keys);
			sdk_timer->startTimer(timer);
			status = clEnqueueNDRangeKernel(queue, search_kernel, 1, NULL, &globalSize, &preferredLocalSize, 0, NULL, &kernel_event);
			ASSERT_CL(status, "Error when enqueuing search_kernel");
			clWaitForEvents(1, &kernel_event);
			clReleaseEvent(kernel_event);
		}

		sdk_timer->stopTimer(timer);

		time_spent = sdk_timer->readTimer(timer);
		printf("Avg time to search %d nodes on the GPU= %.10f ms\n", num_search_keys, 1000 * (time_spent / iteration));

		found_count = 0;

		for (i = 0; i < num_search_keys; i++) {
			if (found_key_nodes[i])
				found_count++;
		}

		printf ("Total keys found: %d\n\n", found_count);

	}while (get_next_search_per_wi(&search_per_wi));


	/* cleanup */
	
	dF.clSVMFree(context, mutex);

	clReleaseKernel(insert_kernel);
	clReleaseKernel(search_kernel);
	clReleaseCommandQueue(queue);
	clReleaseProgram(program);
	clReleaseContext(context);

}


int main(int argc, char* argv[])
{
	cl_int status = 0;

	int iteration = 1;
	int search_per_wi = 1;
	int num_cpu_threads = 4;
	size_t preferredLocalSize = 256;
	int i;
	
	
	// basic arg parsing
	while (argv[1] && argv[1][0] == '-') {
		if (strcmp(argv[1], "-n") == 0) {
			argv++; argc--;
			num_nodes *= atoi(argv[1]);
		} else if (strcmp(argv[1], "-i") == 0) {
			argv++; argc--;
			iteration = atoi(argv[1]);
		} else if (strcmp(argv[1], "-w") == 0) {
			argv++; argc--;
			search_per_wi = atoi(argv[1]);
		} else if (strcmp(argv[1], "-t") == 0) {
			argv++; argc--;
			num_cpu_threads = atoi(argv[1]);
		} else if (strcmp(argv[1], "-g") == 0) {
			argv++; argc--;
			preferredLocalSize = atoi(argv[1]);
		} else if (strcmp(argv[1], "-o") == 0) {
			argv++; argc--;
			use_ocl = atoi(argv[1]);
		} else {
			fprintf(stderr, "Illegal option %s ignored\n", argv[1]);
			printf("Usage: %s [-n (BST tree size) in million nodes][-i (search kernel iteartion)][-w (search per work item)][-t (num_cpu_threads)][-g (work group size)][-o (Use OpenCL stacl]\n", argv[0]);
			exit(1);
		}
		argv++;
		argc--;
	}

	if (argc > 1) {
		printf("Usage: %s [-n (BST tree size) in million nodes][-i (search kernel iteartion)][-w (search per work item)][-t (num_cpu_threads)][-g (work group size)][-o (Use OpenCL stacl]\n", argv[0]);
		exit(1);
	}

	sdk_timer = new SDKTimer();
	timer = sdk_timer->createTimer();

	num_search_keys = (int)(num_nodes * 0.25); //Searching 25% of the data

	if (!use_ocl) {
		printf(" Using HSA stack... \n");
		run_hsa_path(iteration, search_per_wi, preferredLocalSize);
	}
	else {
		printf(" Using Orca stack... \n");
		run_ocl_path(iteration, search_per_wi, preferredLocalSize);
	}

	do {

		sdk_timer->resetTimer(timer);
		sdk_timer->startTimer(timer);
		/********* Start CPU performance measurment. *************/
		memset(found_key_nodes, 0, num_search_keys * sizeof(node *));

		for (i = 0; i < iteration; i++) {
			sdk_timer->stopTimer(timer);
			initialize_search_keys(search_keys, num_search_keys);
			sdk_timer->startTimer(timer);

			multithreaded_search(root, search_keys, num_search_keys, num_cpu_threads, found_key_nodes);
			/* 
			//Single threaded CPU search.
			for (int j = 0; j < num_search_keys; j++) {
			found_keys[j] = search_node(root, search_keys[j]);
			}
			*/
		}

		sdk_timer->stopTimer(timer);
		time_spent = sdk_timer->readTimer(timer);
		printf("Avg Time to search %d nodes on the CPU = %.10f ms\n", num_search_keys, 1000 * (time_spent / iteration)); 

		found_count = 0;
		for (i = 0; i < num_search_keys; i++) {
			if (found_key_nodes[i]) {
				found_count++;
			}
		}

		printf ("Total keys found: %d\n\n", found_count);
	}while (get_next_num_cpu_threads(&num_cpu_threads));


	/* cleanup */
	if (!use_ocl) {
		dF.clSVMFree(context, data);
		dF.clSVMFree(context, found_key_nodes);
		dF.clSVMFree(context, search_keys);
	}
	else {
	
		if (data)
			free(data);

		if (found_keys)
			free(found_keys);

		if (found_key_nodes)
			free(found_key_nodes);

		if (search_keys)
			free(search_keys);
	}
	return 0;
}