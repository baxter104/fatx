#ifndef FATX_HPP
#define FATX_HPP
/*
 *	FATX filesystem support
 *
 *  Copyright (C) 2012, 2013, 2014 Christophe Duverger
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
/**
 *  Compile with:
 *  -D DEBUG		to produce debug informations output
 *	-D DBGCOLOR=x	to produce debug messages with x=2 ansi colors x=1 thread codes prefix x=0 nothing x=-1 no thread number
 *	-D DBG_INIT		to produce debug on initialisation sequence
 *	-D DBG_READ		to print bytes read at device level
 *	-D DBG_WRITE	to print bytes written at device level
 *	-D DBG_SEM		to print accesses to semaphores
 *	-D DBGSEM=\"x\"	to print only semaphore named x
 *	-D DBG_BUFFER	to print buffer operations
 *	-D DBGBUFDMP=x	to print x bytes of buffer at each change
 *	-D DBG_CACHE	to print cache operations
 *	-D DBG_CACHDMP	to dump cache at each change
 *	-D DBG_AREAS	to print fat areas
 *	-D DBG_GUESS	to print guesses
 *	-D DBGCR=x		to limit to x bytes per line
 *	-D DBGLIMIT=x	to limit to x bytes the printing of read/write
 *	-D DBG_FAT		to print the FAT
 *	-D DBG_GAPS		to print gaps
 *	-D NO_WRITE		to fake writing but no modification is done
 *	-D NO_FUSE		to disable fuse support
 *	-D NO_LOCK		to disable semaphores
 *	-D NO_CACHE		to disable FAT cache
 *	-D NO_FUSE_CALL	to disable calls to fuse library
 *	-D NO_SPLICE	to disable splice calls by fuse
 *	-D NO_OPTION	to disable option parsing
 *	-D ENABLE_XBOX	to enable configuration for XBOX xbe
 *
 *  Make symlink to executable with names:
 *	 "fusefatx"		for fuse filesystem support
 *	 "mkfs.fatx"	for filesystem creation
 *	 "fsck.fatx"	for filesystem check and repair
 *	 "unrm.fatx"	for recovery of deleted files
 *	 "label.fatx"	for display or change volume name
 *
 *  Use -h option for each symlink call to find syntax and options list
 */

#ifdef ENABLE_XBOX
	#define NO_LOCK
	#define NO_FUSE
	#define NO_OPTION
	#define NO_IO
	#define NO_FCNTL
	#define NO_TIME
	#ifdef DEBUG
		#undef DEBUG
	#endif
	#ifdef NO_FD
		#undef NO_FD
	#endif
#endif
#ifdef __CYGWIN__
	#define NO_FCNTL
#endif
#ifndef NO_IO
	#include <fstream>
	#include <iostream>
#endif
#ifndef NO_FD
	#include <stdio.h>
#endif
#ifndef NO_FCNTL
	#include <fcntl.h>
#endif
#ifndef NO_TIME
	#include <time.h>
#endif
#ifndef NO_LOCK
	#include <pthread.h>
#endif
#ifndef NO_FUSE
	#define FUSE_USE_VERSION 29
	#include <fuse.h>
#endif
#ifndef NO_OPTION
	#include <boost/program_options.hpp>
#endif
#ifndef NO_LOCK
	#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
	#include <boost/interprocess/sync/sharable_lock.hpp>
	#include <boost/interprocess/sync/upgradable_lock.hpp>
	#include <boost/interprocess/sync/scoped_lock.hpp>
#endif

#include <string>
#include <vector>
#include <memory>
#include <list>
#include <map>
#include <set>
#include <cassert>
#include <algorithm>
#include <bitset>

#include <string.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <boost/cstdint.hpp>
#include <boost/integer.hpp>

#include <boost/format.hpp>
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bimap.hpp>
#include <boost/bimap/list_of.hpp>
#include <boost/bimap/set_of.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

using namespace std;
using namespace boost;
#ifndef NO_OPTION
using namespace boost::program_options;
#endif
#ifndef NO_LOCK
using namespace boost::interprocess;
#endif

#define function std::function

typedef uint64_t			streamptr;		/// pointers in device
typedef uint64_t			clusptr;		/// cluster numbers
typedef uint64_t			filesize;		/// file size

