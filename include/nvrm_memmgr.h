/*
 * Copyright (c) 2009 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef INCLUDED_nvrm_memmgr_H
#define INCLUDED_nvrm_memmgr_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_init.h"

#include "nvos.h"

/**
 * FAQ for commonly asked questions:
 *
 * Q) Why can NvRmMemMap fail?
 * A) Some operating systems don't allow user mode applications to map arbitrary
 *    memory regions, this is a huge security hole.  In other environments, such
 *    as simulation, its just not even possible to get a direct pointer to 
 *    the memory, because the simulation is in a different process.
 *
 * Q) What do I do if NvRmMemMap fails?
 * A) Driver writers have two choices.  If the driver must have a mapping, for
 *    example direct draw requires a pointer to the memory then the driver
 *    will have to fail whatever operation it is doing and return an error.
 *    The other choice is to fall back to using NvRmMemRead/Write functions
 *    or NvRmMemRdxx/NvRmMemWrxx functions, which are guaranteed to succeed.
 *
 * Q) Why should I use NvRmMemMap instead of NvOsPhysicalMemMap?
 * A) NvRmMemMap will do a lot of extra work in an OS like WinCE to create
 *    a new mapping to the memory in your process space.  NvOsPhysicalMemMap
 *    will is for mapping registers and other non-memory locations.  Using
 *    this API on WindowsCE will cause WindowsCE to crash.
 *
 * Q) Why is Matt Craighead so smart?
 * A) He has two brains.
 */


/**
 * UNRESOLVED ISSUES:
 *
 * 1. Should we have NvRmFill* APIs in addition to NvRmWrite*?  Say, if you just
 *    want to clear a buffer to zero?
 *
 * 2. There is currently an issue with a memhandle that is shared across
 *    processes.  If a MemHandle is created, and then duplicated into another
 *    process using NvRmMemHandleGetId/NvRmMemHandleFromId it's not clear
 *    what would happen if both processes tried to do an NvRmAlloc on a handle.
 *    Perhaps make NvRmMemHandleGetId fail if the memory is not already
 *    allocated.
 *
 * 3. It may be desirable to have more hMem query functions, for debuggability.
 *    Part of the information associated with a memory buffer will live in
 *    kernel space, and not be accesible efficiently from a user process.
 *    Knowing which heap a buffer is in, or whether a buffer is pinned or
 *    mapped could be useful.  Note that queries like this could involve race
 *    conditions.  For example, memory could be moved from one heap to another
 *    the moment after you ask what heap it's in.
 */

