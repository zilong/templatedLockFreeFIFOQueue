#ifndef __RINGBUFFERTYPES__ 
#define __RINGBUFFERTYPES__ 1
   enum RingBufferType { Heap, SharedMemory, TCP, Infinite };
   enum Direction { Producer, Consumer };
#endif
