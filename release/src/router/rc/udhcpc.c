/*
 * udhcpc scripts
 *
 * Copyright (C) 2009, Broadcom Corporation. All Rights Reserved.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id: udhcpc.c,v 1.27 2009/12/02 20:06:40 Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <proto/ethernet.h>

#include <bcmnvram.h>
#include <shutils.h>
#include <rc.h>

/* Zeroconf support if DHCP fails */
#undef DHCP_ZEROCONF

static int
expires(char *wan_ifname, unsigned int in)
{
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	int unit;
	time_t now;
	FILE *fp;

	if ((unit = wan_prefix(wan_ifname, prefix)) < 0)
		return -1;
	if (wan_ifunit(wan_ifname) < 0)
		snprintf(prefix, sizeof(prefix), "wan%d_x", unit);

	nvram_set_int(strcat_r(prefix, "expires", tmp), (unsigned int) uptime() + in);

	snprintf(tmp, sizeof(tmp), "/tmp/udhcpc%d.expires", unit);
	if ((fp = fopen(tmp, "w")) == NULL) {
		perror(tmp);
		return errno;
	}
	time(&now);
	fprintf(fp, "%d", (unsigned int) now + in);
	fclose(fp);

	return 0;
}

/* 
 * deconfig: This argument is used when udhcpc starts, and when a
 * leases is lost. The script should put the interface in an up, but
 * deconfigured state.
*/
static int
deconfig(int zcip)
{
#ifdef RTCONFIG_DSL_TCLINUX //tmp
	return 0;
#endif
	char *wan_ifname = safe_getenv("interface");
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	int unit = wan_ifunit(wan_ifname);

	/* Figure out nvram variable name prefix for this i/f */
	if (wan_prefix(wan_ifname, prefix) < 0)
		return -1;
	if ((unit < 0) &&
	    (nvram_match(strcat_r(prefix, "proto", tmp), "l2tp") ||
	     nvram_match(strcat_r(prefix, "proto", tmp), "pptp"))) {
		logmessage(zcip ? "zcip client" : "dhcp client", "skipping resetting IP address to 0.0.0.0");
	} else
		ifconfig(wan_ifname, IFUP, "0.0.0.0", NULL);

	expires(wan_ifname, 0);
	wan_down(wan_ifname);

	/* Skip physical VPN subinterface */
	if (!(unit < 0))
		update_wan_state(prefix, WAN_STATE_STOPPED, WAN_STOPPED_REASON_DHCP_DECONFIG);

	_dprintf("udhcpc:: %s done\n", __FUNCTION__);
	return 0;
}