/**
 * @defgroup nvrm_memmgr RM Memory Management Services
 *
 * @ingroup nvddk_rm
 *
 * The APIs in this header file are intended to be used for allocating and
 * managing memory that needs to be accessed by HW devices.  It is not intended
 * as a replacement for malloc() -- that functionality is provided by
 * NvOsAlloc().  If only the CPU will ever access the memory, this API is
 * probably extreme overkill for your needs.
 *
 * Memory allocated by NvRmMemAlloc() is intended to be asynchronously movable
 * by the RM at any time.  Although discouraged, it is possible to permanently
 * lock down ("pin") a memory buffer such that it can never be moved.  Normally,
 * however, the intent is that you would only pin a buffer for short periods of
 * time, on an as-needed basis.
 *
 * The first step to allocating memory is allocating a handle to refer to the
 * allocation.  The handle has a separate lifetime from the underlying buffer.
 * Some properties of the memory, such as its size in bytes, must be declared at
 * handle allocation time and can never be changed.
 *
 * After successfully allocating a handle, you can specify properties of the
 * memory buffer that are allowed to change over time.  (Currently no such
 * properties exist, but in the past a "priority" attribute existed and may
 * return some day in the future.)
 *
 * After specifying the properties of the memory buffer, it can be allocated.
 * Some additional properties, such as the set of heaps that the memory is
 * permitted to be allocated from, must be specified at allocation time and
 * cannot be changed over the buffer's lifetime of the buffer.
 *
 * The Coherency specified during the allocation of memory buffer is used for
 * accesses that occur from CPU.
 * If the coherency specified is Uncached, The accesses from CPU would never
 * get data into cache.
 * If the coherency is specified as WriteCombined, The accesses from CPU would
 * never get data into caches, But data can pass through store buffers.
 * If the coherency is specified as WriteBack, The accesses from CPU can pass
 * through store buffers and caches.
 * If the coherency is specified as InnerWriteBack, The accesses from CPU can
 * pass through store buffers and inner caches only.
 * On SOC's, where no outer cache exists, WriteBack and InnerWriteBack refer
 * to same coherency.
 * After accessing memory buffer from CPU, before handing over the buffer to
 * H/W engines, Cache maintenance(NvRmMemCacheMaint()) on buffer is necessary
 * irrespective of coherency used. Yes, even for uncached buffers, even though
 * cache is not flushed for Uncached buffers. It might be needed to execute
 * barriers and/or store buffer flushes depending how uncached memory is handled
 * in OS for the specific processor. It might be a NOP for a specific processor.
 * But not calling, NvRmMemCacheMaint() can cause issues with
 * future processors that require carrying out any specific operations like
 * flushing pending transactions, forcing ordering, etc. to ensure coherency.
 *
 * Before accessing memory from CPU, after H/W engine writes memory,
 * Cache maintenance is necessary for WriteBack and InnerWriteBack coherency.
 * Speculative reads in a processor can get any data into cache for the memory
 * that is mapped as Cached even though S/W didn't access it explicitly.
 * In order ensure CPU access the correct data written by H/W module, it should
 * invalidate cache for the memory it is accessing after H/W has written to it.
 *
 * The contents of memory can be examined and modified using a variety of read
 * and write APIs, such as NvRmMemRead and NvRmMemWrite.  However, in some
 * cases, it is necessary for the driver or application to be able to directly
 * read or write the buffer using a pointer.  In this case, the NvRmMemMap API
 * can be used to obtain such a mapping into the current process's virtual
 * address space.  It is important to note that the map operation is not
 * guaranteed to succeed.  Drivers that use mappings are strongly encouraged
 * to support two code paths: one for when the mapping succeeds, and one for
 * when the mapping fails.  A memory buffer is allowed to be mapped multiple
 * times, and the mappings are permitted to be of subregions of the buffer if
 * desired.
 *
 * Before the memory buffer is used, it must be pinned.  While pinned, the
 * buffer will not be moved, and its physical address can be safely queried.  A
 * memory buffer can be pinned multiple times, and the pinning will be reference
 * counted.  Assuming a valid handle and a successful allocation, pinning can
 * never fail.
 *
 * After the memory buffer is done being used, it should be unpinned.  Unpinning
 * never fails.  Any unpinned memory is free to be moved to any location which
 * satisfies the current properties in the handle.  Drivers are strongly
 * encouraged to unpin memory when they reach a quiescent state.  It is not
 * unreasonable to have a goal that all memory buffers (with the possible
 * exception of memory being continuously scanned out by the display) be
 * unpinned when the system is idle.
 *
 * The memory buffer allocated as Uncached/WriteCombined can be mapped as
 * WriteBack/InnerWriteBack. Once it is mapped as WriteBack/InnerWriteBack,
 * entire memory buffer is converted to be WriteBack/InnerWriteBack buffer.
 * It is an irreversible operation. NvRmMemUnmap wouldn't restore the memory
 * back to Uncached/WriteCombined.
 * Beware of the fact that having conflicting mappings at the same time on
 * memory buffer can lead to undesired consequences based on the usage.
 * i.e, If the memory buffer allocated as UC/WC, mapped as UC/WC buffer,
 * and converted to WB/IWB, mapped as WB/IWB(without unmapping UC/WC mapping),
 * then care need to be taken to avoid coherency issues.
 * If UC/WC mapping(s) is unmapped before mapping as WB/IWB, then the memory
 * buffer would behave as if the buffer was allocated as WB/IWB at alloc time.
 *
 * The NvRmMemPin API is only one of the two ways to pin a buffer.  In the case
 * of modules that are programmed through command buffers submitted through
 * host, it is not the preferred way to pin a buffer.  The "RELOC" facility in
 * the stream API should be used instead if possible.  It is conceivable that in
 * the distant future, the NvRmMemPin API might be removed.  In such a world,
 * all graphics modules would be expected to use the RELOC API or a similar API,
 * and all IO modules would be expected to use zero-copy DMA directly from the
 * application buffer using NvOsPageLock.
 *
 * Some properties of a buffer can be changed at any point in its handle's
 * lifetime.  Properties that are changed while a memory buffer is pinned will
 * have no effect until the memory is unpinned.
 *
 * After you are done with a memory buffer, you must free its handle.  This
 * automatically unpins the memory (if necessary) and frees the storage (if any)
 * associated with it.
 *
 * @ingroup nvrm_memmgr
 * @{
 */


/**
 * A type-safe handle for a memory buffer.
 */

typedef struct NvRmMemRec *NvRmMemHandle;

/**
 * Define for invalid Physical address
 */
#define NV_RM_INVALID_PHYS_ADDRESS (0xffffffff)

/**
 * NvOsConfig key to control messages on MemMgr failures.
 *
 * By default (when the key has no value), verbose printing is enabled
 * for debug builds and disabled for release builds. Setting the key will
 * override default setting.
 *
 * Set value to:
 *  1  enable more verbose printing; and
 *  0  disable verbose printing.
 *
 * Programmatically status can be set and queried with  NvOsSetConfigU32 and
 * NvOsGetConfigU32, respectively. See NvOs for more information on platform
 * specifics about config values.
 *
 * Note that verbose printing might not be supported on all backends.
 * Currently it is supported Android and Linux running on device.
 */

#define NVRM_MEMMGR_VERBOSITY_KEY "nvrm.memmgr.vbose"

/**
 * NvRm heap identifiers.
 */