class						fatx_context;	/// context that contains pointers on used instances of following classes
class						frontend;		/// arguments management & options values
class						device;			/// read/write to the device
class						fatxpar;		/// partition identification & partition usefull values
class						dskmap;			/// device file allocation table management
class						memmap;			/// memory file allocation table used to handle deleted entries
class						entry;			/// file or directory entry
class						vareas;			/// vector of areas in fat
class						buffer;			/// file buffer

typedef std::shared_ptr<vareas>			ptr_vareas;
typedef std::unique_ptr<buffer>			ptr_buffer;
typedef std::unique_ptr<entry>			ptr_entry;

static const size_t			blksize			= 512;					/// standard block size
static const clusptr		EOC				= 0xFFFFFFFF;			/// fat: end of chain
static const clusptr		FLK				= 0x00000000;			/// fat: free link
static const char			EOD				= -1;					/// entry mark for end of directory
static const unsigned char	name_size		= 0x2A;					/// maximum size of entry names
static const unsigned char	deleted_size	= 0xE5;					/// name size used in entry to mark entry as deleted
static const size_t			slab			= name_size * 2 + 2;	/// maximum size of label name file
static const int			max_fuse_args	= 20;					/// maximum number of unrecognized arguments passed to fuse
static const unsigned int	max_buf			= 1*1024*1024;			/// buffer maximum size
static const unsigned int	max_cache_div	= 1000;					/// fat size divider for cache maximum size
static const unsigned int	nb_cache_div	= 10;					/// cache size divider for nuber of read ahead operations
static const unsigned int	timeout			= 60;					/// timeout in seconds
static const int			code_noerr		= 0;					/// no error code
static const int			code_corrd		= 1<<0;					/// errors corrected code
static const int			code_ncorr		= 1<<2;					/// errors remaining code
static const int			code_operr		= 1<<3;					/// internal error code
static const int			code_usage		= 1<<4;					/// usage error code

extern const char*			sepdir;									/// using unix directories
extern const char*			fsid;									/// filesystem id
extern const char*			fidx;									/// file used for label name
extern const char*			def_landf;								/// default directory for lost & founds
extern const char*			def_fpre;								/// default file prefix for lost & founds
extern const char*			def_label;								/// default label name



bool                        drop_privileges(string user);                      /// drop privileges if running as root

/// Macro for debug output
///
#ifdef DEBUG
	#ifndef DBGCOLOR
		#define DBGCOLOR 0
	#endif
	#if DBGCOLOR == 2
		static const vector<string> colors = {
			"\033[37;41m",
			"\033[37;42m",
			"\033[37;43m",
			"\033[37;44m",
			"\033[37;45m",
			"\033[37;46m",
			"\033[37;40m",
			"\033[31;40m",
			"\033[32;40m",
			"\033[33;40m",
			"\033[34;40m",
			"\033[35;40m",
			"\033[36;40m"
		};
		static map<pthread_t, size_t> thread_colors;
		static size_t color_cpt = 0;
		#define dbglog(s) { \
			if(thread_colors.find(pthread_self()) == thread_colors.end()) {\
				thread_colors.insert(make_pair(pthread_self(), color_cpt)); \
				color_cpt = (color_cpt + 1) % colors.size(); \
			} \
			console::write((format("%s{%08X}%s %s") \
				% colors[thread_colors.find(pthread_self())->second] \
				% pthread_self() \
				% "\033[0m" \
				% (s) \
			).str(), true); \
		}
	#endif
	#if DBGCOLOR == 1
		static const vector<string> colors = {
			"|| ",
			"// ",
			"\\\\ ",
			"-- ",
			"** ",
			"++ ",
			"00 ",
			".. ",
			"## ",
			"$$ ",
			"@@ ",
			"XX "
		};
		static map<pthread_t, size_t> thread_colors;
		static size_t color_cpt = 0;
		#define dbglog(s) { \
			if(thread_colors.find(pthread_self()) == thread_colors.end()) {\
				thread_colors.insert(make_pair(pthread_self(), color_cpt)); \
				color_cpt = (color_cpt + 1) % colors.size(); \
			} \
			console::write((format("%s{%08X}%s %s") \
				% colors[thread_colors.find(pthread_self())->second] \
				% pthread_self() \
				% "" \
				% (s) \
			).str(), true); \
		}
	#endif
	#if DBGCOLOR == 0
		#define dbglog(s) { \
			console::write((format("%s{%08X}%s %s") \
				% "" \
				% pthread_self() \
				% "" \
				% (s) \
			).str(), true); \
		}
	#endif
	#if DBGCOLOR == -1
		#define dbglog(s) { \
			console::write((s), true); \
		}
	#endif
