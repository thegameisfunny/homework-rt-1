#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <time.h>
#include "timing.h"
#include "workload.h"
#include "telemetry.h"
#include "tasks.h"

/*
polling server per gestire imp (che di base non e' rt)

gira ogni POLLING_SERVER_PERIOD, ha budget POLLING_SERVER_CAPACITY  e priorità POLLING_SERVER_PRIORITY.
il task resta comunque in imp.c e il server gli presta il tempo di calcolo e lo alza di priorita' quando viene servito.
appena si sveglia ricarica il budget, replenishment time infatti corrisponde al periodo, e butta un occhio sulla POLLING_QUEUE:
se è vuota non deve fare niente nel periodo e quindi smette, se c'è qualche richiesta allora lo segnalo a imp su ACTIVATION_QUEUE e lo schedulo con SCHED_FIFO a priorità IMP_SERVED_PRIORITY.
da li' ogni POLLING_SERVER_CHECK controlla dal cpu clock quanto ha consumato imp e lo rimette a SCHED_OTHER se finisce il budget, se imp non ha finito resta semplicemente in background fermo al suo punto, se imp ha gia' finito allora manda la richiesta.
Dobbiamo aggiungere un modo per controllare quando sta per finire il periodo altrimenti ci ritroviamo che il server smette di eseguire e il task resta in SCHED_FIFO.
fuori dal budget imp torna un normalissimo thread SCHED_OTHER, quindi gira solo
quando i task rt lasciano la cpu libera, possiamo provare a implementare un blocco totale ma non è un problema il fatto che giri quando i task rt hanno finito.
aprimao tutte code non bloccanti perche' il server non deve aspettare.
*/

//Tempo di CPU consumato finora dal thread con orologio 'clock' [ns]
static long cpu_ns(clockid_t clock)
{
	struct timespec t;

	clock_gettime(clock, &t);
	return t.tv_sec * 1000000000L + t.tv_nsec;
}

//Imposta la politica di scheduling di IMP
static void imp_set_scheduling(pthread_t imp, int policy, int priority)
{
	struct sched_param param = { .sched_priority = priority };
	int error = pthread_setschedparam(imp, policy, &param);

	if (error != 0) {
		errno = error;
		perror("pollingserver: pthread_setschedparam (imp)");
	}
}

//true se il prossimo controllo cadrebbe oltre la fine del periodo corrente
static bool period_ending(struct periodic_thread *th)
{
	struct timespec now;
	long left_us;

	clock_gettime(CLOCK_REALTIME, &now);
	left_us = (th->r.tv_sec - now.tv_sec) * 1000000L + (th->r.tv_nsec - now.tv_nsec) / 1000;
	return left_us <= POLLING_SERVER_CHECK;
}

void* pollingserver_task(void* arg){

	struct pollingserver_str *pollingserver = (struct pollingserver_str *) arg;
	struct periodic_thread *th = &pollingserver->th;
	pthread_t imp = *pollingserver->imp_thread;

	bool request;
	bool serving = false;		// IMP sta eseguendo una richiesta
	long capacity_ns = th->wcet * 1000L;
	long start_ns;				// CPU di IMP all'inizio del servizio in questo periodo
	clockid_t imp_clock;		// orologio del tempo di CPU di IMP
	struct mq_attr attr;
	struct timespec check = { 0, POLLING_SERVER_CHECK * 1000L };

	if (pthread_getcpuclockid(imp, &imp_clock) != 0) {
		perror("pollingserver: pthread_getcpuclockid");
		pthread_exit(NULL);
	}

	// Richieste di IMP: lette senza bloccarsi
	mqd_t polling_queue;
	if ((polling_queue = mq_open(POLLING_QUEUE, O_RDONLY | O_NONBLOCK)) == (mqd_t)-1) {
		perror("pollingserver: mq_open (polling)");
		pthread_exit(NULL);
	}

	// Attivazione di IMP: inviata senza bloccarsi
	mqd_t activation_queue;
	if ((activation_queue = mq_open(ACTIVATION_QUEUE, O_WRONLY | O_NONBLOCK)) == (mqd_t)-1) {
		perror("pollingserver: mq_open (activation)");
		pthread_exit(NULL);
	}

	start_periodic_timer(th, POLLING_SERVER_OFFSET);

	while (1) {

		// 1. Attivazione periodica: la capacita' viene ricaricata
		wait_next_activation(th);

		// 2. Polling: se IMP non e' gia' servito, si controlla se ha una nuova richiesta
		if (!serving && mq_receive(polling_queue, (char *)&request, sizeof(request), 0) > 0) {
			if (mq_send(activation_queue, (const char *)&request, sizeof(request), 0) == -1)
				perror("pollingserver: Error sending activation");
			else
				serving = true;
		}

		// Nessuna richiesta: la capacita' di questo periodo va persa
		if (!serving)
			continue;	//salta l'iterazione del while e torna a wait_next_activation(th)

		// 3. Servizio: IMP esegue alla priorita' del server, dentro la capacita'
		start_ns = cpu_ns(imp_clock);
		imp_set_scheduling(imp, SCHED_FIFO, IMP_SERVED_PRIORITY);//passo allo scheduling fifo rt

		while (1) {
			clock_nanosleep(CLOCK_MONOTONIC, 0, &check, NULL);	// qui gira IMP

			// a) capacita' esaurita
			if (cpu_ns(imp_clock) - start_ns >= capacity_ns)
				break;

			// b) lavoro terminato: IMP ha gia' inviato la richiesta successiva
			if (mq_getattr(polling_queue, &attr) == 0 && attr.mq_curmsgs > 0) {
				serving = false;
				break;
			}

			// c) il periodo sta per finire
			if (period_ending(th))
				break;
		}

		// 4. Fine del servizio in questo periodo: IMP torna non real-time o va in idle se deve attendere di essere servito di nuovo
		/*
		if (serving){
			imp_set_scheduling(imp, SCHED_IDLE, 0); SI POTREBBE ANCHE BLOCCARE COMPLETAMENTE IL THREAD IMP, MA NON E' NECESSARIO, SE HA TEMPO PER ESEGUIRE NON CI INFASTIDISCE. Non essendo RT non disturba i task RT.
		}
		else{
			imp_set_scheduling(imp, SCHED_OTHER, 0);
		}
		*/
		imp_set_scheduling(imp, SCHED_OTHER, 0);
	}
	return NULL;
}
