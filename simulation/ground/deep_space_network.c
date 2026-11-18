/*
 * deep_space_network.c - SIMULATION of Earth: the Deep Space Network and the ground station
 *
 * Not software of Pathfinder. A program of its own, at the other end of the radio link:
 * make ground starts it (make ground GUI=1: with the ground station window). The lander
 * (make lander) and Earth can start in any order: until the frames arrive, Earth just
 * listens.
 *
 * The radio link: UDP on this computer (simulation/vehicle/transponder.h). The frames of the
 * lander arrive on LINK_DOWNLINK_PORT, the commands go up to LINK_UPLINK_PORT.
 *
 * Downlink: every frame the radio transmits arrives here. The telemetry packets carry only
 * numbers: like a real ground station, Earth turns them into text. On the terminal it keeps
 * a panel, redrawn in place at every frame: the last value of each channel and the last
 * events. And it sends every packet over UDP to the ground station (ground_station.py) on
 * this computer, one UDP packet per telemetry packet:
 *   PATHFINDER t=<ms> kind=<kind> v=<v0>,<v1>,<v2>,<v3> text=<text until the end>
 * Before the telemetry packets of each frame, a "downlink" UDP packet: v0 = the data rate of
 * the frame (0 low, 1 medium, 2 high), text = its name.
 *
 * Uplink: the ground station sends the commands of the operator over UDP, one packet each:
 *   PATHFINDER-COMMAND rate=<0 low, 1 medium, 2 high>
 *   PATHFINDER-COMMAND patch=<a line of C for the shell of the lander>
 * A patch goes up at the rate of the uplink, slowly: meanwhile Earth sends the ground
 * station "uplink" packets, v0 = the percentage sent. When the lander has run a patch, the
 * reply of its shell comes in a frame of the system (RADIO_CHANNEL_SYSTEM): Earth sends a
 * "patch" packet, v0 = patches run, v1 = the result (an enum shell_result).
 *
 * A reset of the lander: its frames stop for a while, then the first frame after the boot
 * is the boot report of the system: Earth sends a "reset" packet, v0 = resets so far, v1 =
 * the bus cycle distribution did not complete.
 *
 * Mission time: the clock of the lander starts again from 0 at every boot. Earth keeps a
 * clock of its own, the mission time: at a boot report it adds the time that has passed on
 * Earth. Every t sent to the ground station is mission time. A lander started again from
 * power on (make lander) begins a new mission: mission time from 0.
 */
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "telemetry.h"			// the format of the packets
#include "radio.h"				// the layout of the frame
#include "transponder.h"		// the radio link: struct link_frame, the commands
#include "system.h"				// the results of the patches: enum shell_result

#define GROUND_STATION_PORT  5006		// we send the telemetry here
#define COMMAND_PORT         5007		// the ground station sends the commands here

/* Name of each APID, as the ground station knows it, in the order of enum tlm_apid */
static const char *apid_name[] = {
	"accelerometers", "altimeter", "asi_met", "timing", "deadline", "imp"
};

/*
 * Name of each task of the lander, by its ID. The ground station has its own copy of the
 * IDs of the task table (flight_software/schedule.h), agreed with the lander team
 */
static const char *task_name[] = {
	"SCHEDULER", "DISTRIBUTION", "ACCEL", "ALTIMETER", "COMMS", "ASI/MET"
};
#define TASKS  6						// tasks in the table above

/* Name of each data rate, in the order of enum data_rate */
static const char *data_rate_name[] = {
	"LOW", "MEDIUM", "HIGH"
};

static int sock;						// UDP socket towards the ground station
static struct sockaddr_in ground_station;	// its address: 127.0.0.1, GROUND_STATION_PORT

static int command_sock;				// UDP socket for the commands of the ground station

static int downlink_sock;				// the antenna: the frames of the lander arrive here
static int uplink_sock;					// the antenna: the commands go up from here
static struct sockaddr_in lander;		// 127.0.0.1, LINK_UPLINK_PORT

/*
 * The panel on the terminal: the last text of each channel, and the last events (resets,
 * deadline misses, commands sent), the newest first. The uplink task adds events too: the
 * lock protects them
 */
#define PANEL_EVENTS  6