typedef enum
{

    /**
     * External (non-carveout, i.e., OS-managed) memory heap.
     */
    NvRmHeap_External = 1,

    /**
     * GART memory heap.  The GART heap is really an alias for the External
     * heap.  All GART allocations will come out of the External heap, but
     * additionally all such allocations will be mapped in the GART.  Calling
     * NvRmMemGetAddress() on a buffer allocated in the GART heap will return
     * the GART address, not the underlying memory address.
     */
    NvRmHeap_GART,

    /**
     * IOMMU heap allocates non-contiguous memory from OS heap. Maps memory into
     * H/W modules visible contiguous virtual address space (IOVA space).
     */
    NvRmHeap_IOMMU = NvRmHeap_GART,

    /**
     * Carve-out memory heap within external memory.
     */
    NvRmHeap_ExternalCarveOut,

    /**
     * IRAM memory heap.
     */
    NvRmHeap_IRam,

    /**
     * Carveout special heap; uses external carveout if no camera heap
     * has been instantiated on the device
     */
    NvRmHeap_Camera,

    /**
     * Video Protection Region(VPR) heap.
     */
    NvRmHeap_VPR,

    /**
     * Secured memory heap within external memory.
     */
    NvRmHeap_Secured,
    NvRmHeap_Num,
    NvRmHeap_Force32 = 0x7FFFFFFF
} NvRmHeap;

/**
 * NvRm heap statistics. See NvRmMemGetStat() for further details.
 */

typedef enum
{

    /**
     * Total number of bytes reserved for the carveout heap.
     */
    NvRmMemStat_TotalCarveout = 1,

    /**
     * Number of bytes used in the carveout heap.
     */
    NvRmMemStat_UsedCarveout,

    /**
     * Size of the largest free block in the carveout heap. 
     * Size can be less than the difference of total and 
     * used memory.
     */
    NvRmMemStat_LargestFreeCarveoutBlock,

    /**
     * Total number of bytes in the GART heap. 
     */
    NvRmMemStat_TotalGart,

    /**
     * Number of bytes reserved from the GART heap.
     */
    NvRmMemStat_UsedGart,

    /**
     * Size of the largest free block in GART heap. Size can be 
     * less than the difference of total and used memory.
     */
    NvRmMemStat_LargestFreeGartBlock,
    NvRmMemStat_Num,
    NvRmMemStat_Force32 = 0x7FFFFFFF
} NvRmMemStat;

/**
 * Returns the file descriptor of the nvmap device opened by NvRm when the
 * library is loaded.
 *
 * @retval -1 Indicates that NvRm failed to open the nvmap device.
 * @returns the open file descriptor of the nvmap device.
 */
int NvRm_MemmgrGetIoctlFile(void);

/**
 * Allocates a memory handle that can be used to specify a memory allocation
 * request and manipulate the resulting storage.
 *
 * @see NvRmMemHandleFree()
 *
 * @param hDevice An RM device handle.
 * @param phMem A pointer to an opaque handle that will be filled in with the
 *     new memory handle.
 * @param Size Specifies the requested size of the memory buffer in bytes.
 *
 * @retval NvSuccess Handle creation is successful.
 * @retval NvError_InsufficientMemory Out of memory.
 * @retval NvError_AccessDenied Operation not permitted.
 * @retval NvError_BadParameter Invalid argument(s).
 * @retval NvError_IoctlFailed Unknown error.
 */

 NvError NvRmMemHandleCreate( 
    NvRmDeviceHandle hDevice,
    NvRmMemHandle * phMem,
    NvU32 Size );

/**
 * Looks up a pre-existing memory handle whose allocation was preserved through
 * the boot process.
 *
 * Looking up a memory handle is a one-time event.  Once a preserved handle
 * has been successfully looked up, it may not be looked up again.  Memory
 * handles created with this mechanism behave identically to memory handles
 * created through NvRmMemHandleCreate, including freeing the allocation with
 * NvRmMemHandleFree.
 *
 * @param hDevice An RM device handle.
 * @param Key The key value that was returned by the earlier call to
 *    @see NvRmMemHandlePreserveHandle.
 * @param phMem A pointer to an opaque handle that will be filled in with the
 *    queried memory handle, if a preserved handle matching the key is found.
 *
 * @retval NvSuccess Indicates Handle is successfully created from Key.
 * @retval NvError_InsufficientMemory Out of memory.
 * @retval NvError_AccessDenied Operation not permitted.
 * @retval NvError_NotInitialized Memory subsytem is not ready.
 * @retval NvError_BadParameter Invalid argument(s).
 * @retval NvError_IoctlFailed Unknown error.
 */

 NvError NvRmMemHandleClaimPreservedHandle( 
    NvRmDeviceHandle hDevice,
    NvU32 Key,
    NvRmMemHandle * phMem );

/**
 * Adds a memory handle to the set of memory handles which will be preserved
 * between the current OS context and a subsequent OS context.
 *
 * @param hMem The handle which will be marked for preservation
 * @param Key  A key which can be used to claim the memory handle in a
 *    different OS context.
 *
 * @retval NvSuccess Indicates that the memory handle will be preserved.
 * @retval NvError_InsufficientMemory Out of memory or BootArg memory
 *    was not avaialable to mark the memory handle as preserved.
 * @retval NvError_NotSupported Operation is not supported.
 * @retval NvError_BadParameter Invalid argument(s).
 */

 NvError NvRmMemHandlePreserveHandle( 
    NvRmMemHandle hMem,
    NvU32 * Key );

