/*=======================================================================
 *	Convert values into a different format
 *	$Id: cconv.c,v 1.13 2022/04/29 07:39:03 chah Exp $
 *	Stream of character strings packed into binary data according to
 *	input specification, then converted back to strings according to
 *	output specification
 *
 *	Output				Input
 *	-i	integer (default)	-I	integer
 *	-l	long			-L	long
 *	-h	short			-H	short
 *	-c	char values		-C	char values
 *	-f	float			-F	float
 *	-g	double			-G	double
 *	-s	string			-S	string
 *	-b	BCN			-B	BCN
 *	-p	pointname		-P	pointname
 *	-j	Nord float		-J	Nord float
 *	-z	binary			-Z	binary
 *	-o	octal			-O	octal
 *	-d	decimal			-D	decimal
 *	-x	hexadecimal		-X	hexadecimal
 *	-u	unsigned/UTC		-U	unsigned (no-op)/UTC
 *	-r	raw binary		-R	raw binary
 *	-y	date			-Y	date
 *
 *	type:	.bc..fghij.l...p..s.....y.
 *	mod:	....e...............u.....
 *	style:	...d..........o........x.z
 *	repr:	............mn..qr.t......
 *	unused: a.........k..........vw...
 *
 * (	-q	quote strings		-Q	allow quoted strings
 * (	-t	Tcl quoting		-T	allow Tcl quoting
 *	-e	byte-swap		-E	byte-swap
 *	-m	multiple per line	-M	multiple per line
 *	-N	read from named file
 *=======================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <memory.h>
#ifdef __sunos5__
# define HAVE_NORDFLOAT 1
# define HAVE_POINT 1
# define HAVE_BCN 1
#endif
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <time.h>

#if HAVE_NORDFLOAT
# include <csl3.h>			/* cf32to48_ etc. */
#endif
#if HAVE_POINT
# include <cddefs.h>
# include <csl9.h>
#endif
/* Avoid need to call csl4 functions */
#define cmgbcn(bcn,b,c,n) (*(b) = (bcn)>>11, *(c)=((bcn)>>5)&63, *(n)=(bcn)&31)
#define cmdbcn(bcn,b,c,n) (*(bcn) = (((b)&15)<<11) | (((c)&63)<<5) | ((n)&31))

#ifdef __sunos5__
/*--- Ancient gcc, lacks builtins */
static uint16_t bswap16(uint16_t x);
static uint32_t bswap32(uint32_t x);
static uint64_t bswap64(uint64_t x);
#else
#define bswap16 __builtin_bswap16
#define bswap32 __builtin_bswap32
#define bswap64 __builtin_bswap64
#endif

const char usage[] = "Usage: %s [-chilfdsbjpryuzodx] [-CHILFDSBJPRYUZODX] value... | -N filename\n\
  lower case option = convert to,  upper case = convert from\n\
  types:\n\
    i=integer   l=long      h=short     c=char      f=float     d=double\n\
    s=string    b=BCN       p=pointname j=Nordfloat y=date      r=raw\n\
  modifiers:\n\
    u=unsigned  e=byteswap\n\
  styles:\n\
    z=binary    o=octal     d=decimal   x=hex\n\
";

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define NEW(t) (t *)new(sizeof(t))

static void *new(int);
static void fail(const char *, ...);

/*-----------------------------------------------------------------------
 *	Conversion control
 *-----------------------------------------------------------------------*/

enum Type {
  TYPE_RAW,
  TYPE_CHAR,
  TYPE_SHORT,
  TYPE_INT,
  TYPE_LONG,
  TYPE_FLOAT,
  TYPE_DOUBLE,
  TYPE_STRING,
#if HAVE_POINT
  TYPE_POINT,
#endif
#if HAVE_BCN
  TYPE_BCN,
#endif
#if HAVE_NORDFLOAT
  TYPE_NORDFLOAT,
#endif
  TYPE_DATE
};

enum Style {
  STYLE_DEFAULT,
  STYLE_BINARY,
  STYLE_OCTAL,
  STYLE_DECIMAL,
  STYLE_HEX
};

enum Quoting {
  QUOTING_NONE,
  QUOTING_SHELL,
  QUOTING_TCL
};

typedef struct {
  enum Type type;
  enum Style style;
  enum Quoting quoting;
  int unsignedp;			/* Also UTC for date */
  int byteswap;
} Conversion;

static Conversion *conversion_create(void)
{
  Conversion *this = NEW(Conversion);
  this->type = TYPE_INT;
  this->style = STYLE_DEFAULT;
  this->quoting = QUOTING_NONE;
  this->unsignedp = FALSE;
  this->byteswap = FALSE;
  return this;
}