/*
 * bound: This argument is used when udhcpc moves from an unbound, to
 * a bound state. All of the paramaters are set in enviromental
 * variables, The script should configure the interface, and set any
 * other relavent parameters (default gateway, dns server, etc).
*/
static int
bound(void)
{
	char *wan_ifname = safe_getenv("interface");
	char *value, *gateway;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	char wanprefix[] = "wanXXXXXXXXXX_";
	char route[sizeof("255.255.255.255/255")];
	int unit, ifunit;
	int changed = 0;
#ifdef RTCONFIG_TMOBILE
	int changed_sip = 0;
#endif

#ifdef DHCP_ZEROCONF
	killall("zcip", SIGTERM);
#endif
	/* Figure out nvram variable name prefix for this i/f */
	if ((ifunit = wan_prefix(wan_ifname, wanprefix)) < 0)
		return -1;
	if ((unit = wan_ifunit(wan_ifname)) < 0)
		snprintf(prefix, sizeof(prefix), "wan%d_x", ifunit);
	else	snprintf(prefix, sizeof(prefix), "wan%d_", ifunit);

	if ((value = getenv("ip"))) {
		changed = !nvram_match(strcat_r(prefix, "ipaddr", tmp), trim_r(value));
		nvram_set(strcat_r(prefix, "ipaddr", tmp), trim_r(value));
	}
	if ((value = getenv("subnet")))
		nvram_set(strcat_r(prefix, "netmask", tmp), trim_r(value));
	if ((gateway = getenv("router")))
		nvram_set(strcat_r(prefix, "gateway", tmp), trim_r(gateway));
	/* ex: android phone, the gateway is the DNS server. */
	if ((value = getenv("dns") ? : gateway) &&
	    nvram_get_int(strcat_r(wanprefix, "dnsenable_x", tmp)))
		nvram_set(strcat_r(prefix, "dns", tmp), trim_r(value));
	if ((value = getenv("wins")))
		nvram_set(strcat_r(prefix, "wins", tmp), trim_r(value));
	//if ((value = getenv("hostname")))
	//	sethostname(value, strlen(value) + 1);
	if ((value = getenv("domain")))
		nvram_set(strcat_r(prefix, "domain", tmp), trim_r(value));
#ifdef RTCONFIG_TMOBILE
	if ((value = getenv("sipsrv"))) {
		changed_sip = !nvram_match(strcat_r(prefix, "sipsrv", tmp), trim_r(value));
		nvram_set(strcat_r(prefix, "sipsrv", tmp), trim_r(value));
	}
#endif
	if ((value = getenv("lease"))) {
		unsigned int lease = atoi(value);
		nvram_set_int(strcat_r(prefix, "lease", tmp), lease);
		expires(wan_ifname, lease);
	}

	/* classful static routes */
	nvram_set(strcat_r(prefix, "routes", tmp), getenv("routes"));
	/* ms classless static routes */
	nvram_set(strcat_r(prefix, "routes_ms", tmp), getenv("msstaticroutes"));
	/* rfc3442 classless static routes */
	nvram_set(strcat_r(prefix, "routes_rfc", tmp), getenv("staticroutes"));

	/* rfc3442 could contain gateway
	 * format: "net/size gateway" */
	if (!gateway) {
		foreach(route, nvram_safe_get(strcat_r(prefix, "routes_rfc", tmp)), value) {
			if (gateway) {
				nvram_set(strcat_r(prefix, "gateway", tmp), route);
				break;
			} else
			if (strcmp(route, "0.0.0.0/0") == 0)
				gateway = route;
		}
	}

#ifdef RTCONFIG_IPV6
	if ((value = getenv("ip6rd")) &&
	    (get_ipv6_service() == IPV6_6RD && nvram_match("ipv6_6rd_dhcp", "1"))) {
		char *ptr, *values[4];
		int i;

		ptr = value = strdup(value);
		for (i = 0; value && i < 4; i++)
			values[i] = strsep(&value, " ");
		if (i == 4) {
			nvram_set(strcat_r(wanprefix, "6rd_ip4size", tmp), values[0]);
			nvram_set(strcat_r(wanprefix, "6rd_prefixlen", tmp), values[1]);
			nvram_set(strcat_r(wanprefix, "6rd_prefix", tmp), values[2]);
			nvram_set(strcat_r(wanprefix, "6rd_router", tmp), values[3]);
		}
		free(ptr);
	}
#endif

	// check if the ipaddr is safe to apply
	// only handle one lan instance so far
	// update_wan_state(prefix, WAN_STATE_STOPPED, WAN_STOPPED_REASON_INVALID_IPADDR)
	if (inet_equal(nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)), nvram_safe_get(strcat_r(prefix, "netmask", tmp)),
		       nvram_safe_get("lan_ipaddr"), nvram_safe_get("lan_netmask"))) {
		update_wan_state(prefix, WAN_STATE_STOPPED, WAN_STOPPED_REASON_INVALID_IPADDR);
		return 0;
	}

	/* Clean nat conntrack for this interface,
	 * but skip physical VPN subinterface for PPTP/L2TP */
	if (changed && !(unit < 0 &&
	    (nvram_match(strcat_r(wanprefix, "proto", tmp), "l2tp") ||
	     nvram_match(strcat_r(wanprefix, "proto", tmp), "pptp"))))
		ifconfig(wan_ifname, IFUP, "0.0.0.0", NULL);
	ifconfig(wan_ifname, IFUP,
		 nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)),
		 nvram_safe_get(strcat_r(prefix, "netmask", tmp)));

	wan_up(wan_ifname);
#ifdef RTCONFIG_TMOBILE
	if (changed_sip)
		notify_rc("restart_dnsmasq");