/**
 * Frees a memory handle obtained from NvRmMemHandleCreate(),
 * or NvRmMemHandleFromId().
 *
 * Fully disposing of a handle requires calling this API one time, plus one
 * time for each NvRmMemHandleFromId().  When the internal reference count of
 * the handle reaches zero, all resources for the handle will be released, even
 * if the memory is marked as pinned and/or mapped.  It is the caller's
 * responsibility to ensure mappings are released before calling this API.
 *
 * When the last handle is closed, the associated storage will be implicitly
 * unpinned and freed.
 *
 * This API cannot fail.
 *
 * @see NvRmMemHandleCreate()
 * @see NvRmMemHandleFromId()
 *
 * @param hMem A previously allocated memory handle.  If hMem is NULL, this API
 *     has no effect.
 */

 void NvRmMemHandleFree( 
    NvRmMemHandle hMem );

/**
 * Allocate storage for a memory handle.  The storage must satisfy:
 *  1) all specified properties in the hMem handle
 *  2) the alignment parameters
 *
 * Memory allocated by this API is intended to be used by modules which
 * control hardware devices such as media accelerators or I/O controllers.
 *
 * The memory will initially be in an unpinned state.
 *
 * Assert encountered in debug mode if alignment was not a power of two,
 *     or coherency is not one of NvOsMemAttribute_Uncached,
 *     NvOsMemAttribute_WriteBack, NvOsMemAttribute_WriteCombined or
 *     NvOsMemAttribute_InnerWriteBack.
 *
 * @see NvRmMemPin()
 *
 * @param hMem The memory handle to allocate storage for.
 * @param Heaps[] An array of heap enumerants that indicate which heaps the
 *     memory buffer is allowed to live in.  When a memory buffer is requested
 *     to be allocated or needs to be moved, Heaps[0] will be the first choice
 *     to allocate from or move to, Heaps[1] will be the second choice, and so
 *     on until the end of the array.
 * @params NumHeaps The size of the Heaps[] array.  If NumHeaps is zero, then
 *     Heaps must also be NULL, and the RM will select a default list of heaps
 *     on the client's behalf.
 * @param Alignment Specifies the requested alignment of the buffer, measured in
 *     bytes.  Must be a power of two.
 * @param Coherency Specifies the cache coherency mode desired if the memory
 *     is ever mapped.
 * @param Tags Specifies application-specific tags which could be used during
 *     debugging to identify allocations.
 *
 * @retval NvSuccess Indicates the memory buffer is successfully allocated.
 * @retval NvError_InsufficientMemory Out of memory.
 * @retval NvError_AlreadyAllocated Memory buffer is already allocated.
 * @retval NvError_BadParameter Invalid argument(s).
 * @retval NvError_AccessDenied Operation not permitted.
 * @retval NvError_IoctlFailed Unknown error.
 */

 NvError NvRmMemAllocInternal( 
    NvRmMemHandle hMem,
    const NvRmHeap * Heaps,
    NvU32 NumHeaps,
    NvU32 Alignment,
    NvOsMemAttribute Coherency );

 NvError NvRmMemAllocInternalTagged( 
    NvRmMemHandle hMem,
    const NvRmHeap * Heaps,
    NvU32 NumHeaps,
    NvU32 Alignment,
    NvOsMemAttribute Coherency,
    NvU16 Tags );

 NvError NvRmMemAlloc( 
    NvRmMemHandle hMem,
    const NvRmHeap * Heaps,
    NvU32 NumHeaps,
    NvU32 Alignment,
    NvOsMemAttribute Coherency );
#define NVRM_MEM_TAG_NONE (0x0)
#define NVRM_MEM_TAG_GL_MISC (0x3d00)
#define NVRM_MEM_TAG_GL_TEXTURE (0x3d01)
#define NVRM_MEM_TAG_GL_RBO (0x3d02)
#define NVRM_MEM_TAG_GL_VBO (0x3d03)
#define NVRM_MEM_TAG_GL_VCAA (0x3d04)
#define NVRM_MEM_TAG_DDK2D_MISC (0x2d00)
#define NVRM_MEM_TAG_EGL_MISC (0x100)
#define NVRM_MEM_TAG_CAMERA_MISC (0x200)
#define NVRM_MEM_TAG_CODECS_MISC (0x300)
#define NVRM_MEM_TAG_DISPLAY_MISC (0x400)
#define NVRM_MEM_TAG_DISPMGR_MISC (0x500)
#define NVRM_MEM_TAG_MM_MISC (0x600)
#define NVRM_MEM_TAG_OPENMAX_MISC (0x700)
#define NVRM_MEM_TAG_WSI_MISC (0x800)
#define NVRM_MEM_TAG_SM_MISC (0x900)
#define NVRM_MEM_TAG_RM_MISC (0xa00)
#define NVRM_MEM_TAG_SYSTEM_MISC (0xb00)
#define NVRM_MEM_TAG_ODM_MISC (0xc00)
#define NVRM_MEM_TAG_HAL_MISC (0xd00)
#define NVRM_MEM_TAG_GRALLOC_MISC (0xe00)
#define NVRM_MEM_TAG_OMXCAMERA_MISC (0xf00)
#define NVRM_MEM_TAG_X11_MISC (0x1000)

 NvError NvRmMemAllocTagged( 
    NvRmMemHandle hMem,
    const NvRmHeap * Heaps,
    NvU32 NumHeaps,
    NvU32 Alignment,
    NvOsMemAttribute Coherency,
    NvU16 Tags );