#endif
#ifndef DBGCR
	#define DBGCR			48
#endif

/// Big / Little endian (de)formatter
///
template<int bytes = 1>
class						endian {
private:
	template<int big>
	class endian_format : public std::string {
	public:
		typedef typename uint_t<bytes * 8>::least value_type;
	private:
		size_t					pos(const size_t i) const {
			return (big == 0) ? bytes - 1 - i : i;
		}
		std::string::value_type	getbyte(const value_type n, const size_t i = 0) const {
			return (std::string::value_type)((n & (0xFF << (pos(i) * 8))) >> (pos(i) * 8));
		}
		value_type				setbyte(const std::string s, const size_t i = 0) const {
			return (1 << (8 * pos(i))) * (uint8_t)s[i];
		}
		value_type				getvalue() const {
			value_type res = 0;
			for(size_t i = 0; i < bytes; i++)
				res += setbyte((*this), i);
			return res;
		}
		value_type				setvalue(const value_type n) {
			(*this).erase();
			for(size_t i = 0; i < bytes; i++)
				(*this) += getbyte(n, i);
			return getvalue();
		}
	public:
		value_type	operator () () const				{ return getvalue(); }
		value_type	operator () (const value_type n)	{ return setvalue(n); }
					endian_format(const value_type n)	{ setvalue(n); }
					endian_format(const std::string::value_type* const s) : std::string(s, bytes) {}
					endian_format(const std::string& s) : std::string(s) {}
	};
public:
	typedef typename endian_format<0>::value_type	value_type;
	typedef endian_format<1>						bigend;
	typedef endian_format<0>						litend;
};

/// Mutex management
///
class						mutex
#ifndef NO_LOCK
	:			public interprocess_upgradable_mutex
#endif
{
private:
	string							nam;
	int								cpt;
	void							genlock(char, function<void()>);
	void							genunlock(char, function<void()>);
	bool							gentimedlock(char, function<bool()>);
	void							genunlockupgradableandlock(function<void()>);
	void							genunlockandlockupgradable(function<void()>);
public:
			mutex(const string n = "???") : nam(n), cpt(0) {
	}
			~mutex() {
		nam.clear();
	}
	void	lock() {
		genlock('X',
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::lock, this)
		#else
			0
		#endif
		);
	}
	void	unlock() {
		genunlock('X',
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::unlock, this)
		#else
			0
		#endif
		);
	}
	bool	timed_lock(const posix_time::ptime& p) {
		#ifdef NO_LOCK
			(void) p;
		#endif
		return gentimedlock('X',
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::timed_lock, this, p)
		#else
			0
		#endif
		);
	}
	void	lock_upgradable() {
		genlock('U',
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::lock_upgradable, this)
		#else
			0
		#endif
		);
	}
	void	unlock_upgradable() {
		genunlock('U',
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::unlock_upgradable, this)
		#else
			0
		#endif
		);
	}
	bool	timed_lock_upgradable(const posix_time::ptime& p) {
		#ifdef NO_LOCK
			(void) p;
		#endif
		return gentimedlock('U',
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::timed_lock_upgradable, this, p)
		#else
			0
		#endif
		);
	}
	void	lock_sharable() {
		genlock('S',
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::lock_sharable, this)
		#else
			0
		#endif
		);
	}
	void	unlock_sharable() {
		genunlock('S',
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::unlock_sharable, this)
		#else
			0
		#endif
		);
	}
	bool	timed_lock_sharable(const posix_time::ptime& p) {
		#ifdef NO_LOCK
			(void) p;
		#endif
		return gentimedlock('S',
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::timed_lock_sharable, this, p)
		#else
			0
		#endif
		);
	}
	void	unlock_upgradable_and_lock() {
		genunlockupgradableandlock(
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::unlock_upgradable_and_lock, this)
		#else
			0
		#endif
		);
	}
	void	unlock_and_lock_upgradable() {
		genunlockandlockupgradable(
		#ifndef NO_LOCK
			bind(&interprocess_upgradable_mutex::unlock_and_lock_upgradable, this)
		#else
			0
		#endif
		);
	}
	void	name(const string n) {
		nam = n;
	}
};