typedef union {
  char cval;
  short sval;
  int ival;
  long lval;				/* 32-bit or 64-bit per architecture */
  float fval;
  double dval;
  time_t time;
#if HAVE_POINT
  pknam_t pknam;
#endif
#if HAVE_NORDFLOAT
  short nf[3];
#endif
  uint32_t u32;
  uint64_t u64;
  char bytes[8];
} Uscalar;

/*=======================================================================
 *	Data producer stream
 *	Returns pointer to data, and size (may be what asked for, or not).
 *	Returns -1 when end reached.
 *=======================================================================*/
typedef int(*Producer)(void *, char **, int);

static char *progname;

/*-----------------------------------------------------------------------
 *	Raw data producer from file
 *-----------------------------------------------------------------------*/
typedef struct {
  const char *filename;
  FILE *f;
  int eof;
} FileStream;

static void *file_create(const char *filename)
{
  FileStream *this = NEW(FileStream);
  this->filename = strcpy(new(strlen(filename)+1), filename);
  if (strcmp(filename, "-") == 0) {
    this->f = stdin;
  } else {
    this->f = fopen(filename, "rb");
    if (this->f == NULL) {
      fail("Cannot open %s for read", filename);
    }
  }
  this->eof = FALSE;
  return this;
}

static int file_get(void *closure, char **data, int size)
{
  FileStream *this = closure;
  char *buf;
  int nread;
  if (this->eof) return -1;
  buf = new(size);
  if (fgets(buf, size, this->f) == NULL) {
    this->eof = TRUE;
    free(buf);
    return -1;
  } else {
    nread = strlen(buf);
    *data = realloc(buf, nread+1);
    return nread;
  }
}

static int file_get_raw(void *closure, char **data, int size)
{
  FileStream *this = closure;
  char *buf;
  int nread;
  if (this->eof) return -1;
  buf = new(size);
  nread = fread(buf, 1, size, this->f);
  if (nread <= 0) {
    this->eof = TRUE;
    free(buf);
    return -1;
  } else {
    *data = buf;
    return nread;
  }
}

/*-----------------------------------------------------------------------
 *	String producer from argv.
 *	May return more than asked for.
 *-----------------------------------------------------------------------*/
typedef struct {
  int argc;
  char **argv;
} ArgvStream;

static void *argv_create(int argc, char **argv)
{
  ArgvStream *this = NEW(ArgvStream);
  this->argc = argc;
  this->argv = argv;
  return this;
}

static int argv_get(void *closure, char **data, int size)
{
  ArgvStream *this = closure;
  if (this->argc <= 0) {
    return -1;
  }
  this->argc--;
  *data = *(this->argv)++;
  return strlen(*data);
}

/*-----------------------------------------------------------------------
 *	Size reducer stream
 *	Child producer may give more than we want; make sure we don't
 *	return more than wanted to caller.
 *-----------------------------------------------------------------------*/
typedef struct {
  Producer child;
  void *closure;
  char *last;
  int size;
  int offset;
} Reducer;

static void *reducer_create(Producer child, void *closure)
{
  Reducer *this = NEW(Reducer);
  this->child = child;
  this->closure = closure;
  this->last = NULL;
  this->size = 0;
  this->offset = 0;
  return this;
}

static int reducer_get(void *closure, char **data, int size)
{
  Reducer *this = closure;
  int num;
  while (this->offset >= this->size) {
    if (this->size == -1) return -1;
    this->size = this->child(this->closure, &this->last, 65536);
    this->offset = 0;
  }
  *data = this->last + this->offset;
  num = this->size - this->offset;
  if (num > size) num = size;
  this->offset += num;
  return num;
}

/*-----------------------------------------------------------------------
 *	Data expander
 *	Ensure you always get as much as you ask for
 *-----------------------------------------------------------------------*/
typedef struct {
  Producer child;
  void *closure;
  char *buffer;
  int size;
} Expander;

static void *expander_create(Producer child, void *closure)
{
  Expander *this = NEW(Expander);
  this->child = child;
  this->closure = closure;
  this->buffer = new(this->size = 40);
  return this;
}

static int expander_get(void *closure, char **data, int size)
{
  Expander *this = closure;
  int done, num;
  char *in;

  num = this->child(this->closure, &in, size);
  if (num < 0) return -1;
  if (num < size) {
    if (size > this->size) {
      free(this->buffer);
      this->buffer = new(this->size = size); /* Should realloc really */
    }
    memcpy(this->buffer, in, num);
    done = num;
    while (done < size) {
      num = this->child(this->closure, &in, size-done);
      if (num < 0) {
	memset(this->buffer + done, 0, size-done);
	done = size;
      } else {
	memcpy(this->buffer + done, in, num);
	done += num;
      }
    }
    *data = this->buffer;
    return done;
  } else {
    *data = in;
    return num;
  }
}

/*-----------------------------------------------------------------------
 *	Convert from text to long
 *	Allow either signed or unsigned
 *	N.B. strto[u]l set only only on error, so to distinguish from
 *	legitimate LONG_MAX etc., must clear errno first - see man page.
 *-----------------------------------------------------------------------*/
