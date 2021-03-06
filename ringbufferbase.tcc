/**
 * ringbufferbase.tcc - 
 * @author: Jonathan Beard
 * @version: Thu May 15 09:06:52 2014
 * 
 * Copyright 2014 Jonathan Beard
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _RINGBUFFERBASE_TCC_
#define _RINGBUFFERBASE_TCC_  1

#include <array>
#include <cstdlib>
#include <cassert>
#include <thread>
#include <cstring>
#include <iostream>
#include "Clock.hpp"
#include "pointer.hpp"
#include "ringbuffertypes.hpp"
#include "bufferdata.tcc"
#include "signalvars.hpp"
#include "blocked.hpp"

/**
 * Note: there is a NICE define that can be uncommented
 * below if you want sched_yield called when waiting for
 * writes or blocking for space, otherwise blocking will
 * actively spin while waiting.
 */
#define NICE 1

extern Clock *system_clock;


template < class T, 
           RingBufferType type > class RingBufferBase {
public:
   /**
    * RingBuffer - default constructor, initializes basic
    * data structures.
    */
   RingBufferBase() : data( nullptr ),
                      allocate_called( false ),
                      write_finished( false )
   {
   }
   
   virtual ~RingBufferBase()
   {
   }


   /**
    * size - as you'd expect it returns the number of 
    * items currently in the queue.
    * @return size_t
    */
   size_t   size()
   {
      const auto   wrap_write( Pointer::wrapIndicator( data->write_pt  ) ),
                   wrap_read(  Pointer::wrapIndicator( data->read_pt   ) );

      const auto   wpt( Pointer::val( data->write_pt ) ), 
                   rpt( Pointer::val( data->read_pt  ) );
      if( wpt == rpt )
      {
         if( wrap_read < wrap_write )
         {
            return( data->max_cap );
         }
         else if( wrap_read > wrap_write )
         {
            /**
             * TODO, this condition is momentary, however there
             * is a better way to fix this with atomic operations...
             * or on second thought benchmarking shows the atomic
             * operations slows the queue down drastically so, perhaps
             * this is in fact the best of all possible returns.
             */
            return( data->max_cap  );
         }
         else
         {
            return( 0 );
         }
      }
      else if( rpt < wpt )
      {
         return( wpt - rpt );
      }
      else if( rpt > wpt )
      {
         return( data->max_cap - rpt + wpt ); 
      }
      return( 0 );
   }

   

   /**
    * space_avail - returns the amount of space currently
    * available in the queue.  This is the amount a user
    * can expect to write without blocking
    * @return  size_t
    */
    size_t   space_avail()
   {
      return( data->max_cap - size() );
   }
  
   /**
    * capacity - returns the capacity of this queue which is 
    * set at compile time by the constructor.
    * @return size_t
    */
   size_t   capacity() const
   {
      return( data->max_cap );
   }

   /**
    * allocate - get a reference to an object of type T at the 
    * end of the queue.  Should be released to the queue using
    * the push command once the calling thread is done with the 
    * memory.
    * @return T&, reference to memory location at head of queue
    */
   T& allocate()
   {
      while( space_avail() == 0 )
      {
#ifdef NICE      
         std::this_thread::yield();
#endif         
#if __x86_64
         __asm__ volatile("\
           pause"
           :
           :
           : );
#endif           
      }
      (this)->allocate_called = true;
      const size_t write_index( Pointer::val( data->write_pt ) );
      return( data->store[ write_index ].item );
   }

   /**
    * push - releases the last item allocated by allocate() to
    * the queue.  Function will imply return if allocate wasn't
    * called prior to calling this function.
    * @param signal - const RBSignal signal, default: NONE
    */
   void push( const RBSignal signal = RBSignal::NONE )
   {
      if( ! (this)->allocate_called ) return;
      const size_t write_index( Pointer::val( data->write_pt ) );
      data->signal[ write_index ].sig = signal;
      Pointer::inc( data->write_pt );
      if( signal == RBSignal::RBEOF )
      {
         (this)->write_finished = true;
      }
      (this)->allocate_called = false;
   }

   /**
    * push- writes a single item to the queue, blocks
    * until there is enough space.
    * @param   item, T
    */
   void  push( T &item, const RBSignal signal = RBSignal::NONE )
   {
      while( space_avail() == 0 )
      {
#ifdef NICE      
         std::this_thread::yield();
#endif         
#if __x86_64
         __asm__ volatile("\
           pause"
           :
           :
           : );
#endif           
      }
      
	   const size_t write_index( Pointer::val( data->write_pt ) );
	   data->store[ write_index ].item     = item;
	   data->signal[ write_index ].sig   = signal;
	   Pointer::inc( data->write_pt );
      if( signal == RBSignal::RBEOF )
      {
         (this)->write_finished = true;
      }
   }
   
   /**
    * insert - inserts the range from begin to end in the queue,
    * blocks until space is available.  If the range is greater than
    * available space on the queue then it'll simply add items as 
    * space becomes available.  There is the implicit assumption that
    * another thread is consuming the data, so eventually there will
    * be room.
    * @param   begin - iterator_type, iterator to begin of range
    * @param   end   - iterator_type, iterator to end of range
    */
   template< class iterator_type >
   void insert(   iterator_type begin, 
                  iterator_type end, 
                  const RBSignal signal = RBSignal::NONE )
   {
      while( begin != end )
      {
         while( space_avail() == 0 )
         {
#ifdef NICE
            std::this_thread::yield();
#endif
         }
         const size_t write_index( Pointer::val( data->write_pt ) );
         data->store[ write_index ].item = (*begin);
         
         /** add signal to last el only **/
         if( begin == ( end - 1 ) )
         {
            data->signal[ write_index ].sig = signal;
         }
         else
         {
            data->signal[ write_index ].sig = RBSignal::NONE;
         }
         Pointer::inc( data->write_pt );
         begin++;
      }
      if( signal == RBSignal::RBEOF )
      {
         (this)->write_finished = true;
      }
   }

  
   /**
    * pop - read one item from the ring buffer,
    * will block till there is data to be read
    * @return  T, item read.  It is removed from the
    *          q as soon as it is read
    */
   void 
   pop( T &item, RBSignal *signal = nullptr )
   {
      while( size() == 0 )
      {
#ifdef NICE      
         std::this_thread::yield();
#endif        
#if __x86_64
         __asm__ volatile("\
           pause"
           :
           :
           : );
#endif           
      }
      const size_t read_index( Pointer::val( data->read_pt ) );
      if( signal != nullptr )
      {
         *signal = data->signal[ read_index ].sig;
      }
      item = data->store[ read_index ].item;
      Pointer::inc( data->read_pt );
   }

   /**
    * pop_range - pops a range and returns it as a std::array.  The
    * exact range to be popped is specified as a template parameter.
    * the static std::array was chosen as its a bit faster, however 
    * this might change in future implementations to a std::vector
    * or some other structure.
    */
   template< size_t N >
   void  pop_range( std::array< T, N > &output, 
                    std::array< RBSignal, N > *signal = nullptr )
   {
      while( size() < N )
      {
#ifdef NICE
         std::this_thread::yield();
#endif
      }
     
      size_t read_index;
      if( signal != nullptr )
      {
         for( size_t i( 0 ); i < N; i++ )
         {
            read_index( Pointer::val( data->read_pt ) );
            output[ i ]       = data->store[ read_index ].item;
            (*signal)[ i ]    = data->signal[ read_index ].sig;
            Pointer::inc( data->read_pt );
         }
      }
      else /** ignore signal **/
      {
         /** TODO, incorporate streaming copy here **/
         for( size_t i( 0 ); i < N; i++ )
         {
            read_index( Pointer::val( data->read_pt ) );
            output[ i ]    = data->store[ read_index ].item;
            Pointer::inc( data->read_pt );
         }

      }
      return;
   }


   /**
    * peek() - look at a reference to the head of the
    * ring buffer.  This doesn't remove the item, but it 
    * does give the user a chance to take a look at it without
    * removing.
    * @return T&
    */
    T& peek(  RBSignal *signal = nullptr )
   {
      while( size() < 1 )
      {
#ifdef NICE      
         std::this_thread::yield();
#endif     
#if  __x86_64   
         __asm__ volatile("\
           pause"
           :
           :
           : );
#endif
      }
      const size_t read_index( Pointer::val( data->read_pt ) );
      if( signal != nullptr )
      {
         *signal = data->signal[ read_index ].sig;
      }
      T &output( data->store[ read_index ].item );
      return( output );
   }


   /**
    * recycle - To be used in conjunction with peek().  Simply
    * removes the item at the head of the queue and discards them
    * @param range - const size_t, default range is 1
    */
   void recycle( const size_t range = 1 )
   {
      assert( range <= data->max_cap );
      Pointer::incBy( range, data->read_pt );
   }

protected:
   /**
    * Buffer structure that is the core of the ring
    * buffer.
    */
   Buffer::Data< T, type>      *data;
   /** 
    * This should be okay outside of the buffer, its local 
    * to the writing thread.  Variable gets set "true" in
    * the allocate function and false when the push with
    * only the signal argument is called.
    */
   volatile bool                allocate_called;
   /** TODO, this needs to get moved into the buffer for SHM **/
   volatile bool                write_finished;
};