/// LRU read cache
///
template<typename key_t, typename value_t>
class						read_cache {
public:
	typedef key_t												key_type;
	typedef value_t												value_type;
	typedef bimaps::bimap<
		bimaps::set_of<key_type>,
		bimaps::list_of<value_type>
	>															container_type;
	typedef pair<value_type, key_type>							pair_type;
	typedef vector<pair_type>									lkval_t;
	typedef function<lkval_t(const key_type&, const size_t&)>	fread_t;
	typedef function<bool(const key_type&, const value_type&)>	fwrite_t;
protected:
	const fread_t		read;
	const fwrite_t		write;
	const size_t		capacity;
	const size_t		readahead;
	container_type		container;
	mutex				access;
public:
						read_cache(const fread_t&, const fwrite_t&, size_t, size_t);
						~read_cache();
	void				clear();
	value_type			operator () (const key_type&);
	bool				operator () (const key_type&, const value_type&);
	void				operator () ();
};

/// Entries attributes
///
class						attrib {
public:
	bool						ro;
	bool						hid;
	bool						sys;
	bool						lab;
	bool						dir;
	bool						arc;
	bool						dev;
	bool						na;
	attrib() :
		ro	(false),
		hid	(false),
		sys	(false),
		lab	(false),
		dir	(false),
		arc	(false),
		dev	(false),
		na	(false) {
	}
	attrib(const char& c) :
		ro	(c & (1 << 0)),
		hid	(c & (1 << 1)),
		sys	(c & (1 << 2)),
		lab	(c & (1 << 3)),
		dir	(c & (1 << 4)),
		arc	(c & (1 << 5)),
		dev	(c & (1 << 6)),
		na	(c & (1 << 7)) {
	}
	#ifdef DEBUG
	string						print() const {
		return (format("%c%c%c%c%c%c%c-")
			% (ro	? 'R' : '-')
			% (hid	? 'H' : '-')
			% (sys	? 'S' : '-')
			% (lab	? 'L' : '-')
			% (dir	? 'D' : '-')
			% (arc	? 'A' : '-')
			% (dev	? 'V' : '-')
		).str();
	}
	#endif
	char*						write(char buf[1]) {
		buf[0] =
			ro	? (1 << 0) : 0 |
			hid	? (1 << 1) : 0 |
			sys	? (1 << 2) : 0 |
			lab	? (1 << 3) : 0 |
			dir	? (1 << 4) : 0 |
			arc	? (1 << 5) : 0 |
			dev	? (1 << 6) : 0 |
			na	? (1 << 7) : 0
		;
		return buf;
	}
	#ifndef NO_FUSE
	mode_t						operator () () {
		return
			S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH |
			(!ro ? (S_IWUSR | S_IWGRP | S_IWOTH) : 0) |
			(dir ? S_IFDIR : S_IFREG)
		;
	}
	void						operator () (const mode_t& m) {
		ro = ((m & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0);
	}
	#endif
};

/// FATX date stamps
///
class 						date {
public:
	unsigned int				year;
	unsigned int				month;
	unsigned int				day;
	unsigned int				hour;
	unsigned int				min;
	unsigned int				sec;
	typedef uint64_t			date_t;
	date() :
		year	(1980),
		month	(1),
		day		(1),
		hour	(0),
		min		(0),
		sec		(0) {
	}
	date(const unsigned char buf[4]) :
		year	((buf[0] >> 1) + 1980),
		month	((((buf[0] & 1) << 3) | ((buf[1] & 0xE0) >> 5)) + 1),
		day		((buf[1] & 0x1F) + 1),
		hour	(buf[2] >> 3),
		min		(((buf[2] & 0x07) << 3) | ((buf[3] & 0xE0) >> 5)),
		sec		(buf[3] & 0x1F) {
	}
	#ifdef DEBUG
	string						print() const {
		return (format("%02d:%02d:%02d %02d/%02d/%04d")
			% hour		% min		% sec
			% day		% month		% year
		).str();
	}
	#endif
	unsigned char*				write(unsigned char buf[4]) {
		buf[0] = (((year - 1980) & 0x7F) << 1)	| (((month - 1) & 0x08) >> 3);
		buf[1] = (((month - 1) & 0x07) << 5)	| ((day - 1) & 0x1F);
		buf[2] = ((hour & 0x1F) << 3) | ((min & 0x38) >> 3);
		buf[3] = ((min & 0x07) << 5) | (sec & 0x1F);
		return buf;
	}
	date_t						seq() const {
		return
			((date_t)((year - 1980)	& 0xFFFF)	<< 48) |
			((date_t)(month			& 0xFF)		<< 40) |
			((date_t)(day			& 0xFF)		<< 32) |
			((date_t)(hour			& 0xFF)		<< 16) |
			((date_t)(min			& 0xFF)		<< 8)  |
			((date_t)(sec			& 0xFF)		<< 0)
		;
	}
	time_t						operator () () {
		struct tm st;
		st.tm_year	= year - 1900;
		st.tm_mon	= month - 1;
		st.tm_mday	= day;
		st.tm_hour	= hour;
		st.tm_min	= min;
		st.tm_sec	= sec;
		return mktime(&st);
	}
	void						operator () (const time_t& t) {
		struct tm* st(localtime(&t));
		if(st != 0) {
			year	= st->tm_year + 1900;
			month	= st->tm_mon + 1;
			day		= st->tm_mday;
			hour	= st->tm_hour;
			min		= st->tm_min;
			sec		= st->tm_sec;
		}
	}
};

/// Data areas
///
class						area {
public:
	streamptr	offset;
	streamptr	pointer;
	streamptr	size;
	clusptr		start;
	clusptr		stop;
				area(streamptr o = 0, streamptr p = 0, streamptr s = 0, clusptr rt = 0, clusptr op = 0) : offset(o), pointer(p), size(s), start(rt), stop(op) {
	}
};
class						vareas : public vector<area> {
public:
	clusptr					first() const;
	clusptr					last() const;
	size_t					nbcls() const;
	clusptr					at(size_t) const;
	iterator				in(size_t);
	bool					isin(clusptr) const;
	vareas					sub(filesize, filesize = 0) const;
	void					add(vareas);
	string					print() const;
};
// Data buffers
//
class 						buffer : public string {
public:
	bool			touched;
	streamptr		offset;
					buffer(const streamptr = 0, const streamptr = 0);
					~buffer();
	void			enlarge(const streamptr);
	void			operator () (size_t);
};

class						console {
public:
	static void				write(const string, bool = false);
	static pair<bool, bool>	read();
};
class						frontend {
private:
	bool						readonly;
public:
	enum						call_t {
		unknown	= 0,
		fuse,
		mkfs,
		fsck,
		unrm,
		label
	};