#endif

	logmessage("dhcp client", "bound %s via %s during %d seconds.",
		nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)),
		nvram_safe_get(strcat_r(prefix, "gateway", tmp)),
		nvram_get_int(strcat_r(prefix, "lease", tmp))
		);

	_dprintf("udhcpc:: %s done\n", __FUNCTION__);
	return 0;
}

/*
 * renew: This argument is used when a DHCP lease is renewed. All of
 * the paramaters are set in enviromental variables. This argument is
 * used when the interface is already configured, so the IP address,
 * will not change, however, the other DHCP paramaters, such as the
 * default gateway, subnet mask, and dns server may change.
 */
static int
renew(void)
{
	char *wan_ifname = safe_getenv("interface");
	char *value, *gateway;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	char wanprefix[] = "wanXXXXXXXXXX_";
	int unit, ifunit;
	int changed = 0;
#ifdef RTCONFIG_TMOBILE
	int changed_sip = 0;
#endif

#ifdef DHCP_ZEROCONF
	killall("zcip", SIGTERM);
#endif
	/* Figure out nvram variable name prefix for this i/f */
	if ((ifunit = wan_prefix(wan_ifname, wanprefix)) < 0)
		return -1;
	if ((unit = wan_ifunit(wan_ifname)) < 0)
		snprintf(prefix, sizeof(prefix), "wan%d_x", ifunit);
	else	snprintf(prefix, sizeof(prefix), "wan%d_", ifunit);

	if ((value = getenv("subnet")) == NULL ||
	    nvram_invmatch(strcat_r(prefix, "netmask", tmp), trim_r(value)))
		return bound();
	if ((gateway = getenv("router")) == NULL ||
	    nvram_invmatch(strcat_r(prefix, "gateway", tmp), trim_r(gateway)))
		return bound();

	/* ex: android phone, the gateway is the DNS server. */
	if ((value = getenv("dns") ? : gateway) &&
	    nvram_get_int(strcat_r(wanprefix, "dnsenable_x", tmp))) {
		changed = !nvram_match(strcat_r(prefix, "dns", tmp), trim_r(value));
		nvram_set(strcat_r(prefix, "dns", tmp), trim_r(value));
	}
	if ((value = getenv("wins")))
		nvram_set(strcat_r(prefix, "wins", tmp), trim_r(value));
	//if ((value = getenv("hostname")))
	//	sethostname(value, strlen(value) + 1);
	if ((value = getenv("domain")))
		nvram_set(strcat_r(prefix, "domain", tmp), trim_r(value));
#ifdef RTCONFIG_TMOBILE
	if ((value = getenv("sipsrv"))) {
		changed_sip = !nvram_match(strcat_r(prefix, "sipsrv", tmp), trim_r(value));
		nvram_set(strcat_r(prefix, "sipsrv", tmp), trim_r(value));
	}
#endif
	if ((value = getenv("lease"))) {
		unsigned int lease = atoi(value);
		nvram_set_int(strcat_r(prefix, "lease", tmp), lease);
		expires(wan_ifname, lease);
	}

	/* Update actual DNS or delayed for DHCP+PPP */
	if (changed)
		update_resolvconf();
#ifdef RTCONFIG_TMOBILE
	if (changed_sip)
		notify_rc("restart_dnsmasq");
#endif

	/* Update connected state and DNS for WEB UI,
	 * but skip physical VPN subinterface */
	if (changed && !(unit < 0))
		update_wan_state(wanprefix, WAN_STATE_CONNECTED, 0);

	_dprintf("udhcpc:: %s done\n", __FUNCTION__);
	return 0;
}

#ifdef DHCP_ZEROCONF
static int
leasefail(void)
{
	char *wan_ifname = safe_getenv("interface");
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	char wanprefix[] = "wanXXXXXXXXXX_";
	int unit, ifunit;

	/* Figure out nvram variable name prefix for this i/f */
	if ((ifunit = wan_prefix(wan_ifname, wanprefix)) < 0)
		return -1;
	if ((unit = wan_ifunit(wan_ifname)) < 0)
		snprintf(prefix, sizeof(prefix), "wan%d_x", ifunit);
	else	snprintf(prefix, sizeof(prefix), "wan%d_", ifunit);

	if ((inet_network(nvram_safe_get(strcat_r(prefix, "ipaddr", tmp))) &
	     inet_network(nvram_safe_get(strcat_r(prefix, "netmask", tmp)))) ==
	     inet_network("169.254.0.0"))
		return 0;

	return start_zcip(wan_ifname);
}
#endif

