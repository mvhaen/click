// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * fromdevice.{cc,hh} -- element reads packets live from network via pcap
 * Douglas S. J. De Couto, Eddie Kohler, John Jannotti
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2001 International Computer Science Institute
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "fromdevice.hh"
#include <click/error.hh>
#include <click/confparse.hh>
#include <click/glue.hh>
#include <click/packet_anno.hh>
#include <unistd.h>
#include <fcntl.h>

#ifndef __sun
#include <sys/ioctl.h>
#else
#include <sys/ioccom.h>
#endif

#if FROMDEVICE_LINUX
# include <sys/socket.h>
# include <net/if.h>
# include <features.h>
# if __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 1
#  include <netpacket/packet.h>
#  include <net/ethernet.h>
# else
#  include <net/if_packet.h>
#  include <linux/if_packet.h>
#  include <linux/if_ether.h>
# endif
#endif

#include "fakepcap.hh"

CLICK_DECLS

FromDevice::FromDevice()
  : Element(0, 1), _promisc(0), _snaplen(0)
{
  MOD_INC_USE_COUNT;
#if FROMDEVICE_PCAP
  _pcap = 0;
#endif
#if FROMDEVICE_LINUX
  _fd = -1;
#endif
}

FromDevice::~FromDevice()
{
  MOD_DEC_USE_COUNT;
}

int
FromDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    bool promisc = false, outbound = false, sniffer = true;
    _snaplen = 2046;
    _force_ip = false;
    String bpf_filter;
    if (cp_va_parse(conf, this, errh,
		    cpString, "interface name", &_ifname,
		    cpOptional,
		    cpBool, "be promiscuous?", &promisc,
		    cpUnsigned, "maximum packet length", &_snaplen,
		    cpKeywords,
		    "SNIFFER", cpBool, "act as sniffer?", &sniffer,
		    "PROMISC", cpBool, "be promiscuous?", &promisc,
		    "SNAPLEN", cpUnsigned, "maximum packet length", &_snaplen,
		    "FORCE_IP", cpBool, "force IP packets?", &_force_ip,
		    "BPF_FILTER", cpString, "BPF filter", &bpf_filter,
		    "OUTBOUND", cpBool, "emit outbound packets?", &outbound,
		    cpEnd) < 0)
	return -1;
    if (_snaplen > 8190 || _snaplen < 14)
	return errh->error("maximum packet length out of range");
#if FROMDEVICE_PCAP
    _bpf_filter = bpf_filter;
#else
    if (bpf_filter)
	errh->warning("not using pcap library, BPF filter ignored");
#endif
    if (!sniffer)
	return errh->error("SNIFFER must be set to true for now");
    _promisc = promisc;
    _outbound = outbound;
    return 0;
}

#if FROMDEVICE_LINUX
int
FromDevice::open_packet_socket(String ifname, ErrorHandler *errh)
{
    int fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd == -1)
	return errh->error("%s: socket: %s", ifname.cc(), strerror(errno));

    // get interface index
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.cc(), sizeof(ifr.ifr_name));
    int res = ioctl(fd, SIOCGIFINDEX, &ifr);
    if (res != 0) {
	close(fd);
	return errh->error("%s: SIOCGIFINDEX: %s", ifname.cc(), strerror(errno));
    }
    int ifindex = ifr.ifr_ifindex;

    // bind to the specified interface.  from packet man page, only
    // sll_protocol and sll_ifindex fields are used; also have to set
    // sll_family
    sockaddr_ll sa;
    memset(&sa, 0, sizeof(sa));
    sa.sll_family = AF_PACKET;
    sa.sll_protocol = htons(ETH_P_ALL);
    sa.sll_ifindex = ifindex;
    res = bind(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (res != 0) {
	close(fd);
	return errh->error("%s: bind: %s", ifname.cc(), strerror(errno));
    }

    // nonblocking I/O on the packet socket so we can poll
    fcntl(fd, F_SETFL, O_NONBLOCK);
  
    return fd;
}

int
FromDevice::set_promiscuous(int fd, String ifname, bool promisc)
{
    // get interface flags
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.cc(), sizeof(ifr.ifr_name));
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0)
	return -2;
    int was_promisc = (ifr.ifr_flags & IFF_PROMISC ? 1 : 0);

    // set or reset promiscuous flag
#ifdef SOL_PACKET
    if (ioctl(fd, SIOCGIFINDEX, &ifr) != 0)
	return -2;
    struct packet_mreq mr;
    memset(&mr, 0, sizeof(mr));
    mr.mr_ifindex = ifr.ifr_ifindex;
    mr.mr_type = (promisc ? PACKET_MR_PROMISC : PACKET_MR_ALLMULTI);
    if (setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0)
	return -3;