static long lax_strtol(const char *text, char **rest, int base)
{
  long lval;
  errno = 0;
  lval = strtol(text, rest, base);
  if ((lval == LONG_MAX || lval == LONG_MIN) && errno == ERANGE) {
    /*--- Not valid signed int - try unsigned */
    errno = 0;
    lval = strtoul(text, rest, base);
    if (lval == ULONG_MAX && errno == ERANGE) {
      lval = 0;
    }
  }
  return lval;
}
#define STRTOL lax_strtol

static int amatch(const char *str, char const * const *vals, int nval)
{
  int i;
  for (i=0; i<nval; i++) {
    if (strcmp(str, vals[i]) == 0) return i;
  }
  return -1;
}

/*-----------------------------------------------------------------------
 *	Interpret randomly formatted date/time and convert to seconds
 *	since epoch (Jan 1 1970).
 *-----------------------------------------------------------------------*/
static int text2date(const char *str, time_t *p_time, int utc)
{
  static char const * const wday[7] = {
    "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
  };
  static char const * const mon[12] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
  };
  static char const * const month[12] = {
    "January","February","March","April","May","June",
    "July","August","Sepember","October","November","December"
  };
  time_t now = time(NULL);
  struct tm date = *localtime(&now);
  int i1, i2, i3, i4, i5, i6;
  char *copystr, *tok, c;
  int have_year=0, have_month=0, have_day=0, have_hour=0, have_min=0, have_sec=0, have_dst=0;
  copystr = strcpy(malloc(strlen(str)+1), str);	/* strdup is not POSIX */
  for (tok = strtok(copystr, " ,"); tok != NULL; tok = strtok(NULL, " ,")) {
    int len = strlen(tok);
    if ((i1 = amatch(tok, wday, 7)) >= 0) {
      /* Redundant - ignore */
    } else if ((i1 = amatch(tok, mon, 12)) >= 0) {
      date.tm_mon = i1; have_month = TRUE;
    } else if ((i1 = amatch(tok, month, 12)) >= 0) {
      date.tm_mon = i1; have_month = TRUE;
    } else if (((len == 20 &&
		 sscanf(tok, "%4d-%2d-%2dT%2d:%2d:%2dZ%c",
			&i1, &i2, &i3, &i4, &i5, &i6, &c) == 6) ||
		(len == 16 &&
		 sscanf(tok, "%4d%2d%2dT%2d%2d%2dZ%c",
			&i1, &i2, &i3, &i4, &i5, &i6, &c) == 6)) &&
	       i1 >= 1970 && i1 < 2038 &&
	       i2 >= 1 && i2 < 12 &&
	       i3 >= 1 && i3 < 31 &&
	       i4 >= 0 && i4 < 24 &&
	       i5 >= 0 && i5 < 60 &&
	       i6 >= 0 && i6 < 60) {	/* Disallow leap seconds */
      date.tm_year = i1 - 1900; have_year = TRUE;
      date.tm_mon  = i2 - 1; have_month = TRUE;
      date.tm_mday = i3; have_day = TRUE;
      date.tm_hour = i4; have_hour = TRUE;
      date.tm_min  = i5; have_min = TRUE;
      date.tm_sec  = i6; have_sec = TRUE;
      date.tm_isdst = 0; have_dst = TRUE;
    } else if (len == 8 &&
	       sscanf(tok, "%2d:%2d:%2d%c", &i1, &i2, &i3, &c) == 3 &&
	       i1 >= 0 && i1 < 24 &&
	       i2 >= 0 && i2 < 60 &&
	       i3 >= 0 && i3 < 60) {	/* Disallow leap seconds */
      date.tm_hour = i1; have_hour = TRUE;
      date.tm_min = i2; have_min = TRUE;
      date.tm_sec = i3; have_sec = TRUE;
    } else if (len == 5 &&
	       sscanf(tok, "%2d:%2d%c", &i1, &i2, &c) == 2 &&
	       i1 >= 0 && i1 < 24 &&
	       i2 >= 0 && i2 < 60) {
      date.tm_hour = i1; have_hour = TRUE;
      date.tm_min = i2; have_min = TRUE;
    } else if (sscanf(tok, "%d%c", &i1, &c) == 1 &&
	       i1 >= 1 && i1 <= 31) {
      date.tm_mday = i1; have_day = TRUE;
    } else if (sscanf(tok, "%d%c", &i1, &c) == 1 &&
	       i1 >= 1970 && i1 < 2038) {
      date.tm_year = i1 - 1900; have_year = TRUE;
    } else if (sscanf(tok, "%d-%d-%d%c", &i1, &i2, &i3, &c) == 3 &&
	       i1 >= 1970 && i1 < 2038 &&
	       i2 >= 1 && i2 < 12 &&
	       i3 >= 1 && i3 < 31) {
      date.tm_year = i1 - 1900; have_year = TRUE;
      date.tm_mon = i2 - 1; have_month = TRUE;
      date.tm_mday = i3; have_day = TRUE;
    } else if (sscanf(tok, "%d/%d/%d%c", &i1, &i2, &i3, &c) == 3 &&
	       ((i1 >= 1970 && i1 < 2038) ||
		(i1 >= 70 && i1 <= 99 && (i1 += 1900)) ||
		(i1 >= 0 && i1 < 38 && (i1 += 2000))) &&
	       i2 >= 1 && i2 < 12 &&
	       i3 >= 1 && i3 < 31) {
      date.tm_year = i1 - 1900; have_year = TRUE;
      date.tm_mon = i2 - 1; have_month = TRUE;
      date.tm_mday = i3; have_day = TRUE;
    } else if (sscanf(tok, "%d/%d%c", &i1, &i2, &c) == 3 &&
	       i1 >= 1 && i1 < 12 &&
	       i2 >= 1 && i2 < 31) {
      date.tm_mon = i1 - 1; have_month = TRUE;
      date.tm_mday = i2; have_day = TRUE;
    } else if (strcmp(tok, "GMT")==0) {
      date.tm_isdst = 0; have_dst = TRUE;
    } else if (strcmp(tok, "BST")==0) {
      date.tm_isdst = 1; have_dst = TRUE;
    } else {
      free(copystr);
      return FALSE;
    }
  }
  /*--- If date given, assume midnight unless time given too */
  if (have_year || have_month || have_day) {
    if (!have_hour) date.tm_hour = 0;
    if (!have_min) date.tm_min = 0;
    if (!have_sec) date.tm_sec = 0;
  } else {
    /*--- Just a time today, but "" -> now */
    if (have_min && !have_sec) date.tm_sec = 0;
  }
  if (! have_dst) {
    if (utc) {
      date.tm_isdst = 0;		/* Assume UK standard time is UTC */
    } else {
      date.tm_isdst = -1;		/* Let mktime work it out */
    }
  }
  *p_time = mktime(&date);
  free(copystr);
  return TRUE;
}
  
