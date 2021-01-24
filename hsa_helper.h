
#ifndef _HSA_HELPER_H
#define _HSA_HELPER_H

#include <string>
#include <fstream>
//#include <CL/cl.h>
#include <CL/cl_ext.h>
#include <cassert>

#define ASSERT_CL(STATUS, MSG) if ((STATUS)!=CL_SUCCESS) { if (MSG!=NULL) {printf(MSG); printf("\n");} exit(!CL_SUCCESS); } 

// read a CL file into a string
std::string readCLFile(const std::string& filename) {
  std::ifstream file;
  file.exceptions(std::ifstream::failbit|std::ifstream::badbit);
  file.open(filename.c_str());
  std::string s = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  file.close();
  return s;
}

cl_platform_id initializePlatform() {
  cl_int status = 0;

  // Get the number of OpenCL platforms
  cl_uint numPlatforms;
  status = clGetPlatformIDs(0, NULL, &numPlatforms);
  ASSERT_CL(status, "Error getting number of platforms");

  cl_platform_id selectedPlatform = 0;
  if (numPlatforms > 0) {

    // Get all the platform IDs
    cl_platform_id* platforms = (cl_platform_id*)malloc(numPlatforms*sizeof(cl_platform_id));
    status = clGetPlatformIDs(numPlatforms, platforms, NULL);
    ASSERT_CL(status, "Error getting platform IDs");

    // Find the platform that supports SVM
    char vendor[128];
    for (unsigned int i = 0; i < numPlatforms; i++) {
      status = clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, sizeof(vendor), vendor, NULL);

#ifdef _DEBUG
      printf("Platform Vendor: %s\n",vendor);
#endif

      if(!strcmp(vendor, "Advanced Micro Devices, Inc.")) {
        selectedPlatform = platforms[i];
        break;
      }
    }
    free(platforms);
  }
  return selectedPlatform;
}

cl_context createHSAContext(cl_platform_id platform, bool useHSA = true) {
  cl_int status = 0;
  cl_context_properties cps[3] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
  cl_device_type deviceType = CL_DEVICE_TYPE_ALL;

if (useHSA)
{
#ifdef CL_HSA_ENABLED_AMD
  deviceType|=CL_HSA_ENABLED_AMD;
#endif   
}

  cl_context context = clCreateContextFromType(cps, deviceType, NULL, NULL, &status);
  ASSERT_CL(status, "Error when creating the context");
  return context;
}

cl_device_id  selectHSADevice(cl_context context) {

  cl_uint numDevices;
  cl_int status;
  status = clGetContextInfo(context, CL_CONTEXT_NUM_DEVICES, sizeof(numDevices), &numDevices, NULL);
  ASSERT_CL(status, "Error when querying the number of devices from the context");

#ifdef _DEBUG
  printf("Devices found: %u\n", numDevices);
#endif
  cl_device_id* devices = NULL;
  cl_device_id selectedDevice = 0;
  if (numDevices > 0) {
    devices = (cl_device_id*)malloc(numDevices*sizeof(cl_device_id));
    status = clGetContextInfo(context, CL_CONTEXT_DEVICES, numDevices*sizeof(cl_device_id), devices, NULL);
    ASSERT_CL(status, "Error when getting the device IDs from the context");
    for (unsigned int i = 0; i < numDevices; i++) {

      cl_device_type deviceType;
      status = clGetDeviceInfo(devices[i], CL_DEVICE_TYPE, sizeof(cl_device_type), &deviceType, NULL);
      ASSERT_CL(status, "Error when getting the device type");

#ifdef _DEBUG
      switch(deviceType) {
      case CL_DEVICE_TYPE_CPU: printf("Device type: CPU\n"); break;
      case CL_DEVICE_TYPE_GPU: printf("Device type: GPU\n"); break;
      case CL_DEVICE_TYPE_ACCELERATOR: printf("Device type: Accelerator\n"); break;
      case CL_DEVICE_TYPE_DEFAULT: printf("Device type: Default device\n"); break;
      case CL_DEVICE_TYPE_CUSTOM: printf("Device type: Custom device\n"); break;
      default: printf("Device type: Unknown device\n"); break;
      };
#endif

      if (deviceType == CL_DEVICE_TYPE_GPU) {

#ifdef CHECK_IGPU
        cl_bool isIGPU;
        status = clGetDeviceInfo(devices[i], CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(cl_bool), &isIGPU, NULL);
        ASSERT_CL(status, "Error when getting the device info");
        printf("Host unified memory: %s\n", (const char*)(isIGPU?"Yes":"No"));
#endif // CHECK_IGPU

        // check for SVM extension
        char deviceOptions[512];
        status = clGetDeviceInfo(devices[i], CL_DEVICE_EXTENSIONS, 512, deviceOptions, NULL);
        ASSERT_CL(status, "Error when getting the device info");
        if (
#define DISABLE_EXT_SVM_CHECK
#ifdef DISABLE_EXT_SVM_CHECK
1
#else
          strstr(deviceOptions, "cl_ext_cl20_svm")
#endif
          !=NULL
          ) {
          selectedDevice = devices[i];
          break;
        }
      }
    }
  }
  if (devices) free(devices);
  return selectedDevice;
}

