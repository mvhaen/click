/*
 * upstreamnotifier.{cc,hh} -- a nonfull_notifier
 * John Bicket
 *
 * Copyright (c) 2004 Massachusetts Institute of Technology
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
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include "upstreamnotifier.hh"
CLICK_DECLS

UpstreamNotifier::UpstreamNotifier()
  : Element(1, 1)
{
  MOD_INC_USE_COUNT;
}

UpstreamNotifier::~UpstreamNotifier()
{
  MOD_DEC_USE_COUNT;
}



void *
UpstreamNotifier::cast(const char *n)
{
 
  if (strcmp(n, "UpstreamNotifier") == 0) 
    return (Element *) this;
  else if (strcmp(n, Notifier::NONFULL_NOTIFIER) == 0)
    return static_cast<Notifier *>(&_notifier);
  else 
    return 0;
}
int
UpstreamNotifier::configure(Vector<String> &conf, ErrorHandler *errh)
{
  bool signal;

  _notifier.initialize(router());

  if (cp_va_parse(conf, this, errh,
		  cpBool, "Signal", &signal,
		  cpKeywords, 
		  cpEnd) < 0) {
    return -1;
  }

  _notifier.set_signal_active(signal);
  return 0;
}
void
UpstreamNotifier::push(int p, Packet *p_in)
{
  output(p).push(p_in);
}

enum {H_SIGNAL};

static String
read_param(Element *e, void *thunk)
{
  UpstreamNotifier *f = (UpstreamNotifier *)e;
  switch ((uintptr_t) thunk) {
  case H_SIGNAL: return String(f->_notifier.signal_active()) + "\n";
  }
  return String();
}
static int 
write_param(const String &in_s, Element *e, void *vparam,
		      ErrorHandler *errh)
{
  UpstreamNotifier *f = (UpstreamNotifier *)e;
  String s = cp_uncomment(in_s);
  switch((int)vparam) {
  case H_SIGNAL: {
    bool signal;
    if (!cp_bool(s, &signal)) 
      return errh->error("signal parameter must be boolean");
    f->_notifier.set_signal_active(signal);
    if (signal) {
      f->_notifier.wake_listeners();
    } else {
      f->_notifier.sleep_listeners();
    }

    break;
  }
  }
  return 0;
}
void
UpstreamNotifier::add_handlers()
{
  add_default_handlers(true);
  add_write_handler("signal", write_param, (void *) H_SIGNAL);
  add_read_handler("signal", read_param, (void *) H_SIGNAL);

}
CLICK_ENDDECLS
EXPORT_ELEMENT(UpstreamNotifier)