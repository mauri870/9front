/*
 * USB Human Interaction Device: keyboard and mouse.
 *
 * If there's no usb keyboard, it tries to setup the mouse, if any.
 * It should be started at boot time.
 *
 * Mouse events are converted to the format of mouse(3)
 * on mousein file.
 * Keyboard keycodes are translated to scan codes and sent to kbdfs(8)
 * on kbin file.
 *
 */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include "usb.h"
#include "hid.h"

enum
{
	Awakemsg=0xdeaddead,
	Diemsg = 0xbeefbeef,
};

enum
{
	Kbdelay = 500,
	Kbrepeat = 100,
};

typedef struct KDev KDev;
struct KDev
{
	Dev*	dev;		/* usb device*/
	Dev*	ep;		/* endpoint to get events */
	int	infd;		/* used to send events to kernel */
	Channel	*repeatc;	/* only for keyboard */

	/* report descriptor */
	int	nrep;
	uchar	rep[512];
};

/*
 * Map for the logitech bluetooth mouse with 8 buttons and wheels.
 *	{ ptr ->mouse}
 *	{ 0x01, 0x01 },	// left
 *	{ 0x04, 0x02 },	// middle
 *	{ 0x02, 0x04 },	// right
 *	{ 0x40, 0x08 },	// up
 *	{ 0x80, 0x10 },	// down
 *	{ 0x10, 0x08 },	// side up
 *	{ 0x08, 0x10 },	// side down
 *	{ 0x20, 0x02 }, // page
 * besides wheel and regular up/down report the 4th byte as 1/-1
 */

/*
 * scan codes >= 0x80 are extended (E0 XX)
 */
#define isext(sc)	((sc) >= 0x80)

/*
 * key code to scan code; for the page table used by
 * the logitech bluetooth keyboard.
 */
static char sctab[256] = 
{
[0x00]	0x0,	0x0,	0x0,	0x0,	0x1e,	0x30,	0x2e,	0x20,
[0x08]	0x12,	0x21,	0x22,	0x23,	0x17,	0x24,	0x25,	0x26,
[0x10]	0x32,	0x31,	0x18,	0x19,	0x10,	0x13,	0x1f,	0x14,
[0x18]	0x16,	0x2f,	0x11,	0x2d,	0x15,	0x2c,	0x2,	0x3,
[0x20]	0x4,	0x5,	0x6,	0x7,	0x8,	0x9,	0xa,	0xb,
[0x28]	0x1c,	0x1,	0xe,	0xf,	0x39,	0xc,	0xd,	0x1a,
[0x30]	0x1b,	0x2b,	0x2b,	0x27,	0x28,	0x29,	0x33,	0x34,
[0x38]	0x35,	0x3a,	0x3b,	0x3c,	0x3d,	0x3e,	0x3f,	0x40,
[0x40]	0x41,	0x42,	0x43,	0x44,	0x57,	0x58,	0xe3,	0x46,
[0x48]	0xf7,	0xd2,	0xc7,	0xc9,	0xd3,	0xcf,	0xd1,	0xcd,
[0x50]	0xcb,	0xd0,	0xc8,	0x45,	0x35,	0x37,	0x4a,	0x4e,
[0x58]	0x1c,	0xcf,	0xd0,	0xd1,	0xcb,	0xcc,	0xcd,	0xc7,
[0x60]	0xc8,	0xc9,	0xd2,	0xd3,	0x56,	0xff,	0xf4,	0xf5,
[0x68]	0xd5,	0xd9,	0xda,	0xdb,	0xdc,	0xdd,	0xde,	0xdf,
[0x70]	0xf8,	0xf9,	0xfa,	0xfb,	0x0,	0x0,	0x0,	0x0,
[0x78]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0xf1,
[0x80]	0xf3,	0xf2,	0x0,	0x0,	0x0,	0xfc,	0x0,	0x0,
[0x88]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x90]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0x98]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xa0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xa8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xb0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xb8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xc0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xc8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xd0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xd8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xe0]	0x1d,	0x2a,	0xb8,	0xfd,	0xe1,	0x36,	0xe4,	0xfe,
[0xe8]	0x0,	0x0,	0x0,	0x0,	0x0,	0xf3,	0xf2,	0xf1,
[0xf0]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
[0xf8]	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
};

