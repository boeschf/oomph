#include <iostream>
#include <vector>

#include <ghex/common/timer.hpp>


#ifdef USE_MPI

/* MPI backend */
#include <ghex/transport_layer/callback_communicator.hpp>
#include <ghex/transport_layer/mpi/communicator.hpp>
using CommType = gridtools::ghex::tl::communicator<gridtools::ghex::tl::mpi_tag>;
#else

/* UCX backend */
#include <ghex/transport_layer/ucx/callback_communicator.hpp>
#include <ghex/transport_layer/ucx/communicator.hpp>
using CommType = gridtools::ghex::tl::communicator<gridtools::ghex::tl::ucx_tag>;
#endif /* USE_MPI */

using MsgType = gridtools::ghex::tl::shared_message_buffer<>;

/* comm requests currently in-flight */
int ongoing_comm = 0;

void send_callback(MsgType mesg, int rank, int tag)
{
    // std::cout << "send callback called " << rank << " thread " << omp_get_thread_num() << " tag " << tag << "\n";
    ongoing_comm--;
}

void recv_callback(MsgType mesg, int rank, int tag)
{
    // std::cout << "recv callback called " << rank << " thread " << omp_get_thread_num() << " tag " << tag << "\n";
    ongoing_comm--;
}

int main(int argc, char *argv[])
{
    int rank, size, threads, peer_rank;
    int niter, buff_size;
    int inflight;

#ifdef USE_MPI
    int mode;
#ifdef THREAD_MODE_MULTIPLE
    MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &mode);
    if(mode != MPI_THREAD_MULTIPLE){
	std::cerr << "MPI_THREAD_MULTIPLE not supported by MPI, aborting\n";
	std::terminate();
    }
#else
    MPI_Init_thread(NULL, NULL, MPI_THREAD_SINGLE, &mode);
#endif
#endif

    gridtools::ghex::tl::callback_communicator<CommType> comm;

    niter = atoi(argv[1]);
    buff_size = atoi(argv[2]);
    inflight = atoi(argv[3]);   
    
    rank = comm.m_rank;
    size = comm.m_size;
    peer_rank = (rank+1)%2;
    
    if(rank==0)	std::cout << "\n\nrunning test " << __FILE__ << " with communicator " << typeid(comm).name() << "\n\n";

    {
	gridtools::ghex::timer timer;
	long bytes = 0;
	std::vector<MsgType> msgs;

	for(int j=0; j<inflight; j++){
	    msgs.emplace_back(buff_size);
	}
	
	if(rank == 1) {
	    timer.tic();
	    bytes = (double)niter*size*buff_size/2;
	}

	if(rank == 0){

	    /* send niter messages - as soon as a slot becomes free */
	    int sent = 0;
	    while(sent != niter){

		for(int j=0; j<inflight; j++){
		    if(msgs[j].use_count() == 1){
			if(rank==0 && (sent)%(niter/10)==0) {
			    std::cout << sent << " iters\n";
			}
			sent++;
			ongoing_comm++;
			comm.send(msgs[j], peer_rank, j, send_callback);
			if(sent==niter) break;
		    }
		}
		if(sent==niter) break;
	    
		/* NOTE: this seems to be needed for large inflight */
		/* progress a bit: for large inflight values this yields better performance */
		/* over simply calling the progress once */
		int p = 0.1*inflight-1;
		do {
		    p-=comm.progress();
		} while(ongoing_comm && p>0);
	    }

	} else {

	    /* recv requests are resubmitted as soon as a request is completed */
	    /* so the number of submitted recv requests is always constant (inflight) */
	    /* expect niter messages (i.e., niter recv callbacks) on receiver  */
	    ongoing_comm = niter;
	    while(ongoing_comm){

		for(int j=0; j<inflight; j++){
		    if(msgs[j].use_count() == 1){
			comm.recv(msgs[j], peer_rank, j, recv_callback);
		    }
		}
	    
		/* NOTE: this seems to be needed for large inflight */
		/* progress a bit: for large inflight values this yields better performance */
		/* over simply calling the progress once */
		int p = 0.1*inflight-1;
		do {
		    p-=comm.progress();
		} while(ongoing_comm && p>0);
	    }
	}

	/* complete all comm */
	while(ongoing_comm){
	    comm.progress();
	}

	comm.flush();
	comm.barrier();
	
	if(rank == 1) timer.vtoc(bytes);
    }

#ifdef USE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
#endif
}