int
udhcpc_wan(int argc, char **argv)
{
	if(strcmp(argv[1], "leasefail"))
		_dprintf("%s:: %s\n", __FUNCTION__, argv[1]);
	if (!argv[1])
		return EINVAL;
	else if (strstr(argv[1], "deconfig"))
		return deconfig(0);
	else if (strstr(argv[1], "bound"))
		return bound();
	else if (strstr(argv[1], "renew"))
		return renew();
#ifdef DHCP_ZEROCONF
	else if (strstr(argv[1], "leasefail"))
		return leasefail();
#endif
/*	else if (strstr(argv[1], "nak")) */

	return 0;
}

int
start_udhcpc(char *wan_ifname, int unit, pid_t *ppid)
{
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	char pid[sizeof("/var/run/udhcpcXXXXXXXXXX.pid")];
#ifdef RTCONFIG_DSL
	char clientid[sizeof("61:") + (32+32+1)*2];
#endif
	char *value;
	char *dhcp_argv[] = { "udhcpc",
		"-i", wan_ifname,
		"-p", (snprintf(pid, sizeof(pid), "/var/run/udhcpc%d.pid", unit), pid),
		"-s", "/tmp/udhcpc",
		"-x", "61:SKY-FTTC-CLIENT-ID-HEX",
		"-x", "60:SKY-FTTC-VENDOR-CLASS-HEX",
		NULL,		/* -t2 */
		NULL,		/* -T5 */
		NULL,		/* -A120 */
		NULL,		/* -b */
		NULL, NULL,	/* -H wan_hostname */
		NULL,		/* -Oroutes */
		NULL,		/* -Ostaticroutes */
		NULL,		/* -Omsstaticroutes */
#ifdef __CONFIG_IPV6__
		NULL,		/* -Oip6rd rfc */
		NULL,		/* -Oip6rd comcast */
#endif
#ifdef RTCONFIG_DSL
		NULL, NULL,	/* -x 61:wan_clientid */
#endif
		NULL };
	int index = 11;		/* first NULL */
	int dr_enable;

	/* Use unit */
	snprintf(prefix, sizeof(prefix), "wan%d_", unit);

	if (nvram_get_int("dhcpc_mode") == 0)
	{
		/* 2 discover packets max (default 3 discover packets) */
		dhcp_argv[index++] = "-t2";
		/* 5 seconds between packets (default 3 seconds) */
		dhcp_argv[index++] = "-T5";
		/* Wait 160 seconds before trying again (default 20 seconds) */
		/* set to 160 to accomodate new timings enforced by Charter cable */
		dhcp_argv[index++] = "-A160";
	}

	if (ppid == NULL)
		dhcp_argv[index++] = "-b";

	value = nvram_safe_get(strcat_r(prefix, "hostname", tmp));
	if (*value && is_valid_hostname(value)) {
		dhcp_argv[index++] = "-H";
		dhcp_argv[index++] = value;
	}

	/* 0: disable, 1: MS routes, 2: RFC routes, 3: Both */
	dr_enable = nvram_get_int("dr_enable_x");
	if (dr_enable != 0) {
		dhcp_argv[index++] = "-O33";		/* routes */
		if (dr_enable & (1 << 0))
			dhcp_argv[index++] = "-O249";	/* "msstaticroutes" */
		if (dr_enable & (1 << 1))
			dhcp_argv[index++] = "-O121";	/* "staticroutes" */
	}

#ifdef RTCONFIG_IPV6
	if (get_ipv6_service() == IPV6_6RD && nvram_match("ipv6_6rd_dhcp", "1")) {
		dhcp_argv[index++] = "-O212";		/* ip6rd rfc */
		dhcp_argv[index++] = "-O150";		/* ip6rd comcast */
	}
#endif

#ifdef RTCONFIG_DSL
	value = nvram_safe_get(strcat_r(prefix, "clientid", tmp));
	if (*value) {
		char *ptr = clientid;
		ptr += sprintf(ptr, "61:");
		while (*value && (ptr - clientid) < sizeof(clientid) - 2)
			ptr += sprintf(ptr, "%02x", *value++);
		dhcp_argv[index++] = "-x";
		dhcp_argv[index++] = clientid;
	}
#endif

#ifdef RTCONFIG_TMOBILE
	/* request option 120 */
	dhcp_argv[index++] = "-O";
	dhcp_argv[index++] = "120";
#endif

	return _eval(dhcp_argv, NULL, 0, ppid);
}