static uchar ptrbootrep[] = {
0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 
0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 
0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 
0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 
0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 
0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 
0x81, 0x06, 0xc0, 0x09, 0x3c, 0x15, 0x00, 0x25, 
0x01, 0x75, 0x01, 0x95, 0x01, 0xb1, 0x22, 0x95, 
0x07, 0xb1, 0x01, 0xc0, 
};

static int debug;

static int
signext(int v, int bits)
{
	int s;

	s = sizeof(v)*8 - bits;
	v <<= s;
	v >>= s;
	return v;
}

static int
getbits(uchar *p, uchar *e, int bits, int off)
{
	int v, m;

	p += off/8;
	off %= 8;
	v = 0;
	m = 1;
	if(p < e){
		while(bits--){
			if(*p & (1<<off))
				v |= m;
			if(++off == 8){
				if(++p >= e)
					break;
				off = 0;
			}
			m <<= 1;
		}
	}
	return v;
}

enum {
	Ng	= RepCnt+1,
	UsgCnt	= Delim+1,	/* fake */
	Nl	= UsgCnt+1,
	Nu	= 256,
};

static uchar*
repparse1(uchar *d, uchar *e, int g[], int l[], int c,
	void (*f)(int t, int v, int g[], int l[], int c, void *a), void *a)
{
	int z, k, t, v, i;

	while(d < e){
		v = 0;
		t = *d++;
		z = t & 3, t >>= 2;
		k = t & 3, t >>= 2;
		switch(z){
		case 3:
			d += 4;
			if(d > e) continue;
			v = d[-4] | d[-3]<<8 | d[-2]<<16 | d[-1]<<24;
			break;
		case 2:
			d += 2;
			if(d > e) continue;
			v = d[-2] | d[-1]<<8;
			break;
		case 1:
			d++;
			if(d > e) continue;
			v = d[-1];
			break;
		}
		switch(k){
		case 0:	/* main item*/
			switch(t){
			case Collection:
				memset(l, 0, Nl*sizeof(l[0]));
				d = repparse1(d, e, g, l, v, f, a);
				continue;
			case CollectionEnd:
				return d;
			case Input:
			case Output:
			case Feature:
				if(l[UsgCnt] == 0 && l[UsagMin] != 0 && l[UsagMin] < l[UsagMax])
					for(i=l[UsagMin]; i<=l[UsagMax] && l[UsgCnt] < Nu; i++)
						l[Nl + l[UsgCnt]++] = i;
				for(i=0; i<g[RepCnt]; i++){
					if(i < l[UsgCnt])
						l[Usage] = l[Nl + i];
					(*f)(t, v, g, l, c, a);
				}
				break;
			}
			memset(l, 0, Nl*sizeof(l[0]));
			continue;
		case 1:	/* global item */
			if(t == Push){
				int w[Ng];
				memmove(w, g, sizeof(w));
				d = repparse1(d, e, w, l, c, f, a);
			} else if(t == Pop){
				return d;
			} else if(t < Ng){
				if(t == RepId)
					v &= 0xFF;
				else if(t == UsagPg)
					v &= 0xFFFF;
				else if(t != RepSize && t != RepCnt){
					v = signext(v, (z == 3) ? 32 : 8*z);
				}
				g[t] = v;
			}
			continue;
		case 2:	/* local item */
			if(l[Delim] != 0)
				continue;
			if(t == Delim){
				l[Delim] = 1;
			} else if(t < Delim){
				if(z != 3 && (t == Usage || t == UsagMin || t == UsagMax))
					v = (v & 0xFFFF) | (g[UsagPg] << 16);
				l[t] = v;
				if(t == Usage && l[UsgCnt] < Nu)
					l[Nl + l[UsgCnt]++] = v;
			}
			continue;
		case 3:	/* long item */
			if(t == 15)
				d += v & 0xFF;
			continue;
		}
	}
	return d;
}

