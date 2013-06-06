/* Collect all available information on all available devices
 * on all available OpenCL platforms present in the system
 */

#ifdef __APPLE__
#include <OpenCL/cl_ext.h>
#else
#include <CL/cl_ext.h>
#endif

#include <string.h>

#include "cl_error.h"
#include "cl_mem.h"

cl_uint num_platforms;
cl_platform_id *platform;
char **platform_name;
cl_uint *num_devs;
cl_uint num_devs_all;
cl_device_id *all_devices;
cl_device_id *device;

const char* bool_str[] = { "No", "Yes" };
const char* endian_str[] = { "Big-Endian", "Little-Endian" };
const char* device_type_str[] = { "Unknown", "Default", "CPU", "GPU", "Accelerator", "Custom" };
const char* local_mem_type_str[] = { "None", "Local", "Global" };
const char* cache_type_str[] = { "None", "Read-Only", "Read/Write" };

char *buffer;
size_t bufsz, nusz;

#define GET_STRING(cmd, id, param) do { \
	error = cmd(id, param, 0, NULL, &nusz); \
	CHECK_ERROR("get " #param " size"); \
	if (nusz > bufsz) { \
		REALLOC(buffer, nusz, #param); \
		bufsz = nusz; \
	} \
	error = cmd(id, param, bufsz, buffer, 0); \
	CHECK_ERROR("get " #param); \
} while (0)

#define SHOW_STRING(cmd, id, param, str) do { \
	GET_STRING(cmd, id, param); \
	printf("  %-46s: %s\n", str, buffer); \
} while (0)

/* Print platform info and prepare arrays for device info */
void
printPlatformInfo(cl_uint p)
{
	cl_platform_id pid = platform[p];

#define PARAM(param, str) \
	SHOW_STRING(clGetPlatformInfo, pid, CL_PLATFORM_##param, "Platform " str)

	puts("");
	PARAM(NAME, "Name");

	/* Store name for future reference */
	size_t len = strlen(buffer);
	platform_name[p] = malloc(len + 1);
	/* memcpy instead of strncpy since we already have the len
	 * and memcpy is possibly more optimized */
	memcpy(platform_name[p], buffer, len);
	platform_name[p][len] = '\0';

	PARAM(VENDOR, "Vendor");
	PARAM(VERSION, "Version");
	PARAM(PROFILE, "Profile");
	PARAM(EXTENSIONS, "Extensions");
#undef PARAM

	error = clGetDeviceIDs(pid, CL_DEVICE_TYPE_ALL, 0, NULL, num_devs + p);
	CHECK_ERROR("number of devices");
	num_devs_all += num_devs[p];
}

#define GET_PARAM(param, var) do { \
	error = clGetDeviceInfo(dev, CL_DEVICE_##param, sizeof(var), &var, 0); \
	CHECK_ERROR("get " #param); \
} while (0)

#define GET_PARAM_PTR(param, var, num) do { \
	error = clGetDeviceInfo(dev, CL_DEVICE_##param, num*sizeof(*var), var, 0); \
	CHECK_ERROR("get " #param); \
} while (0)

// not available
static const char na[] = "n/a";
static const char fpsupp[] = "Floating-point support";

void
printDeviceInfo(cl_uint d)
{
	cl_device_id dev = device[d];
	cl_device_type devtype;
	cl_device_local_mem_type lmemtype;
	cl_device_mem_cache_type cachetype;
	cl_device_exec_capabilities execap;
	cl_device_fp_config fpconfig;
	cl_command_queue_properties queueprop;
	cl_uint uintval, uintval2;
	cl_uint cursor;
	cl_ulong ulongval;
	double doubleval;
	cl_bool boolval;
	size_t szval;
	size_t *szvals = NULL;
	cl_uint szels = 3;

	char* extensions;

	// these will hold the string from which we detected extension support
	char has_half[12] = {0};
	char has_double[12] = {0};
	char has_nv[29] = {0};

	// device supports OpenCL 1.2
	cl_bool is_12 = 0;

#define KB 1024UL
#define MB (KB*KB)
#define GB (MB*KB)
#define TB (GB*KB)
#define MEM_SIZE(val) ( \
	val > TB ? val/TB : \
	val > GB ? val/GB : \
	val > MB ? val/MB : \
	val/KB )
#define MEM_PFX(val) ( \
	val > TB ? "TB" : \
	val > GB ? "GB" : \
	val > MB ? "MB" : \
	 "KB" )

#define STR_PRINT(name, str) \
	printf("  %-46s: %s\n", name, str)

#define STR_PARAM(param, str) \
	SHOW_STRING(clGetDeviceInfo, dev, CL_DEVICE_##param, "Device " str)
#define INT_PARAM(param, name, sfx) do { \
	GET_PARAM(param, uintval); \
	printf("  %-46s: %u" sfx "\n", name, uintval); \
} while (0)
#define SZ_PARAM(param, name, sfx) do { \
	GET_PARAM(param, szval); \
	printf("  %-46s: %zu\n", name, szval); \
} while (0)
#define MEM_PARAM(param, name) do { \
	GET_PARAM(param, ulongval); \
	doubleval = ulongval; \
	if (ulongval > KB) { \
		snprintf(buffer, bufsz, " (%6.4lg%s)", \
			MEM_SIZE(doubleval), \
			MEM_PFX(doubleval)); \
		buffer[bufsz-1] = '\0'; \
	} else buffer[0] = '\0'; \
	printf("  %-46s: %lu%s\n", \
		name, ulongval, buffer); \
} while (0)
#define BOOL_PARAM(param, name) do { \
	GET_PARAM(param, boolval); \
	STR_PRINT(name, bool_str[boolval]); \
} while (0)

	// device name
	STR_PARAM(NAME, "Device Name");
	STR_PARAM(VENDOR, "Device Vendor");
	STR_PARAM(VERSION, "Device Version");
	is_12 = !!(strstr(buffer, "OpenCL 1.2"));
	SHOW_STRING(clGetDeviceInfo, dev, CL_DRIVER_VERSION, "Driver Version");

	// we get the extensions information here, but only print it at the end
	GET_STRING(clGetDeviceInfo, dev, CL_DEVICE_EXTENSIONS);
	size_t len = strlen(buffer);
	extensions = malloc(len+1);
	memcpy(extensions, buffer, len);
	extensions[len] = '\0';