/*
 * config: This argument is used when zcip moves to configured state.
 * All of the paramaters are set in enviromental variables, the script
 * should configure the interface.
*/
static int
config(void)
{
	char *wan_ifname = safe_getenv("interface");
	char *value;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	char wanprefix[] = "wanXXXXXXXXXX_";
	int unit, ifunit;
	int changed = 0;

	/* Figure out nvram variable name prefix for this i/f */
	if ((ifunit = wan_prefix(wan_ifname, wanprefix)) < 0)
		return -1;
	if ((unit = wan_ifunit(wan_ifname)) < 0)
		snprintf(prefix, sizeof(prefix), "wan%d_x", ifunit);
	else	snprintf(prefix, sizeof(prefix), "wan%d_", ifunit);

	if ((value = getenv("ip"))) {
		changed = !nvram_match(strcat_r(prefix, "ipaddr", tmp), trim_r(value));
		nvram_set(strcat_r(prefix, "ipaddr", tmp), trim_r(value));
	}
	nvram_set(strcat_r(prefix, "netmask", tmp), "255.255.0.0");
	nvram_set(strcat_r(prefix, "gateway", tmp), "");
	if (nvram_get_int(strcat_r(wanprefix, "dnsenable_x", tmp)))
		nvram_set(strcat_r(prefix, "dns", tmp), "");
	//nvram_set(strcat_r(prefix, "wins", tmp), "");
	//nvram_set(strcat_r(prefix, "domain", tmp), "");
#ifdef DHCP_ZEROCONF
	nvram_unset(strcat_r(prefix, "lease", tmp));
	nvram_unset(strcat_r(prefix, "expires", tmp));
#endif
	/* Clean nat conntrack for this interface,
	 * but skip physical VPN subinterface for PPTP/L2TP */
	if (changed && !(unit < 0 &&
	    (nvram_match(strcat_r(wanprefix, "proto", tmp), "l2tp") ||
	     nvram_match(strcat_r(wanprefix, "proto", tmp), "pptp"))))
		ifconfig(wan_ifname, IFUP, "0.0.0.0", NULL);
	ifconfig(wan_ifname, IFUP,
		 nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)),
		 nvram_safe_get(strcat_r(prefix, "netmask", tmp)));

	wan_up(wan_ifname);

	logmessage("zcip client", "configured %s",
		nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)));

	_dprintf("zcip:: %s done\n", __FUNCTION__);
	return 0;
}

int
zcip_wan(int argc, char **argv)
{
	_dprintf("%s:: %s\n", __FUNCTION__, argv[1]);
	if (!argv[1])
		return EINVAL;
	else if (strstr(argv[1], "deconfig"))
		return deconfig(1);
	else if (strstr(argv[1], "config"))
		return config();
/*	else if (strstr(argv[1], "init")) */

	return 0;
}

int
start_zcip(char *wan_ifname)
{
	char *zcip_argv[] = { "zcip", "-q", wan_ifname, "/tmp/zcip", NULL };

	return _eval(zcip_argv, NULL, 0, NULL);
}

static int
expires_lan(char *lan_ifname, unsigned int in)
{
	time_t now;
	FILE *fp;
	char tmp[100];

	time(&now);
	snprintf(tmp, sizeof(tmp), "/tmp/udhcpc-%s.expires", lan_ifname);
	if (!(fp = fopen(tmp, "w"))) {
		perror(tmp);
		return errno;
	}
	fprintf(fp, "%d", (unsigned int) now + in);
	fclose(fp);
	return 0;
}