struct DeviceSVMFunctions {
#ifdef CL_HSA_ENABLED_AMD
    clSVMAllocAMD_fn clSVMAlloc;
    clSVMFreeAMD_fn clSVMFree;
    clSetKernelArgSVMPointerAMD_fn clSetKernelArgSVMPointer;
    clSetKernelExecInfoAMD_fn clSetKernelExecInfo;
    clEnqueueSVMFreeAMD_fn clEnqueueSVMFree;
    clEnqueueSVMMemcpyAMD_fn clEnqueueSVMMemcpy;
    clEnqueueSVMMemFillAMD_fn clEnqueueSVMMemFill;
    clEnqueueSVMMapAMD_fn clEnqueueSVMMap;
    clEnqueueSVMUnmapAMD_fn clEnqueueSVMUnmap;
#endif
};

// Helper function that extracts the function pointer for the specified extension name.
void *getExtensionFunction(cl_platform_id platform_id, const char *extensionFunctionName) 
{
    void * p =  clGetExtensionFunctionAddressForPlatform (platform_id, extensionFunctionName);
    if (!p) {
        printf("Unable to find extension function '%s'\n", extensionFunctionName);
        exit(-1);
    }

    return p;
};

enum DeviceSVMMode { SVM_CL20, SVM_Extension, SVM_None };

// Set up structure with function pointers to SVM functions.
// For OpenCL2.0 platforms, this will point to the functions defined in OpenCL 2.0 APIs.
// For platforms with AMD HSA extension, this will point to the extension functions.
void setDeviceSVMFunctions(cl_platform_id platform_id, DeviceSVMMode deviceSVM, 
                           DeviceSVMFunctions *dF) 
{
#if defined CL_VERSION_2_0

    if (deviceSVM == SVM_CL20) {
        // Use OpenCL2.0 functions if compiled with OpenCL2.0 headers, and the runtime 
        // platform supports OpenCL 2.0, install the 2.0 SVM functions:
        dF->clSVMAlloc          = clSVMAlloc;
        dF->clSVMFree           = clSVMFree;
        dF->clSetKernelArgSVMPointer   = clSetKernelArgSVMPointer;
        dF->clSetKernelExecInfo = clSetKernelExecInfo;
        dF->clEnqueueSVMFree    = clEnqueueSVMFree;
        dF->clEnqueueSVMMemcpy  = clEnqueueSVMMemcpy;
        dF->clEnqueueSVMMemFill = clEnqueueSVMMemFill;
        dF->clEnqueueSVMMap     = clEnqueueSVMMap;
        dF->clEnqueueSVMUnmap   = clEnqueueSVMUnmap;
    } 
    else 
#endif
#ifdef CL_HSA_ENABLED_AMD
    {
        if (deviceSVM == SVM_Extension) {
            // If compiling with 1.2 or 2.0, and the device does not support 2.0 but 
            // does support the AMD SVM extension, install the extension function pointers: 
            dF->clSVMAlloc             =  (clSVMAllocAMD_fn) getExtensionFunction (platform_id, "clSVMAllocAMD");
            dF->clSVMFree             =  (clSVMFreeAMD_fn)  getExtensionFunction (platform_id, "clSVMFreeAMD");
            dF->clSetKernelArgSVMPointer = (clSetKernelArgSVMPointerAMD_fn) getExtensionFunction(platform_id, "clSetKernelArgSVMPointerAMD");
            dF->clSetKernelExecInfo = (clSetKernelExecInfoAMD_fn) getExtensionFunction(platform_id, "clSetKernelExecInfoAMD");
            dF->clEnqueueSVMFree    = (clEnqueueSVMFreeAMD_fn) getExtensionFunction(platform_id, "clEnqueueSVMFreeAMD");
            dF->clEnqueueSVMMemcpy  = (clEnqueueSVMMemcpyAMD_fn) getExtensionFunction(platform_id, "clEnqueueSVMMemcpyAMD");
            dF->clEnqueueSVMMemFill = (clEnqueueSVMMemFillAMD_fn) getExtensionFunction(platform_id, "clEnqueueSVMMemFillAMD");
            dF->clEnqueueSVMMap     = (clEnqueueSVMMapAMD_fn) getExtensionFunction(platform_id, "clEnqueueSVMMapAMD");
            dF->clEnqueueSVMUnmap   = (clEnqueueSVMUnmapAMD_fn) getExtensionFunction(platform_id, "clEnqueueSVMUnmapAMD");

        } else {
            // Platform does not support CL20 or SVM, init function pointers to NULL.
            dF->clSVMAlloc =  NULL;
            dF->clSVMFree  =  NULL;
            dF->clSetKernelArgSVMPointer =  NULL;
            dF->clSetKernelExecInfo =  NULL;
            dF->clEnqueueSVMFree    =  NULL;
            dF->clEnqueueSVMMemcpy  =  NULL;
            dF->clEnqueueSVMMemFill =  NULL;
            dF->clEnqueueSVMMap     =  NULL;
            dF->clEnqueueSVMUnmap   =  NULL;
        }
    }
#endif
}

