#include <cstdlib>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdint>
#include <unistd.h>
#include <array>
#include <sstream>
#include <fstream>
#include <cassert>
#include <cinttypes>
#include <cmath>

#include "procwait.hpp"
#include "ringbuffer.tcc"
#include "SystemClock.tcc"
#include "randomstring.tcc"
#include "signalvars.hpp"

#define MAX_VAL 100000000



struct Data
{
   Data( std::int64_t send ) : send_count(  send ),
                               start_time( 0 ),
                               end_time( 0 )
   {}
   std::int64_t          send_count;
   double                start_time;
   double                end_time;
} data( MAX_VAL );


//#define USESharedMemory 1
#define USELOCAL 1
#define BUFFSIZE 100

#ifdef USESharedMemory
typedef RingBuffer< std::int64_t, 
                    RingBufferType::SharedMemory, 
                    false > TheBuffer;
#elif defined USELOCAL
typedef RingBuffer< std::int64_t          /* buffer type */,
                    RingBufferType::Heap  /* allocation type */,
                    false                 /* turn off monitoring */ >  TheBuffer;
#endif


Clock *system_clock = new SystemClock< System >( 1 );


void
producer( Data &data, TheBuffer &buffer )
{
   std::int64_t current_count( 0 );
#if LIMITRATE   
   const float serviceTime( 10e-6 );
#endif   
   data.start_time = system_clock->getTime();
   while( current_count++ < data.send_count )
   {
      auto &ref( buffer.allocate() );
      ref = current_count;
      buffer.push( /*current_count,*/
         (current_count == data.send_count ? 
          RBSignal::RBEOF : RBSignal::NONE ) );
#if LIMITRATE
      const auto endTime( serviceTime + system_clock->getTime() );
      while( endTime > system_clock->getTime() );
#endif
   }
   return;
}

void 
consumer( Data &data, TheBuffer &buffer )
{
   std::int64_t   current_count( 0 );
#if LIMITRATE   
   const float serviceTime( 5e-6 );
#endif   
   RBSignal signal( RBSignal::NONE );
   while( signal != RBSignal::RBEOF )
   {
      buffer.pop( current_count, &signal );
#if LIMITRATE
      const auto endTime( serviceTime + system_clock->getTime() );
      while( endTime > system_clock->getTime() );
#endif
   }
   data.end_time = system_clock->getTime();
   assert( current_count == MAX_VAL );
   return;
}

std::string test()
{
   double total_seconds( 0.0 );
#ifdef USESharedMemory
   char shmkey[ 256 ];
   SHM::GenKey( shmkey, 256 );
   std::string key( shmkey );
   ProcWait *proc_wait( new ProcWait( 1 ) ); 
   const pid_t child( fork() );
   double start( 0.0 );
   switch( child )
   {
      case( 0 /* CHILD */ ):
      {
         TheBuffer buffer_b( BUFFSIZE,
                             key, 
                             Direction::Consumer, 
                             false);
         /** call consumer function directly **/
         consumer( buffer_b );
         exit( EXIT_SUCCESS );
      }
      break;
      case( -1 /* failed to fork */ ):
      {
         std::cerr << "Failed to fork, exiting!!\n";
         exit( EXIT_FAILURE );
      }
      break;
      default: /* parent */
      {
          
         proc_wait->AddProcess( child );
         TheBuffer buffer_a( BUFFSIZE,
                             key, 
                             Direction::Producer, 
                             false);
         start = system_clock->getTime();
         /** call producer directly **/
         producer( data, buffer_a );
      }
   }
  
   /** parent waits for child **/
   proc_wait->WaitForChildren();
   const auto end( system_clock->getTime() );
   total_seconds = (end - start);
   delete( proc_wait );
   
#elif defined USELOCAL
   TheBuffer buffer( BUFFSIZE );
   std::thread a( producer, 
                  std::ref( data ), 
                  std::ref( buffer ) );

   std::thread b( consumer,
                  std::ref( data ),
                  std::ref( buffer ) );
   a.join();
   b.join();
   total_seconds = ( data.end_time - data.start_time );
#endif
   std::stringstream ss;
   ss << "Time: " << total_seconds << "s\n";
   ss << "Rate: " << (( MAX_VAL * sizeof( std::int64_t ) ) / std::pow(2,20) ) / total_seconds << " MB/s\n";
   ss << "\n";
   return( ss.str() );
}


int 
main( int argc, char **argv )
{
   //RandomString< 50 > rs;
   //const std::string root( "/project/mercury/svardata/" );
   //const std::string root( "" );
   //std::ofstream ofs( root + rs.get() + ".csv" );
   //if( ! ofs.is_open() )
   //{
   //   std::cerr << "Couldn't open ofstream!!\n";
   //   exit( EXIT_FAILURE );
   //}
   int runs( 10 );
   while( runs-- )
   {
       std::cout << test() << "\n";
   }
   //ofs.close();
   if( system_clock != nullptr ) 
      delete( system_clock );
}

