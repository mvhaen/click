#ifndef TOLINUXSNIFFERS_HH
#define TOLINUXSNIFFERS_HH
#include "element.hh"
#include "elements/linuxmodule/fromlinux.hh"

/*
 * =c
 * ToLinuxSniffers([DEVNAME])
 * =d
 *
 * Hands packets to any packet sniffers registered with Linux, such as packet
 * sockets. Expects packets with Ethernet headers.
 *
 * If DEVNAME is present, the packet is marked to appear as if it originated
 * from that network device.
 *
 * Packets are not passed to the ordinary Linux networking stack.
 * 
 * =a ToLinux
 * =a FromLinux
 * =a FromDevice
 * =a PollDevice
 * =a ToDevice */

class ToLinuxSniffers : public Element {

  struct device *_dev;
  
 public:
  
  ToLinuxSniffers();
  ~ToLinuxSniffers();
  
  const char *class_name() const		{ return "ToLinuxSniffers"; }
  const char *processing() const		{ return PUSH; }

  int configure_phase() const	{ return FromLinux::TODEVICE_CONFIGURE_PHASE; }
  int configure(const Vector<String> &, ErrorHandler *);
  ToLinuxSniffers *clone() const;
  
  void push(int port, Packet *);

};

#endif

