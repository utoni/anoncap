#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>
#include <pcap.h>

#include <libgen.h>

#include "parser.h"
#include "crypto.h"

#define P_MAIN "anon"

/** Input file names **/
char *input_pcap_file = NULL;

/** Output dir name **/
char *output_pcap_file = NULL;

/** pcap input pointers **/
pcap_t *in_pcap;
char err[PCAP_ERRBUF_SIZE];

/** pcap output pointers **/
pcap_t *out_pcap;
pcap_dumper_t *pdumper;

/** data link type **/
uint16_t datalink;

/** headers only - discard payload and anonymize only headers **/
uint16_t headers_only = 0;

/** inform libpcap about signals **/
char exit_signal = 0;


/**
 * Usage
 **/
void usage()
{
	fprintf(stderr, "\nUSAGE: %s ", P_MAIN);
	fprintf(stderr, "-r pcap -w pcap [options]\n");
	fprintf(stderr, "   -h         \t Show help (this text)\n");
	fprintf(stderr, "   -r pcap    \t Input pcap file\n");
	fprintf(stderr, "   -w pcap    \t Output pcap file\n");
	fprintf(stderr, "   -p         \t Save only protocol headers (up to L4)\n");
	fprintf(stderr, " \n");
}

/**
 * Program options
 **/
void process_opt(int argc, char *argv[])
{
	int c = 0;
	extern char *optarg;

	while ((c = getopt(argc, argv, "ha:r:w:pa:")) != EOF) {
		switch (c) {
		case 'h':
			usage();
			exit(0);
			break;
		case 'r':
			input_pcap_file = optarg;
			break;
		case 'w':
			output_pcap_file = optarg;
			break;
		case 'p':
			headers_only = 1;
			break;
		default:
			usage();
			abort();
		}
	}
	if (input_pcap_file == NULL || output_pcap_file == NULL) {
		usage();
		abort();
	}
}

/**
 * Tell libpcap to stop
 **/
void process_signals(int signo)
{
	exit_signal = 1;
}


/**
 * pcap callback
 **/
void callback(u_char *args, const struct pcap_pkthdr* hdr, const u_char* pkt)
{
	uint16_t payload = 0;

	struct pcap_pkthdr new_hdr;

	new_hdr.ts.tv_sec = hdr->ts.tv_sec;
	new_hdr.ts.tv_usec = hdr->ts.tv_usec;

	/** parse the packet and calculate its key **/
	payload = anonimize(datalink, hdr, pkt);

	if (headers_only) {
		new_hdr.len = (hdr->caplen - payload);
		payload = 0;
	} else
		new_hdr.len = hdr->caplen;
	new_hdr.caplen = new_hdr.len;

	pcap_dump((u_char*)pdumper, &new_hdr, pkt);

	// should we stop?
	if (exit_signal)
		pcap_breakloop(in_pcap);
}

/**
 * Main
 **/
int main(int argc, char* argv[])
{
	process_opt(argc, argv);

	signal(SIGINT, process_signals);
	signal(SIGTERM, process_signals);
	signal(SIGQUIT, process_signals);

	char* out_file = strdup(output_pcap_file);
	char* out_folder = dirname(out_file);

	initialize_crypto(out_folder);

	/** input pcap file **/
	in_pcap = pcap_open_offline(input_pcap_file, err);
	if (!in_pcap) {
		fprintf(stderr, "Error: cannot open %s for reading (code %s)\n", input_pcap_file, err);
		abort();
	}

	/** output pcap file **/
	out_pcap = pcap_open_dead(1, 65535);
	pdumper = pcap_dump_open(out_pcap, output_pcap_file);

	if (!out_pcap) {
		fprintf(stderr, "Error: cannot open %s for writing (code %s)\n", output_pcap_file, err);
		abort();
	}

	datalink = pcap_datalink(in_pcap);
	pcap_loop(in_pcap, -1, callback, NULL);

	pcap_dump_close(pdumper);

	return 0;
}