	call_t						prog;
	bool						force_y;
	bool						force_n;
	bool						force_a;
	bool						verbose;
	bool						recover;
	bool						local;
	bool						deldate;
	bool						dellost;
	bool						fuse_debug;
	bool						fuse_foregrd;
	bool						fuse_singlethr;
	bool						nofat;
	int							argc;
	const char* const *	const	argv;
	string						progname;
	bool						dialog;
	string						lostfound;
	string						foundfile;
	unsigned int				filecount;
	string						mount;
	string						volname;
	string						fuse_option;
	vector<string>				unkopt;
	string						partition;
	string						table;
	streamptr					clus_size;
	uid_t						uid;
	gid_t						gid;
	mode_t						mask;
	bool						allyes;
	streamptr					offset;
	streamptr					size;
	string						input;
	string						script;
    string                      runas;

								frontend(int, const char* const * const);
								~frontend();
	string						name();
	bool						setup();
	void						parser();
	bool						getanswer(bool = false);
	bool						writeable() const {
		return !readonly;
	}
};
class						device {
private:
#ifndef NO_IO
	fstream*					io;
#endif
#ifndef NO_FD
	FILE*						fd;
#endif
	streamptr					tot_size;
	bool						changes;
	mutex						authd;
public:

								device();
								~device();
	bool						setup();
	streamptr					size() const {
		return tot_size;
	}
	bool						modified() const {
		return changes;
	}
	#ifndef NO_FD
	FILE*						getfd() const {
		return fd;
	}
	#endif
	string						read(const streamptr&, const size_t	= blksize);
	bool						write(const streamptr&, const string&);
	string						address(const streamptr&) const;
	void						devlog(bool, const streamptr&, const string&) const;
	string						print(const streamptr&, const size_t& = blksize, const size_t& = 32);
};
class						fatxpar {
private:
	class						bootsect {
	public:
		uint32_t					id;
		uint32_t					spc;
		uint32_t					root;
									bootsect(const char buf[blksize]) :
			id	(endian<4>::litend(&buf[4])()),
			spc	(endian<4>::litend(&buf[8])()),
			root(endian<4>::litend(&buf[12])()) {
		}
									bootsect(const uint32_t i, const uint32_t s, const uint32_t r) :
			id(i), spc(s), root(r) {
		}
		void						write(char buf[blksize]) {
			memcpy(&buf[0], &fsid[0], 4);
			memcpy(&buf[4], &endian<4>::litend(id)[0], 4);
			memcpy(&buf[8], &endian<4>::litend(spc)[0], 4);
			memcpy(&buf[12], &endian<4>::litend(root)[0], 4);
		}
	};
	class						devheader {
	public:
		uint32_t					id;
		uint32_t					unkn;
		uint32_t					p2_start;
		uint32_t					p2_size;
		uint32_t					p1_start;
		uint32_t					p1_size;
									devheader(char buf[blksize]) :
			id			(endian<4>::litend(&buf[0])()),
			unkn		(endian<4>::litend(&buf[4])()),
			p2_start	(endian<4>::litend(&buf[8])()),
			p2_size		(endian<4>::litend(&buf[12])()),
			p1_start	(endian<4>::litend(&buf[16])()),
			p1_size		(endian<4>::litend(&buf[20])()) {
		}
									devheader(const uint64_t s) :
			id(0x00020000), p2_start(0x00633000), p2_size((s - 0xC6600000ULL) >> 9), p1_start(0x005B3000), p1_size(0x00080000) {
		}
		void						write(char buf[blksize]) {
			memcpy(&buf[0], &endian<4>::litend(id)[0], 4);
			memcpy(&buf[8], &endian<4>::litend(p2_start)[0], 4);
			memcpy(&buf[12], &endian<4>::litend(p2_size)[0], 4);
			memcpy(&buf[16], &endian<4>::litend(p1_start)[0], 4);
			memcpy(&buf[20], &endian<4>::litend(p1_size)[0], 4);
		}
	};
public:
	uint32_t					par_id;
	string						par_label;
	streamptr					par_start;
	streamptr					par_size;
	uint32_t					clus_size;
	uint16_t					clus_pow;
	uint32_t					clus_num;
	uint32_t					clus_fat;
	uint16_t					chain_size;
	uint16_t					chain_pow;
	streamptr					fat_start;
	streamptr					fat_size;
	streamptr					root_start;
	clusptr						root_clus;