/*
 * parse the report descriptor and call f for every (Input, Output
 * and Feature) main item as often as it would appear in the report
 * data packet.
 */
static void
repparse(uchar *d, uchar *e,
	void (*f)(int t, int v, int g[], int l[], int c, void *a), void *a)
{
	int l[Nl+Nu], g[Ng];

	memset(l, 0, sizeof(l));
	memset(g, 0, sizeof(g));
	repparse1(d, e, g, l, 0, f, a);
}

static int
setproto(KDev *f, int eid)
{
	int id, proto;

	proto = Bootproto;
	id = f->dev->usb->ep[eid]->iface->id;
	if(f->dev->usb->ep[eid]->iface->csp == PtrCSP){
		f->nrep = usbcmd(f->dev, Rd2h|Rstd|Riface, Rgetdesc, Dreport<<8, id, 
			f->rep, sizeof(f->rep));
		if(f->nrep > 0){
			if(debug){
				int i;

				fprint(2, "report descriptor:");
				for(i = 0; i < f->nrep; i++){
					if(i%8 == 0)
						fprint(2, "\n\t");
					fprint(2, "%#2.2ux ", f->rep[i]);
				}
				fprint(2, "\n");
			}
			proto = Reportproto;
		} else {
			f->nrep = sizeof(ptrbootrep);
			memmove(f->rep, ptrbootrep, f->nrep);
		}
	}
	return usbcmd(f->dev, Rh2d|Rclass|Riface, Setproto, proto, id, nil, 0);
}

static int
setleds(KDev* f, int, uchar leds)
{
	return usbcmd(f->dev, Rh2d|Rclass|Riface, Setreport, Reportout, 0, &leds, 1);
}

/*
 * Try to recover from a babble error. A port reset is the only way out.
 * BUG: we should be careful not to reset a bundle with several devices.
 */
static void
recoverkb(KDev *f)
{
	int i;

	close(f->dev->dfd);		/* it's for usbd now */
	devctl(f->dev, "reset");
	for(i = 0; i < 10; i++){
		sleep(500);
		if(opendevdata(f->dev, ORDWR) >= 0){
			setproto(f, f->ep->id);
			break;
		}
		/* else usbd still working... */
	}
}

static void
kbfree(KDev *kd)
{
	if(kd->infd >= 0)
		close(kd->infd);
	if(kd->ep != nil)
		closedev(kd->ep);
	if(kd->dev != nil)
		closedev(kd->dev);
	free(kd);
}

static void
kbfatal(KDev *kd, char *sts)
{
	if(sts != nil)
		fprint(2, "%s: fatal: %s\n", argv0, sts);
	else
		fprint(2, "%s: exiting\n", argv0);
	if(kd->repeatc != nil)
		sendul(kd->repeatc, Diemsg);
	kbfree(kd);
	threadexits(sts);
}

static void
kbprocname(KDev *kd, char *name)
{
	char buf[128];
	snprint(buf, sizeof(buf), "%s %s", name, kd->ep->dir);
	threadsetname(buf);
}

static void
sethipri(void)
{
	char fn[64];
	int fd;

	snprint(fn, sizeof(fn), "/proc/%d/ctl", getpid());
	fd = open(fn, OWRITE);
	if(fd < 0)
		return;
	fprint(fd, "pri 13");
	close(fd);
}

typedef struct Ptr Ptr;
struct Ptr
{
	int	x;
	int	y;
	int	z;

	int	b;

	int	absx;
	int	absy;
	int	absz;

	int	o;
	uchar	*e;
	uchar	p[128];
};