/*-----------------------------------------------------------------------
 *	Input converter
 *	Reads raw (binary) data from a producer via a conversion.
 *-----------------------------------------------------------------------*/
typedef struct {
  Producer child;
  void *closure;
  Conversion *conv;
  Uscalar u;
} Inconv;

static void *inconv_create(Conversion *conv, Producer child, void *closure)
{
  Inconv *this = NEW(Inconv);
  this->child = child;
  this->closure = closure;
  this->conv = conv;
  return this;
}

static int inconv_get(void *closure, char **data, int size)
{
  Inconv *this = closure;
  Conversion *conv = this->conv;
  char *str, *end;
  int num = 0;
  long lval = 0;

  num = this->child(this->closure, &str, 1024);
  if (num < 0) return -1;

  switch (conv->type) {
  case TYPE_CHAR: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    switch (conv->style) {
    case STYLE_DEFAULT:
      lval = STRTOL(str, &end, 0);
      break;
    case STYLE_BINARY:
      lval = 0L;
      while (*str == '0' || *str == '1') {
	lval *= 2;
	if (*str == '1') lval += 1;
	str++;
      }
      break;
    case STYLE_OCTAL:
      lval = STRTOL(str, &end, 8);
      break;
    case STYLE_DECIMAL:
      lval = STRTOL(str, &end, 10);
      break;
    case STYLE_HEX:
      lval = STRTOL(str, &end, 16);
      break;
    }
    switch (conv->type) {
    case TYPE_CHAR:
      this->u.cval = lval;
      *data = this->u.bytes;
      num = sizeof(char);
      break;
    case TYPE_SHORT:
      if (conv->byteswap) {
	this->u.sval = bswap16(lval);
      } else {
	this->u.sval = lval;
      }
      *data = this->u.bytes;
      num = sizeof(short);
      break;
    case TYPE_INT:
      if (conv->byteswap) {
	this->u.ival = bswap32(lval);
      } else {
	this->u.ival = lval;
      }
      *data = this->u.bytes;
      num = sizeof(int);
      break;
    case TYPE_LONG:
      if (conv->byteswap) {
	if (sizeof(long) == 8) {	/* x86_64 */
	  this->u.lval = bswap64(lval);
	} else {
	  this->u.lval = bswap32(lval);
	}
      } else {
	this->u.lval = lval;
      }
      *data = this->u.bytes;
      num = sizeof(long);
      break;
    default:;
    }
    break;
  case TYPE_FLOAT:
    this->u.fval = strtod(str, &end);
    if (conv->byteswap) {
      this->u.u32 = bswap32(this->u.u32); /* Assume u32 aliases fval */
    }
    *data = this->u.bytes;
    num = sizeof(float);
    break;
  case TYPE_DOUBLE:
    this->u.dval = strtod(str, &end);
    if (conv->byteswap) {
      this->u.u64 = bswap64(this->u.u64); /* Assume u64 aliases dval */
    }
    *data = this->u.bytes;
    num = sizeof(double);
    break;
  case TYPE_STRING:
    switch (conv->quoting) {
    case QUOTING_NONE:
      num = strlen(str);
      *data = str;
      break;
    case QUOTING_SHELL:
#if NOTYET
      num = shell_unquote(str, buffer);
#else
      fail("Shell quoting not implemented yet");
#endif
      break;
    case QUOTING_TCL:
#if NOTYET
      num = tcl_unquote(str, buffer);
#else
      fail("Tcl quoting not implemented yet");
#endif
      break;
    } break;
#if HAVE_POINT
  case TYPE_POINT: {
    c9error_jt err;
    err = cdnmpak(this->u.pknam, str);
    if (err != 0) {
      c9report(EVSTM, err, NULL);
      exit(1);
    }
    if (conv->byteswap) {
      int i;
      for (i = 0; i < sizeof(this->u.pknam) / sizeof(this->u.pknam[0]); i++) {
	this->u.pknam[i] = bswap16(this->u.pknam[i]);
      }
    }
    *data = this->u.bytes;
    num = sizeof(this->u.pknam);
  } break;
#endif
#if HAVE_BCN
  case TYPE_BCN: {
    int b, c, n;
    if (sscanf(str, "%d %d %d", &b, &c, &n) == 3 ||
	sscanf(str, "%d,%d,%d", &b, &c, &n) == 3) {
      cmdbcn(&this->u.sval, b, c, n);
      if (conv->byteswap) {
	this->u.sval = bswap16(this->u.sval);
      }
      *data = this->u.bytes;
      num = sizeof(short);
    } else {
      fail("Unrecognised BCN");
    }
  } break;
#endif
#if HAVE_NORDFLOAT
  case TYPE_NORDFLOAT: {
    union {
      float fval;
      short shalf[2];
    } ieee;
    ieee.fval = strtod(str, &end);
#ifdef __linux__
    {
      /* Assume the "native" byte ordering for Nordfloat is three shorts
       * in the same order as SunOS but each in native byte-order.
       */
      /*--- First convert into two shorts, big end first,
       * ready for conversion.
       * E.g. 3.14159265 -> 0x40490fdb -> [0x4049, 0x0fdb]
       */
      short hilo[2];
      hilo[0] = ieee.shalf[1];
      hilo[1] = ieee.shalf[0];

      /*--- Do the conversion e.g. -> [0x4002, 0xc90f, 0xdb00] */
      cf32to48_(hilo, this->u.nf);
    }
#else
    cf32to48_(ieee.shalf, this->u.nf);
#endif
    if (conv->byteswap) {
      this->u.nf[0] = bswap16(this->u.nf[0]);
      this->u.nf[1] = bswap16(this->u.nf[1]);
      this->u.nf[2] = bswap16(this->u.nf[2]);
    }
    *data = this->u.bytes;
    num = sizeof(this->u.nf);
  } break;
#endif
  case TYPE_DATE: {
    if (text2date(str, &this->u.time, /*utc=*/conv->unsignedp)) {
      if (conv->byteswap) {
	if (sizeof(this->u.time) == 8) {
	  this->u.time = bswap64(this->u.time);
	} else {
	  this->u.time = bswap32(this->u.time);
	}
      }
      *data = this->u.bytes;
      num = sizeof(this->u.time);
    } else {
      fail("Unrecognised date");
    }
  } break;
  case TYPE_RAW:
    fail("BUG: raw type in input converter");
  }

  return num;
}