/* 
 * deconfig: This argument is used when udhcpc starts, and when a
 * leases is lost. The script should put the interface in an up, but
 * deconfigured state.
*/
static int
deconfig_lan(void)
{
	char *lan_ifname = safe_getenv("interface");

	//ifconfig(lan_ifname, IFUP, "0.0.0.0", NULL);
_dprintf("%s: IFUP.\n", __FUNCTION__);
	if(nvram_match("lan_proto", "static"))
		ifconfig(lan_ifname, IFUP, nvram_safe_get("lan_ipaddr"), nvram_safe_get("lan_netmask"));
	else
		ifconfig(lan_ifname, IFUP, nvram_default_get("lan_ipaddr"), nvram_default_get("lan_netmask"));

	expires_lan(lan_ifname, 0);

	lan_down(lan_ifname);

	_dprintf("done\n");
	return 0;
}

/*
 * bound: This argument is used when udhcpc moves from an unbound, to
 * a bound state. All of the paramaters are set in enviromental
 * variables, The script should configure the interface, and set any
 * other relavent parameters (default gateway, dns server, etc).
*/
static int
bound_lan(void)
{
	char *lan_ifname = safe_getenv("interface");
	char *value;

	if ((value = getenv("ip")))
		nvram_set("lan_ipaddr", trim_r(value));
	if ((value = getenv("subnet")))
		nvram_set("lan_netmask", trim_r(value));
	if ((value = getenv("router")))
		nvram_set("lan_gateway", trim_r(value));
	if ((value = getenv("lease"))) {
		nvram_set("lan_lease", trim_r(value));
		expires_lan(lan_ifname, atoi(value));
	}
	if (nvram_get_int("lan_dnsenable_x") && (value = getenv("dns")))
		nvram_set("lan_dns", trim_r(value));

_dprintf("%s: IFUP.\n", __FUNCTION__);
#ifdef RTCONFIG_WIRELESSREPEATER
	if(nvram_get_int("sw_mode") == SW_MODE_REPEATER && nvram_get_int("wlc_mode") == 0){
		update_lan_state(LAN_STATE_CONNECTED, 0);
		_dprintf("done\n");
		return 0;
	}
#endif

	ifconfig(lan_ifname, IFUP, nvram_safe_get("lan_ipaddr"),
		nvram_safe_get("lan_netmask"));

	lan_up(lan_ifname);

	_dprintf("done\n");
	return 0;
}

/*
 * renew: This argument is used when a DHCP lease is renewed. All of
 * the paramaters are set in enviromental variables. This argument is
 * used when the interface is already configured, so the IP address,
 * will not change, however, the other DHCP paramaters, such as the
 * default gateway, subnet mask, and dns server may change.
 */
static int
renew_lan(void)
{
	bound_lan();

	_dprintf("done\n");
	return 0;
}

/* dhcp client script entry for LAN (AP) */
int
udhcpc_lan(int argc, char **argv)
{
	if (!argv[1])
		return EINVAL;
	else if (strstr(argv[1], "deconfig"))
		return deconfig_lan();
	else if (strstr(argv[1], "bound"))
		return bound_lan();
	else if (strstr(argv[1], "renew"))
		return renew_lan();
	else
		return EINVAL;
}

// -----------------------------------------------------------------------------

#ifdef RTCONFIG_IPV6
// copy env to nvram
// returns 1 if new/changed, 0 if not changed/no env
static int env2nv(char *env, char *nv)
{
	char *value = getenv(env);
	if (value) {
		value = trim_r(value);
		if (!nvram_match(nv, value)) {
			nvram_set(nv, value);
			return 1;
		}
	}
	return 0;
}