static void
ptrparse(int t, int f, int g[], int l[], int, void *a)
{
	int v, m;
	Ptr *p = a;

	if(t != Input)
		return;
	if(g[RepId] != 0){
		if(p->p[0] != g[RepId]){
			p->o = 0;
			return;
		}
		if(p->o < 8)
			p->o = 8;	/* skip report id byte */
	}
	v = getbits(p->p, p->e, g[RepSize], p->o);
	if(g[LogiMin] < 0)
		v = signext(v, g[RepSize]);
	if((f & (Fvar|Farray)) == Fvar && v >= g[LogiMin] && v <= g[LogiMax]){
		/*
		 * we use logical units below, but need the
		 * sign to be correct for mouse deltas.
		 * so if physical unit is signed but logical
		 * is unsigned, convert to signed but in logical
		 * units.
		 */
		if((f & (Fabs|Frel)) == Frel
		&& g[PhysMin] < 0 && g[PhysMax] > 0
		&& g[LogiMin] >= 0 && g[LogiMin] < g[LogiMax])
			v -= (g[PhysMax] * (g[LogiMax] - g[LogiMin])) / (g[PhysMax] - g[PhysMin]);

		switch(l[Usage]){
		case 0x090001:
			m = 1;
			goto Button;
		case 0x090002:
			m = 4;
			goto Button;
		case 0x090003:
			m = 2;
		Button:
			p->b &= ~m;
			if(v != 0)
				p->b |= m;
			break;
		case 0x010030:
			if((f & (Fabs|Frel)) == Fabs){
				p->x = (v - p->absx);
				p->absx = v;
			} else {
				p->x = v;
				p->absx += v;
			}
			break;
		case 0x010031:
			if((f & (Fabs|Frel)) == Fabs){
				p->y = (v - p->absy);
				p->absy = v;
			} else {
				p->y = v;
				p->absy += v;
			}
			break;
		case 0x010038:
			if((f & (Fabs|Frel)) == Fabs){
				p->z = (v - p->absz);
				p->absz = v;
			} else {
				p->z = v;
				p->absz += v;
			}
			p->b &= ~(8|16);
			if(p->z != 0)
				p->b |= (p->z > 0) ? 8 : 16;
			break;
		}
	}
	p->o += g[RepSize];
}

static void
ptrwork(void* a)
{
	char	err[ERRMAX];
	char	mbuf[80];
	int	c, nerrs;
	KDev*	f = a;
	Ptr	p;

	kbprocname(f, "ptr");
	sethipri();

	memset(&p, 0, sizeof(p));

	nerrs = 0;
	for(;;){
		if(f->ep == nil)
			kbfatal(f, nil);
		if(f->ep->maxpkt < 1 || f->ep->maxpkt > sizeof(p.p))
			kbfatal(f, "ptr: weird mouse maxpkt");

		memset(p.p, 0, sizeof(p.p));
		c = read(f->ep->dfd, p.p, f->ep->maxpkt);
		if(c <= 0){
			if(c < 0)
				rerrstr(err, sizeof(err));
			else
				strcpy(err, "zero read");
			if(++nerrs < 3){
				fprint(2, "%s: ptr: %s: read: %s\n", argv0, f->ep->dir, err);
				if(strstr(err, "babble") != 0)
					recoverkb(f);
				continue;
			}
			kbfatal(f, err);
		}
		nerrs = 0;

		p.o = 0;
		p.e = p.p + c;
		repparse(f->rep, f->rep+f->nrep, ptrparse, &p);

		seprint(mbuf, mbuf+sizeof(mbuf), "m%11d %11d %11d", p.x, p.y, p.b);
		if(write(f->infd, mbuf, strlen(mbuf)) < 0)
			kbfatal(f, "mousein i/o");
	}
}

static void
putscan(int fd, uchar sc, uchar up)
{
	uchar s[2] = {SCesc1, 0};

	if(sc == 0)
		return;
	s[1] = up | sc&Keymask;
	if(isext(sc))
		write(fd, s, 2);
	else
		write(fd, s+1, 1);
}