/*-----------------------------------------------------------------------
 *	Output conversion
 *-----------------------------------------------------------------------*/
typedef struct {
  Producer child;
  void *closure;
  Conversion *conv;
  char buffer[40];
} Outconv;

static void *outconv_create(Conversion *conv, Producer child, void *closure)
{
  Outconv *this = NEW(Outconv);
  this->child = child;
  this->closure = closure;
  this->conv = conv;
  return this;
}

static int outconv_get(void *closure, char **data, int size)
{
  Outconv *this = closure;
  Conversion *conv = this->conv;
  char *str;
  Uscalar u;
  int num;
  int i;

#define GET(n) if ((num = this->child(this->closure, &str, n)) < 0) return -1
#define GETD(p,n) GET(n); memcpy(p,str,n)

  switch (conv->type) {
  case TYPE_CHAR: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG: {
    long lval = 0;
    switch (conv->type) {
    case TYPE_CHAR:
      size = sizeof(char);
      GETD(u.bytes, size);
      if (! conv->unsignedp &&
	  (conv->style==STYLE_DECIMAL || conv->style == STYLE_DEFAULT)) {
	lval = (signed char)u.cval;
      } else {
	lval = (unsigned char)u.cval;
      }
      break;
    case TYPE_SHORT:
      size = sizeof(short);
      GETD(u.bytes, size);
      if (conv->byteswap) {
	u.sval = bswap16(u.sval);
      }
      if (! conv->unsignedp &&
	  (conv->style==STYLE_DECIMAL || conv->style == STYLE_DEFAULT)) {
	lval = (signed short)u.sval;
      } else {
	lval = (unsigned short)u.sval;
      }
      break;
    case TYPE_INT:
      size = sizeof(int);
      GETD(u.bytes, size);
      if (conv->byteswap) {
	u.ival = bswap32(u.ival);
      }
      if (! conv->unsignedp &&
	  (conv->style==STYLE_DECIMAL || conv->style == STYLE_DEFAULT)) {
	lval = (signed int)u.ival;
      } else {
	lval = (unsigned int)u.ival;
      }
      break;
    case TYPE_LONG:
      size = sizeof(long);
      GETD(u.bytes, size);
      if (conv->byteswap) {
	if (sizeof(long) == 8) {	/* x86_64 */
	  u.lval = bswap64(u.lval);
	} else {
	  u.lval = bswap32(u.lval);
	}
      }
      lval = u.lval;
      break;
    default:;
    }
    switch (conv->style) {
    case STYLE_BINARY:
      size *= 8;
      for (i=0; i<size; i++) {
	this->buffer[i] = (lval & (1 << (size-1 - i))) ? '1' : '0';
      }
      this->buffer[size] = 0;
      break;
    case STYLE_OCTAL:
      sprintf(this->buffer, "%lo", lval);
      break;
    case STYLE_DEFAULT: case STYLE_DECIMAL:
      if (! conv->unsignedp) {
	sprintf(this->buffer, "%ld", lval);
      } else {
	sprintf(this->buffer, "%lu", lval);
      }
      break;
    case STYLE_HEX:
      sprintf(this->buffer, "%lx", lval);
      break;
    }
    *data = this->buffer;
  } break;
  case TYPE_FLOAT:
    GETD(u.bytes, sizeof(float));
    if (conv->byteswap) {
      u.u32 = bswap32(u.u32);
    }
    sprintf(this->buffer, "%g", u.fval);
    *data = this->buffer;
    break;
  case TYPE_DOUBLE:
    GETD(u.bytes, sizeof(double));
    if (conv->byteswap) {
      u.u64 = bswap64(u.u64);
    }
    sprintf(this->buffer, "%g", u.dval);
    *data = this->buffer;
    break;
  case TYPE_STRING:
    GET(1024);
    *data = str;
    return num;
    break;
#if HAVE_POINT
  case TYPE_POINT:
    GETD(u.bytes, sizeof(u.pknam));
    if (conv->byteswap) {
      int i;
      for (i = 0; i < sizeof(u.pknam) / sizeof(u.pknam[0]); i++) {
	u.pknam[i] = bswap16(u.pknam[i]);
      }
    }
    cdnmupk(u.pknam, this->buffer, 1);
    *data = this->buffer;
    break;
#endif
#if HAVE_BCN
  case TYPE_BCN: {
    int b, c, n;
    GETD(u.bytes, sizeof(short));
    if (conv->byteswap) {
      u.sval = bswap16(u.sval);
    }
    cmgbcn(u.sval, &b, &c, &n);
    sprintf(this->buffer, "%d,%d,%d", b, c, n);
    *data = this->buffer;
  } break;
#endif
#if HAVE_NORDFLOAT
  case TYPE_NORDFLOAT: {
    union {
      double dval;
      short spart[4];
    } ieee;
    GETD(u.nf, 6);
    if (conv->byteswap) {
      u.nf[0] = bswap16(u.nf[0]);
      u.nf[1] = bswap16(u.nf[1]);
      u.nf[2] = bswap16(u.nf[2]);
    }
#ifdef __linux__
    {
      /* Assume the "native" byte ordering for Nordfloat is three shorts
       * in the same order as SunOS but each in native byte-order.
       */
      short bigout[4];
      cf48to64_(u.nf, bigout);
      ieee.spart[0] = bigout[3];
      ieee.spart[1] = bigout[2];
      ieee.spart[2] = bigout[1];
      ieee.spart[3] = bigout[0];
    }
#else
    cf48to64_(u.nf, ieee.spart);
#endif
    sprintf(this->buffer, "%g", ieee.dval);
    *data = this->buffer;
  } break;
#endif
  case TYPE_DATE: {
    struct tm tm;
    char buf[40];
    GETD(u.bytes, sizeof(time_t));
    if (conv->byteswap) {
      if (sizeof(u.time) == 8) {
	u.time = bswap64(u.time);
      } else {
	u.time = bswap32(u.time);
      }
    }
    /*--- N.B. ctime/localtime use rules to determine when BST is in
     * effect, but it seems (on gen-off-8 on 1999-12-14 at least) that
     * the current ruleset has summer time continuous from the beginning
     * of time until Autumn 1971,
     * e.g. "cconv -y 0" yields "Thu Jan  1 01:00:00 1970"
     */
    if (conv->unsignedp) {		/* "unsigned" overloaded as "UTC" */
      gmtime_r(&u.time, &tm);
    } else {
      localtime_r(&u.time, &tm);
    }
    strcpy(this->buffer, asctime_r(&tm, buf));
    this->buffer[24] = '\0';		/* Zap newline */
    *data = this->buffer;
  } break;
  case TYPE_RAW:
    fail("BUG: raw type in output converter");
  }

  return strlen(*data);
}