static char last_text[TLM_IMP + 1][128];	// by APID ("" = nothing yet)
static float worst_ms = -1;					// distribution: worst response in the last frame
static float deadline_ms = 0;
static unsigned int resets = 0;
static char events[PANEL_EVENTS][160];
static long int last_t_ms = 0;				// time of the last frame on the lander
static pthread_mutex_t events_lock = PTHREAD_MUTEX_INITIALIZER;
static int panel_lines = 0;					// lines of the panel on the terminal (0 = none yet)

/* Mission time = time of the lander + base. Only the downlink task uses them */
static long int base_ms = 0;
static long int last_frame_t_ms = -1;			// the time of the last frame, on the lander (-1 = none)
static long int last_frame_mission_ms = 0;		// the same, in mission time
static struct timespec last_frame_arrival;		// when it arrived, on Earth


/* Why the shell of the lander rejected a patch, in the order of enum shell_result */
static const char *shell_result_name[] = {
	"done", "syntax error", "unknown variable", "bad value"
};

void* uplink_task(void* arg);
void* downlink_task(void* arg);


/* The text of a telemetry packet, from its numbers */
static void packet_text(struct tlm_packet *packet, char *text, size_t size)
{
	float *v = packet->value;

	if (packet->apid == TLM_ACCELEROMETERS && v[0] != 0)
		snprintf(text, size, "ACCEL        : %.3f %.3f %.3f m/s^2", v[1], v[2], v[3]);
	else if (packet->apid == TLM_ACCELEROMETERS)
		snprintf(text, size, "ACCEL        : accelerometers not read");
	else if (packet->apid == TLM_ALTIMETER && v[0] != 0)
		snprintf(text, size, "ALTIMETER    : altitude %.0f cm", v[1]);
	else if (packet->apid == TLM_ALTIMETER)
		snprintf(text, size, "ALTIMETER    : not read (off)");
	else if (packet->apid == TLM_ASI_MET)
		snprintf(text, size, "ASI/MET      : %.0f C %.0f Pa", v[0], v[1]);
	else if (packet->apid == TLM_TIMING)
		snprintf(text, size, "DISTRIBUTION : response time %.1f ms (deadline %.0f ms)",
		         v[0], v[1]);
	else if (packet->apid == TLM_DEADLINE && v[0] >= 0 && v[0] < TASKS)
		snprintf(text, size, "%-12s : deadline missed, %.0f ms > %.0f ms", task_name[(int) v[0]], v[1], v[2]);
	else if (packet->apid == TLM_IMP)
		snprintf(text, size, "IMP          : image %.0f compressed in %.1f s", v[0], v[1] / 1000);
	else
		snprintf(text, size, "unknown packet");
}

/* A new event at the top of the list: the oldest one goes */
static void add_event(long int t_ms, const char *text)
{
	pthread_mutex_lock(&events_lock);
	for (int i = PANEL_EVENTS - 1; i > 0; i--)
		strcpy(events[i], events[i - 1]);
	snprintf(events[0], sizeof(events[0]), "%8.1f s  %s", t_ms / 1000.0, text);
	pthread_mutex_unlock(&events_lock);
}

/*
 * The panel, redrawn in place: the cursor goes back up over the previous one, and each line
 * is cleared and written again (ANSI codes). Every line is cut to the width of the terminal,
 * so that it takes exactly one row: a longer line would wrap, the panel would be taller, and
 * going back up would not reach its top. If the output is not a terminal (a file), the panel
 * is simply added after the previous one
 */