								fatxpar();
	bool						setup();
	bool						write();
	size_t						label(unsigned char [slab]) const;
	void						label(const unsigned char [slab], const size_t size);
};
namespace					clsarithm {
	inline clusptr				siz2cls(const filesize&);
	inline clusptr				inccls(const clusptr&);
	inline streamptr			cls2ptr(const clusptr&);
	inline clusptr				ptr2cls(const streamptr&);
	inline streamptr			cls2fat(const clusptr&);
	inline string				clsprint(const clusptr&, const clusptr&);
}
class						dskmap {
protected:
	typedef clusptr	mapptr_t;
	typedef clusptr	mapsiz_t;
	typedef bimaps::bimap<
		bimaps::set_of<mapptr_t>,
		bimaps::multiset_of<mapsiz_t>
	>				gap_t;
	typedef read_cache<
		clusptr,
		clusptr
	>				memnext_t;
	typedef function<
		void(const clusptr&, const clusptr&)
	>				lbdarea_t;
	typedef function<
		void(const clusptr&, const clusptr&)
	>				lbdfat_t;

	memnext_t		memnext;
	gap_t			freegaps;
	mutex			authm;
	set<clusptr>	bad;

	void						forfat(lbdfat_t);
	memnext_t::lkval_t			real_read(const clusptr&, size_t);
	bool						real_write(const clusptr&, const clusptr&);
public:
	enum						status_t {
		disk,
		deleted,
		modified,
		marked
	};
								dskmap(const fatxpar&);
	virtual						~dskmap();
	clusptr						clsavail();
	void						erase();
	void						gapcheck();
	vareas						getareas(const clusptr&, lbdarea_t = 0);
	virtual clusptr				read(const clusptr&);
	bool						write(const clusptr&, const clusptr&);
	vareas						alloc(const clusptr&, const clusptr& = 0);
	void						free(const clusptr&);
	bool						resize(ptr_vareas, const clusptr&);
	string						printchain(clusptr);
	void						printgaps() const;
	virtual void				change(const clusptr&, entry*, const clusptr& = FLK, const status_t = marked);
	virtual ptr_vareas			markchain(clusptr, entry*);
	virtual void				restore(clusptr, bool = true);
	virtual status_t			status(const clusptr&) const;
	virtual entry*				getentry(const clusptr&) const;
	virtual void				fatlost();
	virtual void				fatcheck();
	#ifdef DEBUG
	virtual void				printfat();
	#endif
};
class						memmap : public dskmap {
private:
public:
	class						link {
	public:
		clusptr						next;
		entry*						ent;
		status_t					status;