/*-----------------------------------------------------------------------
 *	Read options
 *-----------------------------------------------------------------------*/
int main(int argc, char **argv)
{
  Conversion *inconv, *outconv;
  char *opt;
  int error = 0;
  Producer prod;
  void *stream;
  const char *infile = NULL;
  char *str;
  int num;

  progname = *argv++;
  argc--;

  inconv = conversion_create();
  outconv = conversion_create();

  /*
   * Decode any command line options
   * Heuristic: "-<letter>" is option, "-?" is request for help,
   * "-<digit>..." is an argument to be converted.
   */
  for (; argc > 0 && (*argv)[0]=='-' &&
	 (isalpha((unsigned char)(*argv)[1]) || (*argv)[1]=='?');
       argv++,argc--) {
    for (opt=argv[0]+1; *opt; opt++) {
      switch (*opt) {
      case 'I': inconv->type = TYPE_INT; break;
      case 'i': outconv->type = TYPE_INT; break;
      case 'L': inconv->type = TYPE_LONG; break;
      case 'l': outconv->type = TYPE_LONG; break;
      case 'H': inconv->type = TYPE_SHORT; break;
      case 'h': outconv->type = TYPE_SHORT; break;
      case 'C': inconv->type = TYPE_CHAR; break;
      case 'c': outconv->type = TYPE_CHAR; break;
      case 'F': inconv->type = TYPE_FLOAT; break;
      case 'f': outconv->type = TYPE_FLOAT; break;
      case 'G': inconv->type = TYPE_DOUBLE; break;
      case 'g': outconv->type = TYPE_DOUBLE; break;
      case 'S': inconv->type = TYPE_STRING; break;
      case 's': outconv->type = TYPE_STRING; break;
#if HAVE_BCN
      case 'B': inconv->type = TYPE_BCN; break;
      case 'b': outconv->type = TYPE_BCN; break;
#else
      case 'B': case 'b':
	fprintf(stderr, "-b/-B not supported on this platform\n");
	error++;
	break;
#endif
#if HAVE_POINT
      case 'P': inconv->type = TYPE_POINT; break;
      case 'p': outconv->type = TYPE_POINT; break;
#else
      case 'P': case 'p':
	fprintf(stderr, "-P/-p not supported on this platform\n");
	error++;
	break;
#endif
      case 'R': inconv->type = TYPE_RAW; break;
      case 'r': outconv->type = TYPE_RAW; break;
#if HAVE_NORDFLOAT
      case 'J': inconv->type = TYPE_NORDFLOAT; break;
      case 'j': outconv->type = TYPE_NORDFLOAT; break;
#else
      case 'J': case 'j':
	fprintf(stderr, "-P/-p not supported on this platform\n");
	error++;
	break;
#endif
      case 'Y': inconv->type = TYPE_DATE; break;
      case 'y': outconv->type = TYPE_DATE; break;

      case 'Z': inconv->style = STYLE_BINARY; break;
      case 'z': outconv->style = STYLE_BINARY; break;
      case 'O': inconv->style = STYLE_OCTAL; break;
      case 'o': outconv->style = STYLE_OCTAL; break;
      case 'D': inconv->style = STYLE_DECIMAL; break;
      case 'd': outconv->style = STYLE_DECIMAL; break;
      case 'X': inconv->style = STYLE_HEX; break;
      case 'x': outconv->style = STYLE_HEX; break;

      case 'Q': inconv->quoting = QUOTING_SHELL; break;
      case 'q': outconv->quoting = QUOTING_SHELL; break;
      case 'T': inconv->quoting = QUOTING_TCL; break;
      case 't': outconv->quoting = QUOTING_TCL; break;

      case 'U': inconv->unsignedp = TRUE; break;
      case 'u': outconv->unsignedp = TRUE; break;
      case 'E': inconv->byteswap = TRUE; break;
      case 'e': outconv->byteswap = TRUE; break;

      case 'N':
	/*--- Input filename expected - either in this arg, or next */
	if (infile) {
	  fprintf(stderr, "Only one -N option allowed\n");
	  error++;
	} else if (*(opt+1) != '\0') {
	  opt++;
	  infile = opt;
	  opt += strlen(infile) - 1;	/* To terminate loop */
	} else {
	  if (argc == 1) {
	    fprintf(stderr, "Missing filename after -N\n");
	    error++;
	  } else {
	    argv++, argc--;
	    infile = *argv;
	  }
	}
	break;
      default: error++;
      }
    }
  }
  if (error) {
    fprintf(stderr, usage, progname);
    exit(200);
  }
  if (argc >= 1 && (*argv)[0]=='-' && (*argv)[1]=='-' && (*argv)[2]=='\0') {
    /*--- "--" signified end of options */
    argv++; argc--;
  }

  if (infile) {
    /*--- Expect filename from which to read data, or "-" for stdin */
    if (argc != 0) {
      fprintf(stderr, usage, progname);
      exit(200);
    }
    stream = file_create(infile);
    if (inconv->type == TYPE_RAW) {
      prod = file_get_raw;
    } else {
      prod = file_get;
    }

  } else {
    /*--- Get strings from command line args */
    if (argc == 0) {			/* Need at least one value */
      fprintf(stderr, usage, progname);
      exit(200);
    }
    stream = argv_create(argc, argv);
    prod = argv_get;
  }

  if (inconv->type != TYPE_RAW) {
    /*--- Apply input conversion */
    stream = inconv_create(inconv, prod, stream);
    prod = inconv_get;
  }

  /*--- Data produced can have variable sizes; truncate to what asked for */
  stream = reducer_create(prod, stream);
  prod = reducer_get;

  if (outconv->type == TYPE_RAW) {
    while ((num = prod(stream, &str, 1024)) >= 0) {
      fwrite(str, 1, num, stdout);
    }
  } else {
    /*--- Unless outputting strings, pad to required size */
    if (outconv->type != TYPE_STRING) {
      stream = expander_create(prod, stream);
      prod = expander_get;
    }

    /*--- Convert to output strings */
    stream = outconv_create(outconv, prod, stream);
    prod = outconv_get;

    while ((num = prod(stream, &str, 1024)) >= 0) {
      printf("%s\n", str);
    }
  }
  
  return 0;
}