int dhcp6c_state_main(int argc, char **argv)
{
	struct in6_addr range;
	char addr[INET6_ADDRSTRLEN + 1], *value;
	int addr_changed = 0;
	int prefix_changed = 0;
	int dns_changed = 0;
	int size, start, end;

	/* Check if enabled */
	if (get_ipv6_service() != IPV6_NATIVE_DHCP)
		return 0;

	if (!wait_action_idle(10))
		return 1;

	if (nvram_get_int("ipv6_dhcp_pd")) {
		value = (char *) getifaddr(nvram_safe_get("lan_ifname"), AF_INET6, GIF_PREFIXLEN) ? : "";
		if (sscanf(value, "%[^/]/%d", addr, &size) != 2)
			goto skip;

		addr_changed = !nvram_match("ipv6_rtr_addr", addr);
		if (addr_changed)
			nvram_set("ipv6_rtr_addr", addr);

		prefix_changed |= (nvram_get_int("ipv6_prefix_length") != size);
		if (prefix_changed)
			nvram_set_int("ipv6_prefix_length", size);

		value = (char *) ipv6_prefix(NULL);
		prefix_changed |= !nvram_match("ipv6_prefix", value);
		if (prefix_changed)
			nvram_set("ipv6_prefix", value);

		if (prefix_changed && nvram_get_int("ipv6_autoconf_type")) {
		/* TODO: rework WEB UI to specify ranges without prefix
		 * TODO: add size checking, now range takes all of 16 bit */
			start = (inet_pton(AF_INET6, nvram_safe_get("ipv6_dhcp_start"), &range) > 0) ?
			    ntohs(range.s6_addr16[7]) : 0x1000;
			end = (inet_pton(AF_INET6, nvram_safe_get("ipv6_dhcp_end"), &range) > 0) ?
			    ntohs(range.s6_addr16[7]) : 0x2000;

			inet_pton(AF_INET6, nvram_safe_get("ipv6_prefix"), &range);

			range.s6_addr16[7] = (start < end) ? htons(start) : htons(end);
			inet_ntop(AF_INET6, &range, addr, sizeof(addr));
			nvram_set("ipv6_dhcp_start", addr);
			range.s6_addr16[7] = (start < end) ? htons(end) : htons(start);
			inet_ntop(AF_INET6, &range, addr, sizeof(addr));
			nvram_set("ipv6_dhcp_end", addr);
		}
	}
skip:

	dns_changed += env2nv("new_domain_name_servers", "ipv6_get_dns");
	dns_changed += env2nv("new_domain_name", "ipv6_get_domain");

	if (dns_changed)
		update_resolvconf();

#ifdef RTCONFIG_DNSMASQ6
#ifdef DNS6_PASSTHROUGH /* unused */
	if (dns_changed && nvram_get_int("ipv6_dnsenable"))
		start_dnsmasq();
#endif
#else
#ifdef DNS6_PASSTHROUGH /* unused */
	if ((dns_changed && nvram_get_int("ipv6_dnsenable")) ||
	    (prefix_changed && nvram_get_int("ipv6_autoconf_type")))
		start_dhcp6s();
	if (prefix_changed ||
	    (dns_changed && nvram_get_int("ipv6_dnsenable")))
		start_radvd();
#else
	if (addr_changed ||
	    (prefix_changed && nvram_get_int("ipv6_autoconf_type")))
		start_dhcp6s();
	if (prefix_changed || addr_changed)
		start_radvd();
#endif
#endif

	return 0;
}