#else
    if (was_promisc != promisc) {
	ifr.ifr_flags = (promisc ? ifr.ifr_flags | IFF_PROMISC : ifr.ifr_flags & ~IFF_PROMISC);
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0)
	    return -3;
    }
#endif

    return was_promisc;
}
#endif /* FROMDEVICE_LINUX */

int
FromDevice::initialize(ErrorHandler *errh)
{
    if (!_ifname)
	return errh->error("interface not set");

    /* 
     * Later versions of pcap distributed with linux (e.g. the redhat
     * linux pcap-0.4-16) want to have a filter installed before they
     * will pick up any packets.
     */

#if FROMDEVICE_PCAP
  
    assert(!_pcap);
    char *ifname = _ifname.mutable_c_str();
    char ebuf[PCAP_ERRBUF_SIZE];
    _pcap = pcap_open_live(ifname, _snaplen, _promisc,
			   1,     /* timeout: don't wait for packets */
			   ebuf);
    // Note: pcap error buffer will contain the interface name
    if (!_pcap)
	return errh->error("%s", ebuf);

    // nonblocking I/O on the packet socket so we can poll
    int pcap_fd = fd();
    fcntl(pcap_fd, F_SETFL, O_NONBLOCK);

#ifdef BIOCSSEESENT
    {
	int accept = _outbound;
	if (ioctl(pcap_fd, BIOCSSEESENT, &accept) != 0)
	    return errh->error("FromDevice: BIOCSEESENT failed");
    }
#endif

#if defined(BIOCIMMEDIATE) && !defined(__sun) // pcap/bpf ioctl, not in DLPI/bufmod
    {
	int yes = 1;
	if (ioctl(pcap_fd, BIOCIMMEDIATE, &yes) != 0)
	    return errh->error("FromDevice: BIOCIMMEDIATE failed");
    }
#endif

    bpf_u_int32 netmask;
    bpf_u_int32 localnet;
    if (pcap_lookupnet(ifname, &localnet, &netmask, ebuf) < 0)
	errh->warning("%s", ebuf);
  
    // compile the BPF filter
    struct bpf_program fcode;
    if (pcap_compile(_pcap, &fcode, _bpf_filter.mutable_c_str(), 0, netmask) < 0)
	return errh->error("%s: %s", ifname, pcap_geterr(_pcap));
    if (pcap_setfilter(_pcap, &fcode) < 0)
	return errh->error("%s: %s", ifname, pcap_geterr(_pcap));

    add_select(pcap_fd, SELECT_READ);

    _datalink = pcap_datalink(_pcap);
    if (_force_ip && !fake_pcap_dlt_force_ipable(_datalink))
	errh->warning("%s: strange data link type %d, FORCE_IP will not work", ifname, _datalink);

#elif FROMDEVICE_LINUX

    _fd = open_packet_socket(_ifname, errh);
    if (_fd < 0)
	return -1;

    int promisc_ok = set_promiscuous(_fd, _ifname, _promisc);
    if (promisc_ok < 0) {
	if (_promisc)
	    errh->warning("cannot set promiscuous mode");
	_was_promisc = -1;
    } else
	_was_promisc = promisc_ok;

    add_select(_fd, SELECT_READ);

    _datalink = FAKE_DLT_EN10MB;

#else

    return errh->error("FromDevice is not supported on this platform");
  
#endif
    
    return 0;
}

void
FromDevice::cleanup(CleanupStage)
{
#if FROMDEVICE_LINUX
  if (_fd >= 0) {
    if (_was_promisc >= 0)
	set_promiscuous(_fd, _ifname, _was_promisc);
    close(_fd);
    _fd = -1;
  }
#elif FROMDEVICE_PCAP
  if (_pcap) {
      pcap_close(_pcap);
      _pcap = 0;
  }
#endif
}