#define HAS_EXT(ext) (strstr(extensions, #ext))
#define CPY_EXT(what, ext) do { \
	strncpy(has_##what, has, sizeof(#ext)); \
	has_##what[sizeof(#ext)-1] = '\0'; \
} while (0)
#define CHECK_EXT(what, ext) do { \
	has = HAS_EXT(ext); \
	if (has) CPY_EXT(what, ext); \
} while(0)

	{
		char *has;
		CHECK_EXT(half, cl_khr_fp16);
		CHECK_EXT(double, cl_khr_fp64);
		if (!*has_double)
			CHECK_EXT(double, cl_amd_fp64);
		CHECK_EXT(nv, cl_nv_device_attribute_query);
	}


	// device type
	GET_PARAM(TYPE, devtype);
	// FIXME this can be a combination of flags
	STR_PRINT("Device Type", device_type_str[ffs(devtype)]);
	STR_PARAM(PROFILE, "Device Profile");

	// compute units and clock
	INT_PARAM(MAX_COMPUTE_UNITS, "Max compute units",);
	INT_PARAM(MAX_CLOCK_FREQUENCY, "Max clock frequency", "MHz");
	if (*has_nv) {
		GET_PARAM(COMPUTE_CAPABILITY_MAJOR_NV, uintval);
		GET_PARAM(COMPUTE_CAPABILITY_MINOR_NV, uintval2);
		printf("  %-46s: %u.%u\n",
			"NVIDIA Compute Capability",
			uintval, uintval2);
	}

	// workgroup sizes
	INT_PARAM(MAX_WORK_ITEM_DIMENSIONS, "Max work item dimensions",);
	if (uintval > szels)
		szels = uintval;
	REALLOC(szvals, szels, "work item sizes");
	GET_PARAM_PTR(MAX_WORK_ITEM_SIZES, szvals, uintval);
	for (cursor = 0; cursor < uintval; ++cursor) {
		snprintf(buffer, bufsz, "Max work item size[%u]", cursor);
		buffer[bufsz-1] = '\0'; // this is probably never needed, but better safe than sorry
		printf("    %-44s: %zu\n", buffer , szvals[cursor]);
	}
	SZ_PARAM(MAX_WORK_GROUP_SIZE, "Max work group size",);

	// preferred/native vector widths
	printf("  %-46s:", "Preferred / native vector sizes");