		link(entry* e, const clusptr& n = FLK, const status_t s = marked) : next(n), ent(e), status(s) {
		}
	};
	typedef map<
		clusptr,
		class link
	>							memchain_t;
	typedef vector<vareas>		lost_t;
	memchain_t					memchain;
	lost_t						lost;
	
								memmap(const fatxpar&);
								~memmap();
	clusptr						read(const clusptr&);
	void						change(const clusptr&, entry*, const clusptr& = FLK, const status_t = marked);
	ptr_vareas					markchain(clusptr, entry*);
	status_t					status(const clusptr&) const;
	entry*						getentry(const clusptr&) const;
	void						fatlost();
	void						fatcheck();
	#ifdef DEBUG
	void						printfat();
	#endif
};
class						entry : boost::noncopyable {
private:
	int							cptacc;
	enum {none, yes, no}		writeopened;
	mutex						authb;
	mutex						authw;
	void						opendir();
	void						closedir();
	bool						write();
public:
	enum						status_t {
		valid,
		delwdata,
		delnodata,
		lost,
		end,
		invalid
	};
	enum						pass_t {
		findfile,
		finddel,
		tryrecov
	};
	static const size_t			ent_size = 64;
	static const size_t			ent_pow	= 6;

	status_t					status;
	uint8_t						namesize;
	attrib						flags;
	char						name[name_size + 1];
	clusptr						cluster;
	filesize					size;
	date						creation;
	date						access;
	date						update;
	streamptr					loc;
	ptr_vector<entry>			childs;
	entry*						parent;
	ptr_buffer					entbuf;
	ptr_vareas					areas;

								entry();
								entry(const streamptr&, const char [ent_size] = 0);
								entry(const string &, const filesize& = 0, const bool = false);
								~entry();
	string						print() const;
	string						path() const;
	bool						addtodir(entry*);
	void						remfrdir(entry*, bool = true);
	entry*						find(const char*);
	void						touch(bool = true, bool = true, bool = true);
	bool						save();
	bool						rename(const char*);
	void						recover();
	void						mark();
	void						guess();
	bool						analyse(const pass_t&, const string = string(""));
	bool						resize(const filesize);
	bool						data(char*, bool, filesize, filesize);
	size_t						bufread(char*, filesize, filesize);
	size_t						bufwrite(const char*, filesize, filesize);
	bool						flush(bool = true);
	void						open(bool w);
	void						close(bool w);
	bool						writeable() const {
		return writeopened != no;
	}
	bool						operator == (const entry&) const;
#ifndef NO_FUSE
	struct fuse_bufvec*			getbufvec(filesize, filesize);
#endif
};
class 						fatx_context {
private:
	static fatx_context*	fatxc;
public:
	frontend&				mmi;
	device					dev;
	fatxpar					par;
	dskmap*					fat;
	entry*					root;

							fatx_context(frontend&);
							~fatx_context();
	bool					setup();
	void					destroy();
	static fatx_context*	get() {
		return fatxc;
	}
	static void				set(fatx_context* const fc) {
		fatxc = fc;
	}
};

#ifdef ENABLE_XBOX
extern int					fatx(int = 0, char* [] = 0);
#endif

#endif