int
start_dhcp6c(void)
{
	FILE *fp;
	char *wan_ifname = (char *)get_wan6face();
	char *lan_ifname = nvram_safe_get("lan_ifname");
	char *dhcp6c_argv[] = { "dhcp6c",
		"-T", "LL",
		NULL,		/* -D */
		NULL,		/* interface */
		NULL };
	int index = 3;
	unsigned char ea[ETHER_ADDR_LEN];
	unsigned long iaid = 0;
	struct {
		uint16 type;
		uint16 hwtype;
	} __attribute__ ((__packed__)) duid;
	uint16 duid_len = 0;
	int prefix_len;
	int need_wanaddr, need_prefix, need_dns;

	/* Check if enabled */
	if (get_ipv6_service() != IPV6_NATIVE_DHCP)
		return 0;

	if (!wan_ifname || *wan_ifname == '\0')
		return -1;

	need_wanaddr = nvram_match("ipv6_ra_conf", "mset");
	need_prefix = nvram_get_int("ipv6_dhcp_pd");
	need_dns = nvram_get_int("ipv6_dnsenable") &&
		(nvram_match("ipv6_ra_conf", "mset") ||
	         nvram_match("ipv6_ra_conf", "oset"));

#ifndef RTCONFIG_DNSMASQ6
#ifdef DNS6_PASSTHROUGH /* unused */
	if (!nvram_get_int("ipv6_dnsenable") &&
	    (!nvram_get_int("ipv6_autoconf_type") || !nvram_get_int("ipv6_dhcp_pd")))
		start_dhcp6s();
	if (!nvram_get_int("ipv6_dhcp_pd") &&
	    !nvram_get_int("ipv6_dnsenable"))
		start_radvd();
#else
	if (!nvram_get_int("ipv6_dhcp_pd")) {
		start_dhcp6s();
		start_radvd();
	}
#endif
#endif

	if (!need_wanaddr && !need_prefix && !need_dns)
		return 0;

	nvram_set("ipv6_get_dns", "");
	nvram_set("ipv6_get_domain", "");
	if (nvram_get_int("ipv6_dhcp_pd")) {
		nvram_set("ipv6_rtr_addr", "");
		nvram_set("ipv6_prefix", "");
	}

	prefix_len = 64 - (nvram_get_int("ipv6_prefix_length") ? : 64);
	if (prefix_len < 0)
		prefix_len = 0;

	if (ether_atoe(nvram_safe_get("wan0_hwaddr"), ea)) {
		/* Generate IAID from the last 7 digits of WAN MAC */
		iaid =	((unsigned long)(ea[3] & 0x0f) << 16) |
			((unsigned long)(ea[4]) << 8) |
			((unsigned long)(ea[5]));

		/* Generate DUID-LL */
		duid_len = sizeof(duid) + ETHER_ADDR_LEN;
		duid.type = htons(3);	/* DUID-LL */
		duid.hwtype = htons(1);	/* Ethernet */
	}

	/* Create dhcp6c_duid */
	unlink("/var/dhcp6c_duid");
	if ((duid_len != 0) &&
	    (fp = fopen("/var/dhcp6c_duid", "w")) != NULL) {
		fwrite(&duid_len, sizeof(duid_len), 1, fp);
		fwrite(&duid, sizeof(duid), 1, fp);
		fwrite(&ea, ETHER_ADDR_LEN, 1, fp);
		fclose(fp);
	}

	/* Create dhcp6c.conf */
	if ((fp = fopen("/etc/dhcp6c.conf", "w")) == NULL) {
		perror("/etc/dhcp6c.conf");
		return -1;
	}

	fprintf(fp, "interface %s {\n"
			    "script \"%s\";\n", wan_ifname, "/sbin/dhcp6c-state");

	if (need_wanaddr || need_prefix)
		fprintf(fp, "send rapid-commit;\n");
	else
		fprintf(fp, "information-only;\n");

	if (need_wanaddr)
		fprintf(fp, "send ia-na %lu;\n", iaid);
	if (need_prefix)
		fprintf(fp, "send ia-pd %lu;\n", iaid);
	if (need_dns) {
		fprintf(fp, "request domain-name-servers;\n"
			    "request domain-name;\n");
	}
	fprintf(fp, "};\n");

	if (need_wanaddr) {
		fprintf(fp,
		    "id-assoc na %lu { };\n", iaid);
	}
	if (need_prefix) {
		fprintf(fp,
		    "id-assoc pd %lu {\n"
			    "prefix-interface %s {\n"
				    "sla-id 1;\n"
				    "sla-len %d;\n"
			    "};\n"
		    "};\n", iaid, lan_ifname, prefix_len);
	}
	fclose(fp);

	if (nvram_get_int("ipv6_debug"))
		dhcp6c_argv[index++] = "-D";

	dhcp6c_argv[index++] = wan_ifname;

	return _eval(dhcp6c_argv, NULL, 0, NULL);
}

void stop_dhcp6c(void)
{
	char *lan_ifname = nvram_safe_get("lan_ifname");

	if (!pids("dhcp6c"))
		return;

	killall("dhcp6c-event", SIGTERM);
	killall_tk("dhcp6c");

	if (nvram_get_int("ipv6_dhcp_pd"))
		eval("ip", "-6", "addr", "flush", "scope", "global", "dev", lan_ifname);
	eval("ip", "-6", "neigh", "flush", "dev", lan_ifname);
}
#endif	// RTCONFIG_IPV6
