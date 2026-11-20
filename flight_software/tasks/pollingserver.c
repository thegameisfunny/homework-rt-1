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
 * Polling server per il task non real-time IMP.
 *
 * Il server e' un task periodico (POLLING_SERVER_PERIOD) con capacita' POLLING_SERVER_CAPACITY
 * (th->wcet), a priorita' POLLING_SERVER_PRIORITY assegnata con Deadline Monotonic.
 * Il lavoro aperiodico resta nel codice di IMP (imp.c): il server lo "serve" prestandogli la
 * propria priorita' e la propria capacita'.
 *
 * Ad ogni attivazione la capacita' viene ricaricata e il server controlla (polling) se IMP ha
 * una richiesta sulla POLLING_QUEUE:
 * - se non ci sono richieste, la capacita' del periodo va persa;
 * - se c'e' una richiesta, il server sblocca IMP (ACTIVATION_QUEUE) e lo porta a SCHED_FIFO,
 *   a IMP_SERVED_PRIORITY (subito sotto il server). Ogni POLLING_SERVER_CHECK misura il tempo
 *   di CPU consumato da IMP (orologio CPU del thread IMP) e lo riporta a SCHED_OTHER quando:
 *     a) la capacita' e' esaurita (il lavoro residuo continua al periodo successivo),
 *     b) IMP ha terminato il lavoro (ha gia' inviato la richiesta successiva), oppure
 *     c) il periodo sta per finire.
 * Fuori dalla capacita' IMP resta un normale task SCHED_OTHER (non real-time): puo' avanzare
 * solo quando la CPU non serve ai task real-time.
 *
 * Il server non si blocca mai sulle code: sono aperte in modalita' non bloccante.
 */

/* Tempo di CPU consumato finora dal thread con orologio 'clock' [ns] */
static long cpu_ns(clockid_t clock)
{
	struct timespec t;

	clock_gettime(clock, &t);
	return t.tv_sec * 1000000000L + t.tv_nsec;
}

/* Imposta la politica di scheduling di IMP */
static void imp_set_scheduling(pthread_t imp, int policy, int priority)
{
	struct sched_param param = { .sched_priority = priority };
	int error = pthread_setschedparam(imp, policy, &param);

	if (error != 0) {
		errno = error;
		perror("pollingserver: pthread_setschedparam (imp)");
	}
}

/* true se il prossimo controllo cadrebbe oltre la fine del periodo corrente */
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
		imp_set_scheduling(imp, SCHED_FIFO, IMP_SERVED_PRIORITY);//passo a sched fifo da sched other

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

		// 4. Fine del servizio in questo periodo: IMP torna non real-time
		imp_set_scheduling(imp, SCHED_OTHER, 0);
	}
	return NULL;
}