static void draw_panel(struct downlink_frame *frame, long int t_ms, enum data_rate rate)
{
	static const char *channel[] = { "ACCEL", "ALTIMETER", "ASI/MET", "IMP" };
	static const enum tlm_apid channel_apid[] = { TLM_ACCELEROMETERS, TLM_ALTIMETER, TLM_ASI_MET, TLM_IMP };
	char line[12 + PANEL_EVENTS][200];		// the lines of the panel: 12, plus the events
	int lines = 0;
	int terminal = isatty(STDOUT_FILENO);
	int width = sizeof(line[0]);			// the longest line that fits in a row of the terminal
	struct winsize window;

	snprintf(line[lines++], sizeof(line[0]), "EARTH  -  frame %u, built at %.1f s, %s data rate",
	         frame->number, t_ms / 1000.0, data_rate_name[rate]);
	snprintf(line[lines++], sizeof(line[0]), " ");
	for (int i = 0; i < 4; i++) {
		if (last_text[channel_apid[i]][0] != '\0')
			snprintf(line[lines++], sizeof(line[0]), "  %s", last_text[channel_apid[i]]);
		else
			snprintf(line[lines++], sizeof(line[0]), "  %-12s : -", channel[i]);
	}
	if (worst_ms >= 0)
		snprintf(line[lines++], sizeof(line[0]), "  DISTRIBUTION : worst response %.1f ms (deadline %.0f ms)",
		         worst_ms, deadline_ms);
	else
		snprintf(line[lines++], sizeof(line[0]), "  DISTRIBUTION : -");
	snprintf(line[lines++], sizeof(line[0]), "  SYSTEM RESETS: %u", resets);
	snprintf(line[lines++], sizeof(line[0]), " ");
	snprintf(line[lines++], sizeof(line[0]), "  last events:");
	pthread_mutex_lock(&events_lock);
	for (int i = 0; i < PANEL_EVENTS; i++)
		snprintf(line[lines++], sizeof(line[0]), "  %.160s", events[i]);
	pthread_mutex_unlock(&events_lock);
	snprintf(line[lines++], sizeof(line[0]), " ");
	snprintf(line[lines++], sizeof(line[0]), "  q + Enter to quit");

	if (terminal) {
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0 && window.ws_col > 1)
			width = window.ws_col - 1;		// one column less: a full row could wrap
		if (panel_lines > 0)
			printf("\033[%dA", panel_lines);	// back to the top of the previous panel
		for (int i = 0; i < lines; i++)
			printf("\033[2K%.*s\n", width, line[i]);
	}
	else {
		for (int i = 0; i < lines; i++)
			printf("%s\n", line[i]);
	}
	panel_lines = lines;
	fflush(stdout);
}

/*
 * One line to the ground station: a telemetry packet of the lander, or "downlink", made by
 * Earth at each frame (value[0] = the data rate)
 */
static void send_to_ground_station(long int t_ms, const char *kind, const float *value, const char *text)
{
	char packet[256];
	int len;

	len = snprintf(packet, sizeof(packet), "PATHFINDER t=%ld kind=%s v=%g,%g,%g,%g text=%s",
	               t_ms, kind, value[0], value[1], value[2], value[3], text);
	sendto(sock, packet, len, 0, (struct sockaddr *) &ground_station, sizeof(ground_station));
}

/*
 * Uplink task: waits for the commands of the ground station and transmits them to the lander.
 * Until Earth quits.
 * arg: the socket where the commands arrive.
 */
void* uplink_task(void* arg){

	int from_ground_station = *(int *) arg;

	char packet[256];
	char text[200];
	ssize_t len;
	struct command_msg command;
	long int t_ms;							// the time of the lander, as Earth last saw it
	long int duration_us;					// the time the patch takes to go up
	long int sent_us;
	struct timespec step = { 0, 250000000 };	// the progress, every 0.25 s
	float progress[4] = { 0, 0, 0, 0 };

	while (1) {

		len = recv(from_ground_station, packet, sizeof(packet) - 1, 0);
		if (len > 0) {
			packet[len] = '\0';
			pthread_mutex_lock(&events_lock);
			t_ms = last_t_ms;
			pthread_mutex_unlock(&events_lock);

			if (sscanf(packet, "PATHFINDER-COMMAND rate=%d", &command.rate) == 1
			    && command.rate >= DATA_RATE_LOW && command.rate <= DATA_RATE_HIGH) {
				command.kind = COMMAND_SET_DATA_RATE;
				snprintf(text, sizeof(text), "EARTH        : uplink, set the data rate to %s",
				         data_rate_name[command.rate]);
				add_event(t_ms, text);					// on the panel, at the next frame
				sendto(uplink_sock, &command, sizeof(command), 0, (struct sockaddr *) &lander, sizeof(lander));
			}
			else if (strncmp(packet, "PATHFINDER-COMMAND patch=", 25) == 0) {
				command.kind = COMMAND_PATCH;
				snprintf(command.patch, sizeof(command.patch), "%.95s", packet + 25);
				snprintf(text, sizeof(text), "EARTH        : uplink, patch: %s", command.patch);
				add_event(t_ms, text);

				// The patch goes up at the rate of the uplink: its bits, plus 16 bytes of the
				// frame of the command. The ground station sees the progress
				duration_us = (strlen(command.patch) + 16) * 8 * 1000000L / UPLINK_BITS_PER_S;
				for (sent_us = 0; sent_us < duration_us; sent_us += 250000) {
					progress[0] = sent_us * 100.0f / duration_us;
					send_to_ground_station(t_ms, "uplink", progress, "patch going up");
					nanosleep(&step, NULL);
				}
				progress[0] = 100;
				send_to_ground_station(t_ms, "uplink", progress, "patch sent");
				sendto(uplink_sock, &command, sizeof(command), 0, (struct sockaddr *) &lander, sizeof(lander));
			}
		}
	}
}