/**
 * Infinite / Dummy  specialization 
 */
template < class T > class RingBufferBase< T, RingBufferType::Infinite >
{
public:
   /**
    * RingBuffer - default constructor, initializes basic
    * data structures.
    */
   RingBufferBase() : data( nullptr ),
                      allocate_called( false ),
                      write_finished( false )
   {
   }
   
   virtual ~RingBufferBase()
   {
   }


   /**
    * size - as you'd expect it returns the number of 
    * items currently in the queue.
    * @return size_t
    */
   size_t   size()
   {
      return( 1 );
   }

   RBSignal get_signal() 
   {
#if 0   
      /** 
       * there are two signalling paths, the one 
       * we'll give the highest priority to is the 
       * asynchronous one.
       */
      const auto signal_queue( data->store[ 0 ].signal );
      /**
       * TODO, fix this
       */
      const auto signal_local( (this)->signal_a );
      if( signal_local == RBSignal::NONE )
      {
         return( signal_queue );
      }
      /** there must be something in the local signal **/
      //(this)->signal = RBSignal::NONE;
#endif      
      return( RBSignal::NONE );
   }

   void send_signal( const RBSignal &signal )
   {
      //(this)->signal = signal;
   }

   /**
    * space_avail - returns the amount of space currently
    * available in the queue.  This is the amount a user
    * can expect to write without blocking
    * @return  size_t
    */
    size_t   space_avail()
   {
      return( data->max_cap );
   }

  
   /**
    * capacity - returns the capacity of this queue which is 
    * set at compile time by the constructor.
    * @return size_t
    */
   size_t   capacity() const
   {
      return( data->max_cap );
   }

   
   /**
    * allocate - get a reference to an object of type T at the 
    * end of the queue.  Should be released to the queue using
    * the push command once the calling thread is done with the 
    * memory.
    * @return T&, reference to memory location at head of queue
    */
   T& allocate()
   {
      (this)->allocate_called = true;
      return( data->store[ 0 ].item );
   }

   /**
    * push - releases the last item allocated by allocate() to
    * the queue.  Function will imply return if allocate wasn't
    * called prior to calling this function.
    * @param signal - const RBSignal signal, default: NONE
    */
   void push( const RBSignal signal = RBSignal::NONE )
   {
      if( ! (this)->allocate_called ) return;
      data->signal[ 0 ].sig = signal;
      (this)->allocate_called = false;
   }

   /**
    * push - This version won't write anything, it'll
    * increment the counter and simply return;
    * @param   item, T
    */
   void  push( T &item, const RBSignal signal = RBSignal::NONE )
   {
      data->store [ 0 ].item  = item;
      /** a bit awkward since it gives the same behavior as the actual queue **/
      data->signal[ 0 ].sig  = signal;
   }

   /**
    * insert - insert a range of items into the queue.
    * @param   begin - start iterator
    * @param   end   - ending iterator
    * @param   signal - const RBSignal, set if you want to send a signal
    */
   template< class iterator_type >
   void insert( iterator_type begin, 
                iterator_type end, 
                const RBSignal signal = RBSignal::NONE )
   {
      while( begin != end )
      {
         data->store[ 0 ].item = (*begin);
         begin++;
      }
      data->signal[ 0 ].sig = signal;
   }
 
   /**
    * pop - This version won't return any useful data,
    * its just whatever is in the buffer which should be zeros.
    * @return  T, item read.  It is removed from the
    *          q as soon as it is read
    */
   void pop( T &item, RBSignal *signal = nullptr )
   {
      item  = data->store[ 0 ].item;
      if( signal != nullptr )
      {
         *signal = data->signal[ 0 ].sig;
      }
   }
  
   /**
    * pop_range - dummy function version of the real one above
    * sets the input array to whatever "dummy" data has been
    * passed into the buffer, often real data.  In most cases
    * this will allow the application to work as it would with
    * correct inputs even if the overall output will be junk.  This
    * enables correct measurement of the arrival and service rates.
    * @param output - std:;array< T, N >*
    */
   template< size_t N >
   void  pop_range( 
      std::array< T, N > &output, 
      std::array< RBSignal, N > *signal = nullptr )
   {
      if( signal != nullptr )
      {
         for( size_t i( 0 ); i < N; i++ )
         {
            output[ i ]     = data->store [ 0 ].item;
            (*signal)[ i ]  = data->signal[ 0 ].sig;
         }
      }
      else
      {
         for( size_t i( 0 ); i < N; i++ )
         {
            output[ i ]    = data->store[ 0 ].item;
         }
      }
   }


   /**
    * peek() - look at a reference to the head of the
    * the ring buffer.  This doesn't remove the item, but it 
    * does give the user a chance to take a look at it without
    * removing.
    * @return T&
    */
   T& peek(  RBSignal *signal = nullptr )
   {
      T &output( data->store[ 0 ].item );
      if( signal != nullptr )
      {
         *signal = data->signal[  0  ].sig;
      }
      return( output );
   }
   
   /**
    * recycle - remove ``range'' items from the head of the
    * queue and discard them.  Can be used in conjunction with
    * the peek operator.
    * @param   range - const size_t, default = 1
    */
   void recycle( const size_t range = 1 )
   {
   }

protected:
   /** go ahead and allocate a buffer as a heap, doesn't really matter **/
   Buffer::Data< T, RingBufferType::Heap >      *data;
   volatile bool                                allocate_called;
   volatile bool                                write_finished;
};
#endif /* END _RINGBUFFERBASE_TCC_ */