static void
putmod(int fd, uchar mods, uchar omods, uchar mask, uchar esc, uchar sc)
{
	uchar s[4], *p;

	p = s;
	if((mods&mask) && !(omods&mask)){
		if(esc)
			*p++ = SCesc1;
		*p++ = sc;
	}
	if(!(mods&mask) && (omods&mask)){
		if(esc)
			*p++ = SCesc1;
		*p++ = Keyup|sc;
	}
	if(p > s)
		write(fd, s, p - s);
}

static void
sleepproc(void* a)
{
	Channel *c = a;
	int ms;

	threadsetname("sleepproc");
	while((ms = recvul(c)) > 0)
		sleep(ms);
	chanfree(c);
}

static void
repeatproc(void* arg)
{
	KDev *f = arg;
	Channel *repeatc, *sleepc;
	int kbdinfd;
	ulong l, t;
	uchar sc;
	Alt a[3];

	repeatc = f->repeatc;
	kbdinfd = f->infd;
	threadsetname("repeatproc");
	
	sleepc = chancreate(sizeof(ulong), 0);
	if(sleepc != nil)
		proccreate(sleepproc, sleepc, Stack);

	a[0].c = repeatc;
	a[0].v = &l;
	a[0].op = CHANRCV;
	a[1].c = sleepc;
	a[1].v = &t;
	a[1].op = sleepc!=nil ? CHANSND : CHANNOP;
	a[2].c = nil;
	a[2].v = nil;
	a[2].op = CHANEND;

	l = Awakemsg;
	while(l != Diemsg){
		if(l == Awakemsg){
			l = recvul(repeatc);
			continue;
		}
		sc = l & 0xff;
		t = Kbdelay;
		if(alt(a) == 1){
			t = Kbrepeat;
			while(alt(a) == 1)
				putscan(kbdinfd, sc, 0);
		}
	}
	if(sleepc != nil)
		sendul(sleepc, 0);
	chanfree(repeatc);
	threadexits(nil);
}

static void
stoprepeat(KDev *f)
{
	sendul(f->repeatc, Awakemsg);
}

static void
startrepeat(KDev *f, uchar sc)
{
	sendul(f->repeatc, sc);
}

/*
 * This routine diffs the state with the last known state
 * and invents the scan codes that would have been sent
 * by a non-usb keyboard in that case. This also requires supplying
 * the extra esc1 byte as well as keyup flags.
 * The aim is to allow future addition of other keycode pages
 * for other keyboards.
 */
static uchar
putkeys(KDev *f, uchar buf[], uchar obuf[], int n, uchar dk)
{
	int i, j;
	uchar uk;
	int fd;

	fd = f->infd;
	putmod(fd, buf[0], obuf[0], Mctrl, 0, SCctrl);
	putmod(fd, buf[0], obuf[0], (1<<Mlshift), 0, SClshift);
	putmod(fd, buf[0], obuf[0], (1<<Mrshift), 0, SCrshift);
	putmod(fd, buf[0], obuf[0], Mcompose, 0, SCcompose);
	putmod(fd, buf[0], obuf[0], Maltgr, 1, SCcompose);

	/* Report key downs */
	for(i = 2; i < n; i++){
		for(j = 2; j < n; j++)
			if(buf[i] == obuf[j])
			 	break;
		if(j == n && buf[i] != 0){
			dk = sctab[buf[i]];
			putscan(fd, dk, 0);
			startrepeat(f, dk);
		}
	}

	/* Report key ups */
	uk = 0;
	for(i = 2; i < n; i++){
		for(j = 2; j < n; j++)
			if(obuf[i] == buf[j])
				break;
		if(j == n && obuf[i] != 0){
			uk = sctab[obuf[i]];
			putscan(fd, uk, Keyup);
		}
	}
	if(uk && (dk == 0 || dk == uk)){
		stoprepeat(f);
		dk = 0;
	}
	return dk;
}