/*
 * A frame from the transponder: its bytes. Every frame has the same length, so a frame of
 * another length is not ours. Earth takes it apart with the layout of the lander
 * (struct downlink_frame, radio.h)
 */
static void frame_received(const void *bytes, size_t size, enum data_rate rate)
{
	struct downlink_frame frame;	// the frame, out of the bytes received
	struct system_report report = { 0 };	// a frame of the system: its report
	int system_frame = 0;			// 1 = a frame of the system, report filled
	struct tlm_packet packet;		// one packet of the frame
	char text[128];					// its text
	unsigned int offset;			// where it starts, in the bytes of the frame
	struct timespec now;
	long int t_ms;					// the frame, in mission time

	if (size == sizeof(frame)) {
		memcpy(&frame, bytes, sizeof(frame));
		if (frame.channel == RADIO_CHANNEL_SYSTEM && frame.n_bytes >= sizeof(report)) {
			memcpy(&report, frame.data, sizeof(report));
			system_frame = 1;
		}

		// 1. Mission time. After a boot the clock of the lander starts again from 0: what
		//    has passed on Earth since the last frame goes into the base. If the clock of the
		//    lander goes back without a boot report, it was started again: a new mission
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (system_frame && report.kind == SYSTEM_REPORT_BOOT && last_frame_t_ms >= 0)
			base_ms = last_frame_mission_ms
			          + (now.tv_sec - last_frame_arrival.tv_sec) * 1000
			          + (now.tv_nsec - last_frame_arrival.tv_nsec) / 1000000
			          - frame.t_ms;
		else if (frame.t_ms < last_frame_t_ms)
			base_ms = 0;
		t_ms = frame.t_ms + base_ms;
		last_frame_t_ms = frame.t_ms;
		last_frame_mission_ms = t_ms;
		last_frame_arrival = now;

		// 2. Tell the ground station a frame has arrived, and at which data rate
		float downlink[4] = { rate, 0, 0, 0 };
		send_to_ground_station(t_ms, "downlink", downlink, data_rate_name[rate]);
		pthread_mutex_lock(&events_lock);
		last_t_ms = t_ms;
		pthread_mutex_unlock(&events_lock);

		// 3. A frame of the system (system.h): the boot report after a reset, or the reply
		//    of the shell to a patch
		if (system_frame && report.kind == SYSTEM_REPORT_BOOT) {
			resets = report.resets;
			snprintf(text, sizeof(text), "LANDER       : boot after SYSTEM RESET #%u, cycle %u",
			         report.resets, report.cycle);
			add_event(t_ms, text);
			float reset[4] = { report.resets, report.cycle, 0, 0 };
			send_to_ground_station(t_ms, "reset", reset, text);
		}
		if (system_frame && report.kind == SYSTEM_REPORT_SHELL) {
			if (report.result == SHELL_OK)
				snprintf(text, sizeof(text), "LANDER       : patch done");
			else if (report.result > SHELL_OK && report.result <= SHELL_BAD_VALUE)
				snprintf(text, sizeof(text), "LANDER       : patch rejected, %s",
				         shell_result_name[report.result]);
			else
				snprintf(text, sizeof(text), "LANDER       : patch, unknown result");
			add_event(t_ms, text);
			float patched[4] = { report.lines, report.result, 0, 0 };
			send_to_ground_station(t_ms, "patch", patched, text);
		}

		// 4. A frame of the flight software: take the telemetry packets out of the bytes, one
		//    after the other (telemetry.h), turn them into text, keep them for the panel and
		//    send them to the ground station
		if (frame.channel == RADIO_CHANNEL_FLIGHT) {
			worst_ms = -1;
			offset = 0;
			while (offset + sizeof(packet) <= frame.n_bytes) {
				memcpy(&packet, &frame.data[offset], sizeof(packet));
				packet.t_ms += base_ms;						// mission time
				if (packet.apid >= TLM_ACCELEROMETERS && packet.apid <= TLM_IMP) {
					packet_text(&packet, text, sizeof(text));
					if (packet.apid == TLM_TIMING) {
						if (packet.value[0] > worst_ms)
							worst_ms = packet.value[0];
						deadline_ms = packet.value[1];
					}
					else if (packet.apid == TLM_DEADLINE) {
						add_event(packet.t_ms, text);
					}
					else {
						strcpy(last_text[packet.apid], text);
					}
					send_to_ground_station(packet.t_ms, apid_name[packet.apid], packet.value, text);
				}
				offset += sizeof(packet);
			}
		}

		// 5. The panel on the terminal
		draw_panel(&frame, t_ms, rate);
	}
}