/*-----------------------------------------------------------------------
 *	Memory allocator
 *-----------------------------------------------------------------------*/
static void *new(int size)
{
  void *this;
  this = malloc(size);
  if (this == NULL) {
    fprintf(stderr, "%s: Out of memory\n", progname);
    exit(1);
  }
  return this;
}

static void fail(const char *fmt, ...)
{
  va_list ap;
  fprintf(stderr, "%s: ", progname);
  va_start(ap,fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  exit(1);
}

#ifdef __sunos5__
static uint16_t
bswap16(uint16_t x)
{
  union {
    uint16_t v;
    char b[2];
  } in, out;
  in.v = x;
  out.b[0] = in.b[1];
  out.b[1] = in.b[0];
  return out.v;
}

static uint32_t
bswap32(uint32_t x)
{
  union {
    uint32_t v;
    char b[4];
  } in, out;
  in.v = x;
  out.b[0] = in.b[3];
  out.b[1] = in.b[2];
  out.b[2] = in.b[1];
  out.b[3] = in.b[0];
  return out.v;
}

static uint64_t
bswap64(uint64_t x)
{
  union {
    uint64_t v;
    char b[2];
  } in, out;
  in.v = x;
  out.b[0] = in.b[7];
  out.b[1] = in.b[6];
  out.b[2] = in.b[5];
  out.b[3] = in.b[4];
  out.b[4] = in.b[3];
  out.b[5] = in.b[2];
  out.b[6] = in.b[1];
  out.b[7] = in.b[0];
  return out.v;
}

#endif

#if NOTYET
int shell_unquote(char *s, char *dest)
{
  int c;
  char *d = dest;
  for (;;) {
    c = *s++;
    if (c == 0) {
      break;
    } else if (c == '\'') {
      for (;;) {
	c = *s++;
	if (c == 0) break;
	if (c == '\'') {
	  break;
	} else {
	  *d++ = c;
	}
      }
      if (c == 0) break;
    } else if (c == '"') {
      for (;;) {
	c = *s++;
	if (c == 0) break;
	if (c == '"') {
	  break;
	} else if (c == '\\') {
	  c = *s++;
	  if (c == 0) break;
	  if (c != '"' && c != '\\') {
	    *d++ = '\\';
	  }
	  *d++ = c;
	} else {
	  *d++ = c;
	}
      }
      if (c == 0) break;
    } else if (c == '\\') {
      c = *s++;
      if (c == 0) break;
      *d++ = c;
    } else {
      *d++ = c;
    }
  }
  num = d - dest;
}

#endif