/**
 * Attempts to lock down a piece of previously allocated memory.  By default
 * memory is "movable" until it is pinned -- the RM is free to relocate it from
 * one address or heap to another at any time for any reason (say, to defragment
 * a heap).  This function can be called to prevent the RM from moving the
 * memory.
 *
 * While a memory buffer is pinned, its physical address can safely be queried
 * with NvRmMemGetAddress().
 *
 * This API always succeeds.
 *
 * Pins are reference counted, so the memory will remain pinned until all Pin
 * calls have had a matching Unpin call.
 *
 * Pinning and mapping a memory buffer are completely orthogonal.  It is not
 * necessary to pin a buffer before mapping it.  Mapping a buffer does not imply
 * that it is pinned.
 *
 * @see NvRmMemGetAddress()
 * @see NvRmMemUnpin()
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate,
 *              NvRmMemHandleFromId.
 *
 * @returns The physical address of the first byte in the specified memory
 *     handle's storage.  If the memory is mapped through the GART, the
 *     GART address will be returned, not the address of the underlying memory.
 */

 NvU32 NvRmMemPin( 
    NvRmMemHandle hMem );

 /**
  * A multiple handle version of NvRmMemPin to reduce kernel trap overhead.
  * 
  * @see NvRmMemPin
  *
  * @param hMems An array of memory handles to pin
  * @param Addrs An arary of address (the result of the pin)
  * @param Count The number of handles and addresses
  */

 void NvRmMemPinMult( 
    NvRmMemHandle * hMems,
    NvU32 * Addrs,
    NvU32 Count );

/**
 * Retrieves a physical address for an hMem handle and an offset into that
 * handle's memory buffer.
 *
 * If the memory referred to by hMem is not pinned, the return value is
 * undefined, and an assert will fire in a debug build.
 *
 * @see NvRmMemPin()
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param Offset The offset into the memory buffer for which the
 *     address is desired.
 *
 * @returns The physical address of the specified byte within the specified
 *     memory handle's storage.  If the memory is mapped through the GART, the
 *     GART address will be returned, not the address of the underlying memory.
 */

 NvU32 NvRmMemGetAddress( 
    NvRmMemHandle hMem,
    NvU32 Offset );

/**
 * Unpins a memory buffer so that it is once again free to be moved.  Pins are
 * reference counted, so the memory will not become movable until all Pin calls
 * have had a matching Unpin call.
 *
 * If the pin count is already zero when this API is called, the behavior is
 * undefined, and an assert will fire in a debug build.
 *
 * This API cannot fail.
 *
 * @see NvRmMemPin()
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 *     If hMem is NULL, this API will do nothing.
 */

 void NvRmMemUnpin( 
    NvRmMemHandle hMem );

 /**
  * A multiple handle version of NvRmMemUnpin to reduce kernel trap overhead.
  * 
  * @see NvRmMemPin
  *
  * @param hMems An array of memory handles to unpin
  * @param Count The number of handles and addresses
  */

 void NvRmMemUnpinMult( 
    NvRmMemHandle * hMems,
    NvU32 Count );

/**
 * Attempts to map a memory buffer into the process's virtual address space.
 *
 * It is recommended that mappings be short-lived as some systems have a limited
 * number of concurrent mappings that can be supported, or because virtual
 * address space may be scarce.
 *
 * It is legal to have multiple concurrent mappings of a single memory buffer.
 *
 * Pinning and mapping a memory buffer are completely orthogonal.  It is not
 * necessary to pin a buffer before mapping it.  Mapping a buffer does not imply
 * that it is pinned.
 *
 * There is no guarantee that the mapping will succeed.  For example, on some
 * operating systems, the OS's security mechanisms make it impossible for
 * untrusted applications to map certain types of memory.  A mapping might also
 * fail due to exhaustion of memory or virtual address space.  Therefore, you
 * must implement code paths that can handle mapping failures.  For example, if
 * the mapping fails, you may want to fall back to using NvRmMemRead() and
 * NvRmMemWrite().  Alternatively, you may want to consider avoiding the use of
 * this API altogether, unless there is a compelling reason why you need
 * mappings.
 *
 * Using Flags argument, memory buffer allocated as Uncached/WriteCombined can
 * be mapped as WriteBack/InnerWriteBack.
 * Once it is mapped as WriteBack/InnerWriteBack, entire memory buffer is
 * converted to be WriteBack/InnerWriteBack buffer. It is an irreversible
 * operation. NvRmMemUnmap operation wouldn't restore the memory back
 * to Uncached/WriteCombined. Ensure that no previous mappings exists,
 * when an Uncached/WriteCombined memory buffer is chosen to map as
 * WriteBack/InnerWriteBack.
 * If WriteBack/InnerWriteBack flags are not specified in Flags, the coherency
 * is same as alloc time specified coherency.
 *
 * @see NvRmMemUnmap()
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param Offset Byte offset within the memory buffer to start the map at.
 * @param Size Size in bytes of mapping requested.  Must be greater than 0.
 * @param Flags Special flags -- use NVOS_MEM_* (see nvos.h for details)
 * @param pVirtAddr If the mapping is successful, provides a virtual
 *     address through which the memory buffer can be accessed.
 *
 * @retval NvSuccess Indicates that the memory was successfully mapped.
 * @retval NvError_InsufficientMemory Out of memory.
 * @retval NvError_NotSupported Mapping not allowed (e.g., for GART heap)
 * @retval NvError_BadParameter Invalid argument(s).
 * @retval NvError_KernelDriverNotFound Can access nvmap driver.
 * @retval NvError_InvalidAddress Ioctl Failed.
 */