/*
 * Downlink task: waits for the frames of the lander, until Earth quits. A datagram that is
 * not a frame of the link, or has a data rate Earth does not know, is not ours: ignored
 */
void* downlink_task(void* arg){

	(void) arg;
	struct link_frame waves;
	ssize_t len;

	while (1) {
		len = recv(downlink_sock, &waves, sizeof(waves), 0);
		if (len >= (ssize_t) offsetof(struct link_frame, bytes)
		    && waves.size == len - offsetof(struct link_frame, bytes)
		    && waves.rate >= DATA_RATE_LOW && waves.rate <= DATA_RATE_HIGH)
			frame_received(waves.bytes, waves.size, waves.rate);
	}
}

/* A UDP socket bound to a port of this computer, or -1 */
static int listen_on(int port)
{
	struct sockaddr_in address;
	int sock_listen = socket(AF_INET, SOCK_DGRAM, 0);

	memset(&address, 0, sizeof(address));
	address.sin_family      = AF_INET;
	address.sin_port        = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(sock_listen, (struct sockaddr *) &address, sizeof(address)) != 0) {
		close(sock_listen);
		sock_listen = -1;
	}

	return sock_listen;
}

int main(void)
{
	pthread_t thread_uplink;
	pthread_t thread_downlink;
	int c;

	// 1. The antennas, towards the lander: the frames come down here, the commands go up there
	downlink_sock = listen_on(LINK_DOWNLINK_PORT);
	uplink_sock = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&lander, 0, sizeof(lander));
	lander.sin_family      = AF_INET;
	lander.sin_port        = htons(LINK_UPLINK_PORT);
	lander.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	// 2. The ground station: the telemetry goes there, its commands come here
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&ground_station, 0, sizeof(ground_station));
	ground_station.sin_family      = AF_INET;
	ground_station.sin_port        = htons(GROUND_STATION_PORT);
	ground_station.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	command_sock = listen_on(COMMAND_PORT);

	if (downlink_sock < 0 || command_sock < 0) {
		fprintf(stderr, "Earth: cannot listen on ports %d and %d: is Earth already running?\n",
		        LINK_DOWNLINK_PORT, COMMAND_PORT);
		return 1;
	}

	// 3. Listen to the lander, and to the commands of the ground station
	pthread_create(&thread_downlink, NULL, downlink_task, NULL);
	pthread_create(&thread_uplink, NULL, uplink_task, &command_sock);
	printf("EARTH  -  listening for the lander (make lander)\n");
	fflush(stdout);

	// 4. Until the operator quits
	do {
		c = getchar();
	} while (c != 'q' && c != EOF);

	// 5. Stop listening (recv is a cancellation point; the uplink may be in nanosleep, one too)
	pthread_cancel(thread_downlink);
	pthread_cancel(thread_uplink);
	pthread_join(thread_downlink, NULL);
	pthread_join(thread_uplink, NULL);
	close(downlink_sock);
	close(uplink_sock);
	close(command_sock);
	close(sock);

	return 0;
}