#define _PRINT_VEC(UCtype, type, optional, ext) do { \
	GET_PARAM(PREFERRED_VECTOR_WIDTH_##UCtype, uintval); \
	GET_PARAM(NATIVE_VECTOR_WIDTH_##UCtype, uintval2); \
	printf("\n%44s    : %8u / %-8u", #type, uintval, uintval2); \
	if (optional) \
		printf("(%s)", *ext ? ext : na); \
} while (0)
#define PRINT_VEC(UCtype, type) _PRINT_VEC(UCtype, type, 0, "")
#define PRINT_VEC_OPT(UCtype, type, ext) _PRINT_VEC(UCtype, type, 1, ext)

	PRINT_VEC(CHAR, char);
	PRINT_VEC(SHORT, short);
	PRINT_VEC(INT, int);
	PRINT_VEC(LONG, long); // this is actually optional in EMBED profiles
	PRINT_VEC_OPT(HALF, half, has_half);
	PRINT_VEC(FLOAT, float);
	PRINT_VEC_OPT(DOUBLE, double, has_double);
	puts("");

	// FP configurations
#define SHOW_FP_FLAG(str, flag) \
	printf("    %-44s: %s\n", str, bool_str[!!(fpconfig & CL_FP_##flag)])
#define SHOW_FP_SUPPORT(type) do { \
	GET_PARAM(type##_FP_CONFIG, fpconfig); \
	SHOW_FP_FLAG("Denormals", DENORM); \
	SHOW_FP_FLAG("Infinity and NANs", INF_NAN); \
	SHOW_FP_FLAG("Round to nearest", ROUND_TO_NEAREST); \
	SHOW_FP_FLAG("Round to zero", ROUND_TO_ZERO); \
	SHOW_FP_FLAG("Round to infinity", ROUND_TO_INF); \
	SHOW_FP_FLAG("IEEE754-2008 fused multiply-add", FMA); \
	SHOW_FP_FLAG("Support is emulated in software", SOFT_FLOAT); \
} while (0)

#define FPSUPP_STR(str, opt) \
	"  %-17s%-29s:" opt "\n", #str "-precision", fpsupp
	printf(FPSUPP_STR(Half, " (%s)"),
		*has_half ? has_half : na);
	if (*has_half)
		SHOW_FP_SUPPORT(HALF);
	printf(FPSUPP_STR(Single, ""));
	SHOW_FP_SUPPORT(SINGLE);
	printf(FPSUPP_STR(Double, " (%s)"),
		*has_double ? has_double : na);
	if (*has_double)
		SHOW_FP_SUPPORT(DOUBLE);

	// arch bits and endianness
	GET_PARAM(ADDRESS_BITS, uintval);
	GET_PARAM(ENDIAN_LITTLE, boolval);
	printf("  %-46s: %u, %s\n", "Address bits", uintval, endian_str[boolval]);

	// memory size and alignment

	// global
	MEM_PARAM(GLOBAL_MEM_SIZE, "Global memory size");
	BOOL_PARAM(ERROR_CORRECTION_SUPPORT, "Error Correction support");
	MEM_PARAM(MAX_MEM_ALLOC_SIZE, "Max memory allocation");
	BOOL_PARAM(HOST_UNIFIED_MEMORY, "Unified memory for Host and Device");
	if (*has_nv) {
		BOOL_PARAM(INTEGRATED_MEMORY_NV, "NVIDIA integrated memory");
		BOOL_PARAM(GPU_OVERLAP_NV, "NVIDIA concurrent copy and kernel execution");
	}
	INT_PARAM(MIN_DATA_TYPE_ALIGN_SIZE, "Minimum alignment for any data type", " bytes");
	GET_PARAM(MEM_BASE_ADDR_ALIGN, uintval);
	printf("  %-46s: %u bits (%u bytes)\n",
		"Alignment of base address", uintval, uintval/8);

	// cache
	GET_PARAM(GLOBAL_MEM_CACHE_TYPE, cachetype);
	STR_PRINT("Global Memory cache type", cache_type_str[cachetype]);
	if (cachetype != CL_NONE) {
		MEM_PARAM(GLOBAL_MEM_CACHE_SIZE, "Global Memory cache size");
		INT_PARAM(GLOBAL_MEM_CACHELINE_SIZE, "Global Memory cache line", " bytes");
	}

	// images
	BOOL_PARAM(IMAGE_SUPPORT, "Image support");
	if (boolval) {
		if (is_12) {
			SZ_PARAM(IMAGE_MAX_BUFFER_SIZE, "  Max 1D image size", " pixels");
			SZ_PARAM(IMAGE_MAX_ARRAY_SIZE, "  Max 1D or 2D image array size", " images");
		}
		GET_PARAM_PTR(IMAGE2D_MAX_HEIGHT, szvals, 1);
		GET_PARAM_PTR(IMAGE2D_MAX_WIDTH, (szvals+1), 1);
		printf("    %-44s: %zux%zu pixels\n", "Max 2D image size",
			szvals[0], szvals[1]);
		GET_PARAM_PTR(IMAGE3D_MAX_HEIGHT, szvals, 1);
		GET_PARAM_PTR(IMAGE3D_MAX_WIDTH, (szvals+1), 1);
		GET_PARAM_PTR(IMAGE3D_MAX_DEPTH, (szvals+2), 1);
		printf("    %-44s: %zux%zux%zu pixels\n", "Max 3D image size",
			szvals[0], szvals[1], szvals[2]);
		INT_PARAM(MAX_READ_IMAGE_ARGS, "  Max number of read image args",);
		INT_PARAM(MAX_WRITE_IMAGE_ARGS, "  Max number of write image args",);
	}

	// local
	GET_PARAM(LOCAL_MEM_TYPE, lmemtype);
	STR_PRINT("Local Memory type", local_mem_type_str[lmemtype]);
	if (lmemtype != CL_NONE)
		MEM_PARAM(LOCAL_MEM_SIZE, "Local Memory size");

	// constant
	MEM_PARAM(MAX_CONSTANT_BUFFER_SIZE, "Max constant buffer size");
	INT_PARAM(MAX_CONSTANT_ARGS, "Max number of constant args",);

	// nv: registers/CU
	if (*has_nv) {
		INT_PARAM(REGISTERS_PER_BLOCK_NV, "NVIDIA registers per CU",);
	}

	MEM_PARAM(MAX_PARAMETER_SIZE, "Max size of kernel argument");

	// queue and kernel capabilities
	printf("  %-46s:\n", "Queue properties support");
	GET_PARAM(QUEUE_PROPERTIES, queueprop);
	STR_PRINT("  Out-of-order execution", bool_str[!!(queueprop & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE)]);
	STR_PRINT("  Profiling", bool_str[!!(queueprop & CL_QUEUE_PROFILING_ENABLE)]);

	printf("  %-46s:\n", "Execution capabilities");
	GET_PARAM(EXECUTION_CAPABILITIES, execap);
	STR_PRINT("  Run OpenCL kernels", bool_str[!!(execap & CL_EXEC_KERNEL)]);
	STR_PRINT("  Run native kernels", bool_str[!!(execap & CL_EXEC_NATIVE_KERNEL)]);
	if (*has_nv) {
		BOOL_PARAM(KERNEL_EXEC_TIMEOUT_NV, "  NVIDIA kernel execution timeout");
	}

	if (is_12)
		STR_PARAM(BUILT_IN_KERNELS, "Built-in kernels");

	// misc. availability
	BOOL_PARAM(AVAILABLE, "Device Available");
	BOOL_PARAM(COMPILER_AVAILABLE, "Compiler Available");
	if (is_12)
		BOOL_PARAM(LINKER_AVAILABLE, "Linker Available");

	// and finally the extensions
	printf("  %-46s: %s\n", "Device Extensions", extensions); \
}

int main(void)
{
	cl_uint p, d;

	ALLOC(buffer, 1024, "general string buffer");
	bufsz = 1024;

	error = clGetPlatformIDs(0, NULL, &num_platforms);
	CHECK_ERROR("number of platforms");

	printf("%-48s: %u\n", "Number of platforms", num_platforms);
	if (!num_platforms)
		return 0;

	ALLOC(platform, num_platforms, "platform IDs");
	error = clGetPlatformIDs(num_platforms, platform, NULL);
	CHECK_ERROR("platform IDs");

	ALLOC(platform_name, num_platforms, "platform names");
	ALLOC(num_devs, num_platforms, "platform devices");

	for (p = 0; p < num_platforms; ++p)
		printPlatformInfo(p);

	ALLOC(all_devices, num_devs_all, "device IDs");

	for (p = 0, device = all_devices;
	     p < num_platforms;
	     device += num_devs[p++]) {
		error = clGetDeviceIDs(platform[p], CL_DEVICE_TYPE_ALL, num_devs[p], device, NULL);
		printf("\n  %-46s: %s\n", "Platform Name", platform_name[p]);
		printf("%-48s: %u\n", "Number of devices", num_devs[p]);
		for (d = 0; d < num_devs[p]; ++d) {
			printDeviceInfo(d);
		}
	}
}