NvError
NvRmMemMap(
    NvRmMemHandle  hMem,
    NvU32          Offset,
    NvU32          Size,
    NvU32          Flags,
    void          **pVirtAddr);

/**
 * Unmaps a memory buffer from the process's virtual address space.  This API
 * cannot fail.
 *
 * If hMem is NULL, this API will do nothing.
 * If pVirtAddr is NULL, this API will do nothing.
 *
 * @see NvRmMemMap()
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param pVirtAddr The virtual address returned by a previous call to
 *     NvRmMemMap with hMem.
 * @param Size The size in bytes of the mapped region.  Must be the same as the
 *     Size value originally passed to NvRmMemMap.
 */

void NvRmMemUnmap(NvRmMemHandle hMem, void *pVirtAddr, NvU32 Size);

/**
 * Reads 8 bits of data from a buffer.  This API cannot fail.
 *
 * If hMem refers to an unallocated memory buffer, this function's behavior is
 * undefined and an assert will trigger in a debug build. 
 * 
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param Offset Byte offset relative to the base of hMem.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 *
 * @returns The value read from the memory location.
 */
NvU8 NvRmMemRd08(NvRmMemHandle hMem, NvU32 Offset);

/**
 * Reads 16 bits of data from a buffer.  This API cannot fail.
 *
 * If hMem refers to an unallocated memory buffer, this function's behavior is
 * undefined and an assert will trigger in a debug build. 
 * 
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param Offset Byte offset relative to the base of hMem.
 *     Must be a multiple of 2.
 *
 * @returns The value read from the memory location.
 */
NvU16 NvRmMemRd16(NvRmMemHandle hMem, NvU32 Offset);

/**
 * Reads 32 bits of data from a buffer.  This API cannot fail.
 *
 * If hMem refers to an unallocated memory buffer, this function's behavior is
 * undefined and an assert will trigger in a debug build. 
 * 
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param Offset Byte offset relative to the base of hMem.
 *     Must be a multiple of 4.
 *
 * @returns The value read from the memory location.
 */
NvU32 NvRmMemRd32(NvRmMemHandle hMem, NvU32 Offset);

/**
 * Writes 8 bits of data to a buffer.  This API cannot fail.
 *
 * If hMem refers to an unallocated memory buffer, this function's behavior is
 * undefined and an assert will trigger in a debug build. 
 * 
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param Offset Byte offset relative to the base of hMem.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param Data The data to write to the memory location.
 */
void NvRmMemWr08(NvRmMemHandle hMem, NvU32 Offset, NvU8 Data);

/**
 * Writes 16 bits of data to a buffer.  This API cannot fail.
 *
 * If hMem refers to an unallocated memory buffer, this function's behavior is
 * undefined and an assert will trigger in a debug build. 
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param Offset Byte offset relative to the base of hMem.
 *     Must be a multiple of 2.
 * @param Data The data to write to the memory location.
 */
void NvRmMemWr16(NvRmMemHandle hMem, NvU32 Offset, NvU16 Data);

/**
 * Writes 32 bits of data to a buffer.  This API cannot fail.
 *
 * If hMem refers to an unallocated memory buffer, this function's behavior is
 * undefined and an assert will trigger in a debug build. 
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param Offset Byte offset relative to the base of hMem.
 *     Must be a multiple of 4.
 * @param Data The data to write to the memory location.
 */
void NvRmMemWr32(NvRmMemHandle hMem, NvU32 Offset, NvU32 Data);

/**
 * Reads a block of data from a buffer.  This API cannot fail.
 *
 * If hMem refers to an unallocated memory buffer, this function's behavior is
 * undefined and an assert will trigger in a debug build. 
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param Offset Byte offset relative to the base of hMem.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param pDst The buffer where the data should be placed.
 *     May be arbitrarily aligned -- need not be located at a word boundary.
 * @param Size The number of bytes of data to be read.
 *     May be arbitrarily sized -- need not be a multiple of 2 or 4.
 */
void NvRmMemRead(NvRmMemHandle hMem, NvU32 Offset, void *pDst, NvU32 Size);

/**
 * Writes a block of data to a buffer.  This API cannot fail.
 *
 * If hMem refers to an unallocated memory buffer, this function's behavior is
 * undefined and an assert will trigger in a debug build. 
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param Offset Byte offset relative to the base of hMem.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param pSrc The buffer to obtain the data from.
 *     May be arbitrarily aligned -- need not be located at a word boundary.
 * @param Size The number of bytes of data to be written.
 *     May be arbitrarily sized -- need not be a multiple of 2 or 4.
 */
void NvRmMemWrite(
    NvRmMemHandle hMem,
    NvU32 Offset,
    const void *pSrc,
    NvU32 Size);

/**
 * Reads a strided series of blocks of data from a buffer.  This API cannot
 * fail.
 *
 * The total number of bytes copied is Count*ElementSize.
 *
 * If hMem refers to an unallocated memory buffer, this function's behavior is
 * undefined and an assert will trigger in a debug build. 
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate.
 * @param Offset Byte offset relative to the base of hMem.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param SrcStride The number of bytes separating each source element.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param pDst The buffer where the data should be placed.
 *     May be arbitrarily aligned -- need not be located at a word boundary.
 * @param DstStride The number of bytes separating each destination element.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param ElementSize The number of bytes in each element.
 *     May be arbitrarily sized -- need not be a multiple of 2 or 4.
 * @param Count The number of destination elements.
 */
