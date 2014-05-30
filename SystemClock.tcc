/**
 * SystemClock.hpp - 
 * @author: Jonathan Beard
 * @version: Sun Apr 20 15:41:37 2014
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
#ifndef _SYSTEMCLOCK_HPP_
#define _SYSTEMCLOCK_HPP_  1
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <pthread.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

#ifdef __linux
#include <time.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <sched.h>
#endif


/**
 * TODO:
 * 1) Add new constructor for allocating in SHM
 * 2) Add accessor for getting SHM Key to open
 * 3) Add accessor to open SHM for new processes 
 *    to use the counter.
 * 4) Assess the resolution of the mach time methods
 */

enum ClockType  { Dummy, Cycle, System };

typedef long double sclock_t;

template < ClockType T > class SystemClock {
public:
   SystemClock() : updater( 0 )
   {
      errno = 0;
      if( pthread_create( &updater, 
                          nullptr, 
                          updateClock, 
                          (void*) &thread_data ) != 0 )
      {
         perror( "Failed to create timer thread, exiting." );
         exit( EXIT_FAILURE );
      }
   }

   virtual ~SystemClock()
   {
      thread_data.done = true;
      pthread_join( updater, nullptr );
   }

   
   sclock_t getTime()
   {
      return( thread_data.clock->read() );
   }



private:
   class Clock {
   public:
      Clock() : a( 0.0 ),
                b( 0.0 )
      {}
      
      void increment()
      {
         a++;
         b++;
      }

      void increment( sclock_t inc )
      {
         a += inc;
         b += inc;
      }

      sclock_t read()
      {
         struct{
            sclock_t a;
            sclock_t b;
         } copy;
         do{
            copy.a = a;
            copy.b = b;
         }while( copy.a != copy.b );
         return( copy.b );
      }

   private:
      volatile sclock_t a;
      volatile sclock_t b;
   };

   struct ThreadData{
      ThreadData() : clock( nullptr ),
                     done(  false )
      {
         clock = new Clock();
      }

      ~ThreadData()
      {
         delete( clock );
         clock = nullptr;
         done = true;
      }

      Clock         *clock;
      volatile bool done;
   } thread_data ;

   /**
    * updateClock - function called by thread to update global clock,
    */
   static void* updateClock( void *data )
   {
      ThreadData *d( reinterpret_cast< ThreadData* >( data ) );
      Clock         *clock( d->clock );
      volatile bool &done(   d->done );
      std::function< void ( Clock* ) > function;
      switch( T )
      {
         case( Dummy ):
         {
            function = []( Clock *clock ){ clock->increment(); };
         }
         break;
         case( Cycle ):
         {
#ifdef   __linux
            FILE  *fp = NULL;
            errno = 0;
            fp = fopen("/proc/cpuinfo", "r");
            if(fp == NULL)
            {
               perror( "Failed to read proc/cpuinfo!!\n" );
               exit( EXIT_FAILURE );
            }
            const size_t buff_size = 20;
            char *key = (char*) alloca(sizeof(char) * buff_size);
            char *value = (char*) alloca(sizeof(char) * buff_size);
            assert( key != NULL );
            assert( value != NULL );
            std::memset( key, '\0', buff_size );
            std::memset( value, '\0', buff_size );
            uint64_t frequency( 0 );
            int count = EOF;
            while( ( count = fscanf(fp,"%[^:]:%[^\n]\n", key, value) ) != EOF )
            {
               if( count == 2 ){
                  /* TODO, not the best way to get CPU Frequency */
                  if( strncmp( key, "cpu MHz", 7 ) == 0 )
                  {
                     frequency = ( uint64_t ) (atof( value ) * 1e6f );
                     goto END;
                  }
               }
            }
         END:   
            fclose( fp );
                     
            /**
             * pin the current thread 
             */
            cpu_set_t   *cpuset( nullptr );
            const int8_t   processors_to_allocate( 1 );
            size_t cpu_allocate_size( -1 );
#if   (__GLIBC_MINOR__ > 9 ) && (__GLIBC__ == 2 )
            cpuset = CPU_ALLOC( processors_to_allocate );
            assert( cpuset != nullptr );
            cpu_allocate_size = CPU_ALLOC_SIZE( processors_to_allocate );
            CPU_ZERO_S( cpu_allocate_size, cpuset );
#else
            cpu_allocate_size = sizeof( cpu_set_t );
            cpuset = (cpu_set_t*) malloc( cpu_allocate_size );
            //TODO maybe shouldn't be an assert, but more graceful
            assert( cpuset != nullptr );
            CPU_ZERO( cpuset );
#endif
            /** TODO, make configurable **/
            const uint32_t assigned_processor( 0 );
            CPU_SET( assigned_processor,
                     cpuset );
            errno = 0;
            if( sched_setaffinity( 0 /* calling thread */,
                                  cpu_allocate_size,
                                  cpuset ) != 0 )
            {
               perror( "Failed to set affinity for cycle counter!!" );
               exit( EXIT_FAILURE );
            }

            uint64_t current(  0 );
            uint64_t previous( 0 );
            /** begin assembly section to init previous **/
#ifdef   __x86_64
            uint64_t highBits = 0x0, lowBits = 0x0;
            __asm__ volatile("\
               lfence                           \n\
               rdtsc                            \n\
               movq     %%rax, %[low]           \n\
               movq     %%rdx, %[high]"          
               :
               /*outputs here*/
               [low]    "=r" (lowBits),
               [high]   "=r" (highBits)
               :
               /*inputs here*/
               :
               /*clobbered registers*/
               "rax","rdx"
            );
            previous = (lowBits & 0xffffffff) | (highBits << 32); 
#elif    __ARMEL__

#elif    __ARMHF__

#else
#warning    Cycle counter not supported on this architecture
#endif

            function = [&]( Clock *clock )
            {
               /** begin assembly sections **/
#ifdef   __x86_64
               uint64_t highBits = 0x0, lowBits = 0x0;
               __asm__ volatile("\
                  lfence                           \n\
                  rdtsc                            \n\
                  movq     %%rax, %[low]           \n\
                  movq     %%rdx, %[high]"          
                  :
                  /*outputs here*/
                  [low]    "=r" (lowBits),
                  [high]   "=r" (highBits)
                  :
                  /*inputs here*/
                  :
                  /*clobbered registers*/
                  "rax","rdx"
               );
               current = (lowBits & 0xffffffff) | (highBits << 32); 
#elif    __ARMEL__

#elif    __ARMHF__

#else
#warning    Cycle counter not supported on this architecture
#endif
               const uint64_t diff( current - previous );
               previous = current;
               /* convert to seconds for increment */
               const sclock_t seconds( (sclock_t) diff / (sclock_t) frequency );
               clock->increment( seconds );
            };
#else
#warning    Cycle counter currently supported for Linux only
#endif
         }
         break;
         case( System ):
         {
#ifdef   __linux
            struct timespec curr_time;
            struct timespec prev_time;
            std::memset( &curr_time, 0, sizeof( struct timespec ) ); 
            std::memset( &prev_time, 0, sizeof( struct timespec ) ); 
            if( clock_gettime( CLOCK_REALTIME, &prev_time ) != 0 )
            {
               perror( "Failed to get initial time." );
            }
            function = [&]( Clock *clock )
            {
               errno = 0;
               if( clock_gettime( CLOCK_REALTIME, &curr_time ) != 0 )
               {
                  perror( "Failed to get current time!" );
               }
               const struct timespec diff( 
                  { .tv_sec  = curr_time.tv_sec  - prev_time.tv_sec,
                    .tv_nsec = curr_time.tv_nsec - prev_time.tv_nsec } );
               prev_time = curr_time;
               /* update global time */
               const sclock_t seconds( 
                     (sclock_t ) diff.tv_sec + ( ( sclock_t ) diff.tv_nsec * 1.0e-9 ) );
               clock->increment( seconds );
            };
#elif defined __APPLE__
            uint64_t  current( 0 );
            uint64_t  previous( 0 );
            /** init **/
            previous = mach_absolute_time();
            static mach_timebase_info_data_t sTimebaseInfo;
            if( sTimebaseInfo.denom == 0 )
            {
               (void) mach_timebase_info( &sTimebaseInfo );
            }
            function = [&]( Clock *clock )
            {
               current = mach_absolute_time();
               const uint64_t diff( current - previous );
               previous = current;
               /** 
                * TODO, fix this, there's gotta be a better way
                * figure out what units the return val is in.
                */
                
                const uint64_t elapsedNano( diff * sTimebaseInfo.numer / sTimebaseInfo.denom );
                
                const sclock_t seconds( (sclock_t) elapsedNano * 1.0e-9 );
                clock->increment( seconds );
            };
#endif
         }
         break;
         default:
         {
            assert( false );
         }
         break;
      }
      while( ! done )
      {
         function( clock );
      }
      pthread_exit( nullptr );
   }

   pthread_t         updater;

};
#endif /* END _SYSTEMCLOCK_HPP_ */