// Detect if the platform is OpenCL 2 (which implicitly supports SVM) 
DeviceSVMMode detectSVM(cl_device_id device_id)
{
    DeviceSVMMode deviceSVM = SVM_None;
    char device_version_string[1000];
    if (CL_SUCCESS == clGetDeviceInfo(device_id, CL_DEVICE_VERSION, 1000, 
                                      device_version_string, NULL)) {
        int majorRev, minorRev;
        if (sscanf(device_version_string, "OpenCL %d.%d", &majorRev, &minorRev) == 2) {
            if (majorRev >= 2) {
                deviceSVM = SVM_CL20;
            }
        }
    }

    size_t extNameSz;
    if (CL_SUCCESS == clGetDeviceInfo(device_id, CL_DEVICE_EXTENSIONS, 0, NULL, &extNameSz)) {
        char *extNames = (char *) (malloc(extNameSz));
        if (CL_SUCCESS == clGetDeviceInfo(device_id, CL_DEVICE_EXTENSIONS, extNameSz, 
                                          extNames, NULL)) {
            // printf("Device Extensions Supported:%s\n", extNames);
            if (strstr(extNames, "cl_amd_svm") != NULL) {
                deviceSVM=SVM_Extension;
            }
        }
        free(extNames);
    }

    return deviceSVM;
}
//clSVMalloc_fn                       clSVMAlloc;
//clSVMFree_fn                        clSVMFree;
//clSetKernelArgSVMPointer_fn         clSetKernelArgSVMPointer;
//clSetKernelExecInfo_fn              clSetKernelExecInfo;
//clEnqueueSVMFree_fn                 clEnqueueSVMFree;
//clEnqueueSVMMemcpy_fn               clEnqueueSVMMemcpy;
//clEnqueueSVMMemFill_fn              clEnqueueSVMMemFill;
//clEnqueueSVMMap_fn                  clEnqueueSVMMap;
//clEnqueueSVMUnmap_fn                clEnqueueSVMUnmap;
//cl_device_svm_capabilities          svmCaps_;
//
//void setupSVMExtenstion(cl_platform_id platform) {
//  clSVMAlloc = (clSVMalloc_fn) clGetExtensionFunctionAddressForPlatform(platform,"clSVMallocAMD");
//  assert(clSVMAlloc);
//  clSVMFree = (clSVMFree_fn) clGetExtensionFunctionAddressForPlatform(platform,"clSVMFreeAMD");
//  assert(clSVMFree);
//  clSetKernelArgSVMPointer =(clSetKernelArgSVMPointer_fn) clGetExtensionFunctionAddressForPlatform(platform,"clSetKernelArgSVMPointerAMD");
//  assert(clSetKernelArgSVMPointer);
//  clSetKernelExecInfo = (clSetKernelExecInfo_fn) clGetExtensionFunctionAddressForPlatform(platform,"clSetKernelExecInfoAMD");
//  assert(clSetKernelExecInfo);
//  clEnqueueSVMFree = (clEnqueueSVMFree_fn) clGetExtensionFunctionAddressForPlatform(platform,"clEnqueueSVMFreeAMD");
//  assert(clEnqueueSVMFree);
//  clEnqueueSVMMemcpy = (clEnqueueSVMMemcpy_fn) clGetExtensionFunctionAddressForPlatform(platform,"clEnqueueSVMMemcpyAMD");
//  assert(clEnqueueSVMMemcpy);
//  clEnqueueSVMMemFill = (clEnqueueSVMMemFill_fn) clGetExtensionFunctionAddressForPlatform(platform,"clEnqueueSVMMemFillAMD");
//  assert(clEnqueueSVMMemFill);
//  clEnqueueSVMMap = (clEnqueueSVMMap_fn) clGetExtensionFunctionAddressForPlatform(platform,"clEnqueueSVMMapAMD");
//  assert(clEnqueueSVMMap);
//  clEnqueueSVMUnmap = (clEnqueueSVMUnmap_fn) clGetExtensionFunctionAddressForPlatform(platform,"clEnqueueSVMUnmapAMD");
//  assert(clEnqueueSVMUnmap);
//}



#endif // _HSA_HELPER_H