void NvRmMemReadStrided(
    NvRmMemHandle hMem,
    NvU32 Offset,
    NvU32 SrcStride,
    void *pDst,
    NvU32 DstStride,
    NvU32 ElementSize,
    NvU32 Count);

/**
 * Writes a strided series of blocks of data to a buffer.  This API cannot
 * fail.
 *
 * The total number of bytes copied is Count*ElementSize.
 *
 * If hMem refers to an unallocated memory buffer, this function's behavior is
 * undefined and an assert will trigger in a debug build. 
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate.
 * @param Offset Byte offset relative to the base of hMem.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param DstStride The number of bytes separating each destination element.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param pSrc The buffer to obtain the data from.
 *     May be arbitrarily aligned -- need not be located at a word boundary.
 * @param SrcStride The number of bytes separating each source element.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param ElementSize The number of bytes in each element.
 *     May be arbitrarily sized -- need not be a multiple of 2 or 4.
 * @param Count The number of source elements.
 */
void NvRmMemWriteStrided(
    NvRmMemHandle hMem,
    NvU32 Offset,
    NvU32 DstStride,
    const void *pSrc,
    NvU32 SrcStride,
    NvU32 ElementSize,
    NvU32 Count);

/**
 * Moves (copies) a block of data to a different (or the same) hMem.  This
 * API cannot fail.  Overlapping copies are supported.
 * 
 * NOTE: While easy to use, this is NOT the fastest way to copy memory.  Using
 * the 2D engine to perform a blit can be much faster than this function.
 *
 * If hDstMem or hSrcMem refers to an unallocated memory buffer, this function's
 * behavior is undefined and an assert will trigger in a debug build. 
 *
 * @param hDstMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param DstOffset Byte offset relative to the base of hMem.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param hSrcMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param SrcOffset Byte offset relative to the base of hMem.
 *     May be arbitrarily aligned -- need not be a multiple of 2 or 4.
 * @param Size The number of bytes of data to be copied from hSrcMem to hDstMem.
 *     May be arbitrarily sized -- need not be a multiple of 2 or 4.
 */

 void NvRmMemMove( 
    NvRmMemHandle hDstMem,
    NvU32 DstOffset,
    NvRmMemHandle hSrcMem,
    NvU32 SrcOffset,
    NvU32 Size );

/**
 * Optionally writes back and/or invalidates a range of the memory from the
 * data cache, if applicable.  Does nothing for memory that was not allocated
 * as cached. Memory must be mapped into the calling process.
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param pMapping Starting address (must be within the mapped region of the
       hMem) to clean
 * @param Size The number of bytes of data to be written.
 *     May be arbitrarily sized -- need not be a multiple of 2 or 4.
 */

void NvRmMemCacheMaint(
    NvRmMemHandle hMem,
    void         *pMapping,
    NvU32         Size,
    NvBool        WriteBack,
    NvBool        Invalidate);

/**
 * Get the size of the buffer associated with a memory handle.
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 *
 * @returns Size in bytes of memory allocated for this handle.
 */

 NvU32 NvRmMemGetSize( 
    NvRmMemHandle hMem );

/**
 * Get the alignment of the buffer associated with a memory handle.
 *
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 *
 * @returns Alignment in bytes of memory allocated for this handle.
 */

 NvU32 NvRmMemGetAlignment( 
    NvRmMemHandle hMem );

/**
 * Queries the maximum cache line size (in bytes) for all of the caches
 * L1 and L2 in the system
 *
 * @returns The largest cache line size of the system
 */

 NvU32 NvRmMemGetCacheLineSize( 
    void  );

/**
 * Queries for the heap type associated with a given memory handle.  Also
 * returns base physical address for the buffer, if the type is carveout or
 * GART.  For External type, this parameter does not make sense.
 * 
 * @param hMem A memory handle returned from NvRmMemHandleCreate/FromId.
 * @param BasePhysAddr Output parameter receives the physical address of the
 *           buffer.
 *
 * @returns The heap type allocated for this memory handle.
 */

 NvRmHeap NvRmMemGetHeapType( 
    NvRmMemHandle hMem,
    NvU32 * BasePhysAddr );

/**
 * Dynamically allocates memory, on CPU this will result in a call to 
 * NvOsAlloc and on AVP, memAPI's are used to allocate memory.
 * @param size The memory size to be allocated.
 * @returns Pointer to the allocated buffer.
 */
void* NvRmHostAlloc(size_t Size);

/**
 * Frees a dynamic memory allocation, previously allocated using NvRmHostAlloc.
 * 
 * @param ptr The pointer to buffer which need to be deallocated.
 */
void NvRmHostFree(void* ptr);