static int
kbdbusy(uchar* buf, int n)
{
	int i;

	for(i = 1; i < n; i++)
		if(buf[i] == 0 || buf[i] != buf[0])
			return 0;
	return 1;
}

static void
kbdwork(void *a)
{
	uchar dk, buf[64], lbuf[64];
	int c, i, nerrs;
	char err[128];
	KDev *f = a;

	kbprocname(f, "kbd");

	f->repeatc = chancreate(sizeof(ulong), 0);
	if(f->repeatc == nil)
		kbfatal(f, "chancreate failed");
	proccreate(repeatproc, f, Stack);

	setleds(f, f->ep->id, 0);
	sethipri();

	memset(lbuf, 0, sizeof lbuf);
	dk = nerrs = 0;
	for(;;){
		if(f->ep == nil)
			kbfatal(f, nil);
		if(f->ep->maxpkt < 3 || f->ep->maxpkt > sizeof buf)
			kbfatal(f, "kbd: weird maxpkt");
		memset(buf, 0, sizeof buf);
		c = read(f->ep->dfd, buf, f->ep->maxpkt);
		if(c <= 0){
			if(c < 0)
				rerrstr(err, sizeof(err));
			else
				strcpy(err, "zero read");
			if(++nerrs < 3){
				fprint(2, "%s: kbd: %s: read: %s\n", argv0, f->ep->dir, err);
				if(strstr(err, "babble") != 0)
					recoverkb(f);
				continue;
			}
			kbfatal(f, err);
		}
		nerrs = 0;
		if(c < 3)
			continue;
		if(kbdbusy(buf + 2, c - 2))
			continue;
		if(usbdebug > 2 || debug > 1){
			fprint(2, "kbd mod %x: ", buf[0]);
			for(i = 2; i < c; i++)
				fprint(2, "kc %x ", buf[i]);
			fprint(2, "\n");
		}
		dk = putkeys(f, buf, lbuf, f->ep->maxpkt, dk);
		memmove(lbuf, buf, c);
	}
}

static void
kbstart(Dev *d, Ep *ep, char *infile, void (*f)(void*))
{
	KDev *kd;

	kd = emallocz(sizeof(KDev), 1);
	kd->infd = open(infile, OWRITE);
	if(kd->infd < 0){
		fprint(2, "%s: %s: open: %r\n", argv0, d->dir);
		goto Err;
	}
	incref(d);
	kd->dev = d;
	if(setproto(kd, ep->id) < 0){
		fprint(2, "%s: %s: setproto: %r\n", argv0, d->dir);
		goto Err;
	}
	kd->ep = openep(kd->dev, ep->id);
	if(kd->ep == nil){
		fprint(2, "%s: %s: openep %d: %r\n", argv0, d->dir, ep->id);
		goto Err;
	}
	if(opendevdata(kd->ep, OREAD) < 0){
		fprint(2, "%s: %s: opendevdata: %r\n", argv0, kd->ep->dir);
		goto Err;
	}
	procrfork(f, kd, Stack, RFNOTEG);
	return;
Err:
	kbfree(kd);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-d] devid\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char* argv[])
{
	int i;
	Dev *d;
	Ep *ep;
	Usbdev *ud;

	ARGBEGIN{
	case 'd':
		debug++;
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 1)
		usage();
	d = getdev(atoi(*argv));
	if(d == nil)
		sysfatal("getdev: %r");
	ud = d->usb;
	for(i = 0; i < nelem(ud->ep); i++){
		if((ep = ud->ep[i]) == nil)
			continue;
		if(ep->type == Eintr && ep->dir == Ein && ep->iface->csp == KbdCSP)
			kbstart(d, ep, "/dev/kbin", kbdwork);
		if(ep->type == Eintr && ep->dir == Ein && ep->iface->csp == PtrCSP)
			kbstart(d, ep, "/dev/mousein", ptrwork);
	}
	threadexits(nil);
}