#if FROMDEVICE_PCAP
CLICK_ENDDECLS
extern "C" {
void
FromDevice_get_packet(u_char* clientdata,
		      const struct pcap_pkthdr* pkthdr,
		      const u_char* data)
{
    static char bcast_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    FromDevice *fd = (FromDevice *) clientdata;
    int length = pkthdr->caplen;
#if defined(__sparc)
    // Packet::make(data,length) allocates new buffer to install
    // DEFAULT_HEADROOM (28 bytes). Thus data winds up on a 4 byte
    // boundary, irrespective of its original alignment. We assume we
    // want a two byte offset from a four byte boundary (DLT_EN10B).
    //
    // Furthermore, note that pcap-dlpi on Solaris uses bufmod by
    // default, hence while pcap-dlpi.pcap_read() is careful to load
    // the initial read from the stream head into a buffer aligned
    // appropriately for the network interface type, (I believe)
    // subsequent packets in the batched read will be copied from the
    // Stream's byte sequence into the pcap-dlpi user-level buffer at
    // arbitrary alignments.
    Packet *p = Packet::make(data - 2, length + 2);
    p->pull(2); 
#else
    Packet *p = Packet::make(data, length);
#endif

    // set packet type annotation
    if (p->data()[0] & 1) {
	if (memcmp(bcast_addr, p->data(), 6) == 0)
	    p->set_packet_type_anno(Packet::BROADCAST);
	else
	    p->set_packet_type_anno(Packet::MULTICAST);
    }

    // set annotations
    p->set_timestamp_anno(Timestamp::make_usec(pkthdr->ts.tv_sec, pkthdr->ts.tv_usec));
    SET_EXTRA_LENGTH_ANNO(p, pkthdr->len - length);

    if (!fd->_force_ip || fake_pcap_force_ip(p, fd->_datalink))
	fd->output(0).push(p);
    else
	p->kill();
}
}
CLICK_DECLS
#endif

void
FromDevice::selected(int)
{
#ifdef FROMDEVICE_PCAP
    // Read and push() at most one packet.
    pcap_dispatch(_pcap, 1, FromDevice_get_packet, (u_char *) this);
#endif
#ifdef FROMDEVICE_LINUX
    struct sockaddr_ll sa;
    socklen_t fromlen = sizeof(sa);
    // store data offset 2 bytes into the packet, assuming that first 14
    // bytes are ether header, and that we want remaining data to be
    // 4-byte aligned.  this assumes that _packetbuf is 4-byte aligned,
    // and that the buffer allocated by Packet::make is also 4-byte
    // aligned.  Actually, it doesn't matter if the packet is 4-byte
    // aligned; perhaps there is some efficiency aspect?  who cares....
    WritablePacket *p = Packet::make(2, 0, _snaplen, 0);
    int len = recvfrom(_fd, p->data(), p->length(), MSG_TRUNC, (sockaddr *)&sa, &fromlen);
    if (len > 0 && (sa.sll_pkttype != PACKET_OUTGOING || _outbound)) {
	if (len > _snaplen) {
	    assert(p->length() == (uint32_t)_snaplen);
	    SET_EXTRA_LENGTH_ANNO(p, len - _snaplen);
	} else
	    p->take(_snaplen - len);
	p->set_packet_type_anno((Packet::PacketType)sa.sll_pkttype);
	p->timestamp_anno().set_timeval_ioctl(_fd, SIOCGSTAMP);
	if (!_force_ip || fake_pcap_force_ip(p, _datalink))
	    output(0).push(p);
	else
	    p->kill();
    } else {
	p->kill();
	if (len <= 0 && errno != EAGAIN)
	    click_chatter("FromDevice(%s): recvfrom: %s", _ifname.cc(), strerror(errno));
    }
#endif
}

void
FromDevice::kernel_drops(bool& known, int& max_drops) const
{
#ifdef FROMDEVICE_PCAP
    struct pcap_stat stats;
    if (pcap_stats(_pcap, &stats) >= 0)
	known = true, max_drops = stats.ps_drop;
    else
	known = false, max_drops = -1;
#elif defined(FROMDEVICE_LINUX)
    // You might be able to do this better by parsing netstat/ifconfig output,
    // but for now, we just give up.
    known = false, max_drops = -1;
#else
    // Drop statistics unknown.
    known = false, max_drops = -1;
#endif
}

String
FromDevice::read_kernel_drops(Element* e, void*)
{
    FromDevice* fd = static_cast<FromDevice*>(e);
    int max_drops;
    bool known;
    fd->kernel_drops(known, max_drops);
    if (known)
	return String(max_drops) + "\n";
    else if (max_drops >= 0)
	return "<" + String(max_drops) + "\n";
    else
	return "??\n";
}

String
FromDevice::read_encap(Element* e, void*)
{
    FromDevice* fd = static_cast<FromDevice*>(e);
    return String(fake_pcap_unparse_dlt(fd->_datalink)) + "\n";
}

void
FromDevice::add_handlers()
{
    add_read_handler("kernel_drops", read_kernel_drops, 0);
    add_read_handler("encap", read_encap, 0);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel FakePcap)
EXPORT_ELEMENT(FromDevice)