/** 
 * This is generally not a publically available function.  It is only available
 * on WinCE to the nvrm device driver.  Attempting to use this function will
 * result in a linker error, you should use NvRmMemMap instead, which will do
 * the "right" thing for all platforms.
 *
 * Under WinCE NvRmMemMap has a custom marshaller, the custom marshaller will
 * do the following:
 *  - Allocate virtual space
 *  - ioctl to the nvrm driver
 *    - nvrm driver will create a mapping from the allocated buffer to
 *      the newly allocated virtual space.
 *
 * @retval NvSuccess Indicates the memory buffer was successfully
 *     mapped.
 * @retval NvError_BadParameter Invalid argument(s).
 * @retval NvError_AccessDenied Operation not permitted.
 * @retval NvError_IoctlFailed Unknown error.
 */
NvError NvRmMemMapIntoCallerPtr(
    NvRmMemHandle hMem,
    void  *pCallerPtr,
    NvU32 Offset,
    NvU32 Size);

/**
 * Create a unique identifier which can be used from any process/processor
 * to generate a new memory handle.  This can be used to share a memory handle
 * between processes, or from AVP and CPU.  
 *
 *  Typical usage would be
 *    GetId
 *    Pass Id to client process/procssor
 *    Client calls:  NvRmMemHandleFromId
 *
 *  See Also NvRmMemHandleFromId
 *
 * NOTE: Getting an id _does not_ increment the reference count of the
 *       memory handle.  You must be sure that whichever process/processor
 *       that is passed an Id calls @NvRmMemHandleFromId@ before you free
 *       a handle.
 *
 * @param hMem The memory handle to retrieve the id for.
 * @returns a unique id that identifies the memory handle.
 */

 NvU32 NvRmMemGetId( 
    NvRmMemHandle hMem );

/**
 * Create a new memory handle, which refers to the memory handle identified
 * by @id@.  This function will increment the reference count on the handle.
 *
 *  See Also NvRmMemGetId
 *
 * @param id value that refers to a memory handle, returned from NvRmMemGetId
 * @param hMem The newly created memory handle
 * @retval NvSuccess Handle creation from Id is successful.
 * @retval NvError_AccessDenied Operation not permitted.
 * @retval NvError_InsufficientMemory Out of memory.
 * @retval NvError_BadParameter Invalid argument(s).
 */

 NvError NvRmMemHandleFromId( 
    NvU32 id,
    NvRmMemHandle * hMem );

/**
 * Get a memory statistics value.
 *
 * Querying values may have an effect on  system performance and may include
 * processing, like heap traversal.
 *
 * @param Stat NvRmMemStat value that chooses the value to return.
 * @param Result Result, if the call was successful. Otherwise value
 *      is not touched.
 * @returns NvSuccess on success.
 * @retval NvError_BadParameter Invalid argument(s).
 * @retval NvError_NotSupported Stat is not available or not supported.
 */

 NvError NvRmMemGetStat( 
    NvRmMemStat Stat,
    NvS32 * Result );

#define NVRM_MEM_CHECK_ID  0
#define NVRM_MEM_TRACE     0
#if     NVRM_MEM_TRACE
#ifndef NV_IDL_IS_STUB
#ifndef NV_IDL_IS_DISPATCH
#define NvRmMemHandleCreate(d,m,s) \
        NvRmMemHandleCreateTrace(d,m,s,__FILE__,__LINE__)
#define NvRmMemHandleFree(m) \
        NvRmMemHandleFreeTrace(m,__FILE__,__LINE__)
#define NvRmMemGetId(m) \
        NvRmMemGetIdTrace(m,__FILE__,__LINE__)
#define NvRmMemHandleFromId(i,m) \
        NvRmMemHandleFromIdTrace(i,m,__FILE__,__LINE__)

static NV_INLINE NvError NvRmMemHandleCreateTrace( 
    NvRmDeviceHandle hDevice,
    NvRmMemHandle * phMem,
    NvU32 Size,
    const char *file,
    NvU32 line)
{
    NvError err;
    err = (NvRmMemHandleCreate)(hDevice, phMem, Size);
    NvOsDebugPrintf("RMMEMTRACE: Create %08x at %s:%d %s\n",
        (int)*phMem,
        file,
        line,
        err?"FAILED":"");
    return err;
}

static NV_INLINE void NvRmMemHandleFreeTrace( 
    NvRmMemHandle hMem,
    const char *file,
    NvU32 line)
{
    NvOsDebugPrintf("RMMEMTRACE: Free   %08x at %s:%d\n",
        (int)hMem,
        file,
        line);
    (NvRmMemHandleFree)(hMem);
}

static NV_INLINE NvU32 NvRmMemGetIdTrace( 
    NvRmMemHandle hMem,
    const char *file,
    NvU32 line)
{
    NvOsDebugPrintf("RMMEMTRACE: GetId  %08x at %s:%d\n",
        (int)hMem,
        file,
        line);
    return (NvRmMemGetId)(hMem);
}

static NV_INLINE NvError NvRmMemHandleFromIdTrace( 
    NvU32 id,
    NvRmMemHandle * hMem,
    const char *file,
    NvU32 line)
{
    NvOsDebugPrintf("RMMEMTRACE: FromId %08x at %s:%d\n",
        id,
        file,
        line);
    return (NvRmMemHandleFromId)(id,hMem);
}

#endif // NV_IDL_IS_DISPATCH
#endif // NV_IDL_IS_STUB
#endif // NVRM_MEM_TRACE

/** @} */

#if defined(__cplusplus)
}
#endif

#endif
