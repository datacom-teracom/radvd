/*
 *   $Id: send.c,v 1.6 1999/06/15 21:42:04 lf Exp $
 *
 *   Authors:
 *    Pedro Roque		<roque@di.fc.ul.pt>
 *    Lars Fenneberg		<lf@elemental.net>	 
 *
 *   This software is Copyright 1996,1997 by the above mentioned author(s), 
 *   All Rights Reserved.
 *
 *   The license which is distributed with this software in the file COPYRIGHT
 *   applies to this software. If your distribution is missing this file, you
 *   may request it from <lf@elemental.net>.
 *
 */

#include <config.h>
#include <includes.h>
#include <radvd.h>

void
send_ra(int sock, struct Interface *iface, struct in6_addr *dest)
{
	uint8_t all_hosts_addr[] = {0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	struct sockaddr_in6 addr;
	struct in6_pktinfo *pkt_info;
	struct msghdr mhdr;
	struct cmsghdr *cmsg;
	struct iovec iov;
	char chdr[CMSG_SPACE(sizeof(struct in6_pktinfo))];
	struct nd_router_advert *radvert;
	struct AdvPrefix *prefix;
	unsigned char buff[MSG_SIZE];
	int len = 0;
	int err;

	dlog(LOG_DEBUG, 3, "sending RA on %s", iface->Name);

	if (dest == NULL)
	{
		struct timeval tv;

		dest = (struct in6_addr *)all_hosts_addr;
		gettimeofday(&tv, NULL);

		iface->last_multicast = tv.tv_sec;
	}
	
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(IPPROTO_ICMPV6);
	memcpy(&addr.sin6_addr, dest, sizeof(struct in6_addr));

	radvert = (struct nd_router_advert *) buff;

	radvert->nd_ra_type  = ND_ROUTER_ADVERT;
	radvert->nd_ra_code  = 0;
	radvert->nd_ra_cksum = 0;

	radvert->nd_ra_curhoplimit	= iface->AdvCurHopLimit;
	radvert->nd_ra_flags_reserved	= 
		(iface->AdvManagedFlag)?ND_RA_FLAG_MANAGED:0;
	radvert->nd_ra_flags_reserved	|= 
		(iface->AdvOtherConfigFlag)?ND_RA_FLAG_OTHER:0;
	radvert->nd_ra_router_lifetime	= htons(iface->AdvDefaultLifetime);

	radvert->nd_ra_reachable  = htonl(iface->AdvReachableTime);
	radvert->nd_ra_retransmit = htonl(iface->AdvRetransTimer);

	len = sizeof(struct nd_router_advert);

	prefix = iface->AdvPrefixList;

	/*
	 *	add prefix options
	 */

	while(prefix)
	{
		struct nd_opt_prefix_info *pinfo;
		
		pinfo = (struct nd_opt_prefix_info *) (buff + len);

		pinfo->nd_opt_pi_type	     = ND_OPT_PREFIX_INFORMATION;
		pinfo->nd_opt_pi_len	     = 4;
		pinfo->nd_opt_pi_prefix_len  = prefix->PrefixLen;
		
		pinfo->nd_opt_pi_flags_reserved  = 
			(prefix->AdvOnLinkFlag)?ND_OPT_PI_FLAG_ONLINK:0;
		pinfo->nd_opt_pi_flags_reserved	|=
			(prefix->AdvAutonomousFlag)?ND_OPT_PI_FLAG_AUTO:0;
			
		pinfo->nd_opt_pi_valid_time	= htonl(prefix->AdvValidLifetime);
		pinfo->nd_opt_pi_preferred_time = htonl(prefix->AdvPreferredLifetime);
		pinfo->nd_opt_pi_reserved2	= 0;
		
		memcpy(&pinfo->nd_opt_pi_prefix, &prefix->Prefix,
		       sizeof(struct in6_addr));

		len += sizeof(struct nd_opt_prefix_info);

		prefix = prefix->next;
	}
	
	/*
	 *	add MTU option
	 */

	if (iface->AdvLinkMTU != 0) {
		struct nd_opt_mtu *mtu;
		
		mtu = (struct nd_opt_mtu *) (buff + len);
	
		mtu->nd_opt_mtu_type     = ND_OPT_MTU;
		mtu->nd_opt_mtu_len      = 1;
		mtu->nd_opt_mtu_reserved = 0; 
		mtu->nd_opt_mtu_mtu      = htonl(iface->AdvLinkMTU);

		len += sizeof(struct nd_opt_mtu);
	}

	/*
	 * add Source Link-layer Address option
	 */

	if (iface->AdvSourceLLAddress && iface->if_hwaddr_len != -1)
	{
		uint8_t *ucp;
		int i;

		ucp = (uint8_t *) (buff + len);
	
		*ucp++  = ND_OPT_SOURCE_LINKADDR;
		*ucp++  = (uint8_t) ((iface->if_hwaddr_len + 16 + 63) >> 6);

		len += 2 * sizeof(uint8_t);

		i = (iface->if_hwaddr_len + 7) >> 3;
		memcpy(buff + len, iface->if_hwaddr, i);
		len += i;
	}

	iov.iov_len  = len;
	iov.iov_base = (caddr_t) buff;
	
	memset(chdr, 0, sizeof(chdr));
	cmsg = (struct cmsghdr *) chdr;
	
	cmsg->cmsg_len   = CMSG_LEN(sizeof(struct in6_pktinfo));
	cmsg->cmsg_level = IPPROTO_IPV6;
	cmsg->cmsg_type  = IPV6_PKTINFO;
	
	pkt_info = (struct in6_pktinfo *)CMSG_DATA(cmsg);
	pkt_info->ipi6_ifindex = iface->if_index;
	memcpy(&pkt_info->ipi6_addr, &iface->if_addr, sizeof(struct in6_addr));

	mhdr.msg_name = (caddr_t)&addr;
	mhdr.msg_namelen = sizeof(struct sockaddr_in6);
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;
	mhdr.msg_control = (void *) cmsg;
	mhdr.msg_controllen = sizeof(chdr);

	err = sendmsg(sock, &mhdr, 0);
	
	if (err < 0) {
		log(LOG_WARNING, "sendmsg: %s", strerror(errno));
	}
}