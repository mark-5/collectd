/**
 * collectd - src/traffic.c
 * Copyright (C) 2005,2006  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif

/* One cannot include both. This sucks. */
#if HAVE_LINUX_IF_H
#  include <linux/if.h>
#elif HAVE_NET_IF_H
#  include <net/if.h>
#endif

#if HAVE_LINUX_NETDEVICE_H
#  include <linux/netdevice.h>
#endif
#if HAVE_IFADDRS_H
#  include <ifaddrs.h>
#endif

#define MODULE_NAME "traffic"

#if HAVE_GETIFADDRS || KERNEL_LINUX || HAVE_LIBKSTAT || HAVE_LIBSTATGRAB
# define TRAFFIC_HAVE_READ 1
#else
# define TRAFFIC_HAVE_READ 0
#endif

#define BUFSIZE 512

static char *bytes_file   = "traffic-%s.rrd";
static char *packets_file = "interface-%s/packets.rrd";
static char *errors_file  = "interface-%s/errors.rrd";

static char *bytes_ds_def[] =
{
	"DS:incoming:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:outgoing:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int bytes_ds_num = 2;

static char *packets_ds_def[] =
{
	"DS:rx:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:tx:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int packets_ds_num = 2;

static char *errors_ds_def[] =
{
	"DS:rx:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:tx:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int errors_ds_num = 2;

#ifdef HAVE_LIBKSTAT
#define MAX_NUMIF 256
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMIF];
static int numif = 0;
#endif /* HAVE_LIBKSTAT */

static void traffic_init (void)
{
#if HAVE_GETIFADDRS
	/* nothing */
/* #endif HAVE_GETIFADDRS */

#elif KERNEL_LINUX
	/* nothing */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
	kstat_t *ksp_chain;
	unsigned long long val;

	numif = 0;

	if (kc == NULL)
		return;

	for (numif = 0, ksp_chain = kc->kc_chain;
			(numif < MAX_NUMIF) && (ksp_chain != NULL);
			ksp_chain = ksp_chain->ks_next)
	{
		if (strncmp (ksp_chain->ks_class, "net", 3))
			continue;
		if (ksp_chain->ks_type != KSTAT_TYPE_NAMED)
			continue;
		if (kstat_read (kc, ksp_chain, NULL) == -1)
			continue;
		if ((val = get_kstat_value (ksp_chain, "obytes")) == -1LL)
			continue;
		ksp[numif++] = ksp_chain;
	}
/* #endif HAVE_LIBKSTAT */

#elif HAVE_LIBSTATG
	/* nothing */
#endif /* HAVE_LIBSTATG */

	return;
}

static void generic_write (char *host, char *inst, char *val,
		char *file_template,
		char **ds_def, int ds_num)
{
	char file[512];
	int status;

	status = snprintf (file, BUFSIZE, file_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, ds_def, ds_num);
}

static void bytes_write (char *host, char *inst, char *val)
{
	generic_write (host, inst, val, bytes_file, bytes_ds_def, bytes_ds_num);
}

static void packets_write (char *host, char *inst, char *val)
{
	generic_write (host, inst, val, packets_file, packets_ds_def, packets_ds_num);
}

static void errors_write (char *host, char *inst, char *val)
{
	generic_write (host, inst, val, errors_file, errors_ds_def, errors_ds_num);
}

#if TRAFFIC_HAVE_READ
static void bytes_submit (char *device,
		unsigned long long incoming,
		unsigned long long outgoing)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%lld:%lld", (unsigned int) curtime, incoming, outgoing) >= BUFSIZE)
		return;

	plugin_submit (MODULE_NAME, device, buf);
}

#if HAVE_GETIFADDRS
static void packets_submit (char *dev,
		unsigned long long rx,
		unsigned long long tx)
{
	char buf[512];
	int  status;

	status = snprintf (buf, 512, "%u:%lld:%lld",
			(unsigned int) curtime,
			rx, tx);
	if ((status >= 512) || (status < 1))
		return;
	plugin_submit ("if_packets", dev, buf);
}

static void errors_submit (char *dev,
		unsigned long long rx,
		unsigned long long tx)
{
	char buf[512];
	int  status;

	status = snprintf (buf, 512, "%u:%lld:%lld",
			(unsigned int) curtime,
			rx, tx);
	if ((status >= 512) || (status < 1))
		return;
	plugin_submit ("if_errors", dev, buf);
}
#endif /* HAVE_GETIFADDRS */

static void traffic_read (void)
{
#if HAVE_GETIFADDRS
	struct ifaddrs *if_list;
	struct ifaddrs *if_ptr;

/* Darin/Mac OS X and possible other *BSDs */
#if HAVE_STRUCT_IF_DATA
#  define IFA_DATA if_data
#  define IFA_RX_BYTES ifi_ibytes
#  define IFA_TX_BYTES ifi_obytes
#  define IFA_RX_PACKT ifi_ipackets
#  define IFA_TX_PACKT ifi_opackets
#  define IFA_RX_ERROR ifi_ierrors
#  define IFA_TX_ERROR ifi_oerrors
/* #endif HAVE_STRUCT_IF_DATA */

#elif HAVE_STRUCT_NET_DEVICE_STATS
#  define IFA_DATA net_device_stats
#  define IFA_RX_BYTES rx_bytes
#  define IFA_TX_BYTES tx_bytes
#  define IFA_RX_PACKT rx_packets
#  define IFA_TX_PACKT tx_packets
#  define IFA_RX_ERROR rx_errors
#  define IFA_TX_ERROR tx_errors
#else
#  error "No suitable type for `struct ifaddrs->ifa_data' found."
#endif

	struct IFA_DATA *if_data;

	if (getifaddrs (&if_list) != 0)
		return;

	for (if_ptr = if_list; if_ptr != NULL; if_ptr = if_ptr->ifa_next)
	{
		if ((if_data = (struct IFA_DATA *) if_ptr->ifa_data) == NULL)
			continue;

		bytes_submit (if_ptr->ifa_name,
				if_data->IFA_RX_BYTES,
				if_data->IFA_TX_BYTES);
		packets_submit (if_ptr->ifa_name,
				if_data->IFA_RX_PACKT,
				if_data->IFA_TX_PACKT);
		errors_submit (if_ptr->ifa_name,
				if_data->IFA_RX_ERROR,
				if_data->IFA_TX_ERROR);
	}

	freeifaddrs (if_list);
/* #endif HAVE_GETIFADDRS */

#elif KERNEL_LINUX
	FILE *fh;
	char buffer[1024];
	unsigned long long incoming, outgoing;
	char *device;
	
	char *dummy;
	char *fields[16];
	int numfields;

	if ((fh = fopen ("/proc/net/dev", "r")) == NULL)
	{
		syslog (LOG_WARNING, "traffic: fopen: %s", strerror (errno));
		return;
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		if (buffer[6] != ':')
			continue;
		buffer[6] = '\0';

		device = buffer;
		while (device[0] == ' ')
			device++;

		if (device[0] == '\0')
			continue;
		
		dummy = buffer + 7;
		numfields = strsplit (dummy, fields, 16);

		if (numfields < 9)
			continue;

		incoming = atoll (fields[0]);
		outgoing = atoll (fields[8]);

		bytes_submit (device, incoming, outgoing);
	}

	fclose (fh);
/* #endif KERNEL_LINUX */

#elif defined(HAVE_LIBKSTAT)
	int i;
	unsigned long long incoming, outgoing;

	if (kc == NULL)
		return;

	for (i = 0; i < numif; i++)
	{
		if (kstat_read (kc, ksp[i], NULL) == -1)
			continue;

		if ((incoming = get_kstat_value (ksp[i], "rbytes")) == -1LL)
			continue;
		if ((outgoing = get_kstat_value (ksp[i], "obytes")) == -1LL)
			continue;

		bytes_submit (ksp[i]->ks_name, incoming, outgoing);
	}
/* #endif HAVE_LIBKSTAT */

#elif defined(HAVE_LIBSTATGRAB)
	sg_network_io_stats *ios;
	int i, num;

	ios = sg_get_network_io_stats (&num);

	for (i = 0; i < num; i++)
		bytes_submit (ios[i].interface_name, ios[i].rx, ios[i].tx);
#endif /* HAVE_LIBSTATGRAB */
}
#else
#define traffic_read NULL
#endif /* TRAFFIC_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, traffic_init, traffic_read, bytes_write);
	plugin_register ("if_packets", NULL, NULL, packets_write);
	plugin_register ("if_errors",  NULL, NULL, errors_write);
}

#undef BUFSIZE
#undef MODULE_NAME
