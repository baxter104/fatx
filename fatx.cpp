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

#include <sys/types.h>
#include <pwd.h>

#include "fatx.hpp"

const char*		sepdir		= "/";				/// using unix directories
const char*		fsid		= "XTAF";			/// filesystem id
const char*		fidx		= "name.txt";		/// file used for label name
const char*		def_landf	= "lost+found";		/// default directory for lost & founds
const char*		def_fpre	= "FILE";			/// default file prefix for lost & founds
const char*		def_label	= "XBOX";			/// default label name

fatx_context*	fatx_context::	fatxc = nullptr;

string get_current_username() {
    struct passwd *pw = getpwuid(geteuid());

    return string(pw->pw_name);
}

// Set real and effective user and group to 'username'. Return false on error.
bool drop_privileges(string username) {
    struct passwd *pw = getpwnam(username.c_str());

    // set the real and effective UIDs and GIDs
    if(setregid(pw->pw_gid, pw->pw_gid))
    {
        perror("setreuid");
        return false;
    }
    if(setreuid(pw->pw_uid, pw->pw_uid))
    {
        printf("dropping guid to %d\n", pw->pw_gid);
        string err_str = "Error dropping privileges";
#if defined DEBUG && defined DBG_CACHE
        err_str += ": ";
        err_str += strerror(errno);
        dbglog(err_str);
#else // defined DEBUG && defined DBG_CACHE
        perror(err_str.c_str());
#endif // defined DEBUG && defined DBG_CACHE
        return false;
    }

    cout << "Successfully dropped privileges to " << username << endl;

    return true;
}

template<typename key_t, typename value_t>
												read_cache<key_t, value_t>::	read_cache(const fread_t& r, const fwrite_t& w, size_t c, size_t a) :
	read(r), write(w), capacity(c), readahead(a), access("cache") {
	assert(capacity != 0);
}
template<typename key_t, typename value_t>
												read_cache<key_t, value_t>::	~read_cache() {
	container.clear();
}
template<typename key_t, typename value_t>
void											read_cache<key_t, value_t>::	clear() {
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(access);
	#endif
	container.clear();
}
template<typename key_t, typename value_t>
typename read_cache<key_t, value_t>::value_type	read_cache<key_t, value_t>::	operator () (const key_type& k) {
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(access);
	#endif
	#ifndef NO_CACHE
		const typename container_type::left_iterator it = container.left.find(k);
		if(it != container.left.end()) {
			container.right.relocate(container.right.end(), container.project_right(it));
			return it->second;
		}
		else {
			assert(container.size() <= capacity);
			lkval_t vv = read(k, readahead);
			if(vv.empty()) {
				#if defined DEBUG && defined DBG_CACHE
					dbglog((format("... fatbuf: nothing for 0x%08X\n") % k).str())
				#endif
				return 0;
			}
			if(container.size() + vv.size() > capacity) {
				typename container_type::right_iterator b = container.right.begin();
				advance(b, container.size() + vv.size() - capacity);
				#if defined DEBUG && defined DBG_CACHE
					dbglog((format("Xx. fatbuf: reduce (%d)\n") % container.size()).str())
				#endif
				container.right.erase(container.right.begin(), b);
				#if defined DEBUG && defined DBG_CACHE
					#ifdef DBG_CACHDMP
						(*this)();
					#endif
				#endif
			}
			container.insert(typename container_type::value_type(vv.front().second, vv.front().first));
			container.right.insert(container.right.begin(), vv.begin() + 1, vv.end());
			#if defined DEBUG && defined DBG_CACHE
				dbglog((format(".xX fatbuf: 0x%08X - 0x%08X (%d/%d)\n") % k % (k + vv.size() - 1) % vv.size() % container.size()).str())
				#ifdef DBG_CACHDMP
					(*this)();
				#endif
			#endif
			return vv.front().first;
		}
	#else
		return read(k, 1).front().first;
	#endif
}
template<typename key_t, typename value_t>
bool											read_cache<key_t, value_t>::	operator () (const key_type& k, const value_type& v) {
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(access);
	#endif
	#ifndef NO_CACHE
		const typename container_type::left_iterator it = container.left.find(k);
		if(it != container.left.end()) {
			it->second = v;
			container.right.relocate(container.right.end(), container.project_right(it));
		}
		else {
			assert(container.size() <= capacity);
			if(container.size() == capacity)
				container.right.erase(container.right.begin());
			container.insert(typename container_type::value_type(k,v));
		}
		#if defined DEBUG && defined DBG_CACHE
			dbglog((format("XXX fatbuf: 0x%08X (%d)\n") % k % container.size()).str())
		#endif
	#endif
	return write(k, v);
}
#if defined DEBUG && defined DBG_CACHDMP
template<typename key_t, typename value_t>
void											read_cache<key_t, value_t>::	operator () () {
	string res;
	size_t j = 0;
	for(const auto& i: container.right) {
		res += (format(" %08X") % i.second).str();
		if(++j % (DBGCR / 4) == 0) {
			dbglog(res + "\n")
			res.clear();
		}
	}
	dbglog(res + "\n")
}
#endif

clusptr						vareas::		first() const {
	return empty() ? 0 : begin()->start;
}
clusptr						vareas::		last() const {
	return empty() ? 0 : (end() - 1)->stop;
}
size_t						vareas::		nbcls() const {
	size_t res = 0;
	for(const area& i: *this)
		res += i.stop - i.start + 1;
	return res;
}
clusptr						vareas::		at(size_t s) const {
	if(s == 0)
		return last();
	for(const area& i: *this) {
		if(s <= i.stop - i.start + 1)
			return i.start + s - 1;
		else
			s -= i.stop - i.start + 1;
	}
	return 0;
}
vareas::iterator			vareas::		in(size_t s) {
	if(s == 0)
		return end() - 1;
	for(vareas::iterator i = begin(); i != end(); i++) {
		if(s <= i->stop - i->start + 1)
			return i;
		else
			s -= i->stop - i->start + 1;
	}
	return end();
}
bool						vareas::		isin(clusptr c) const {
	return find_if(begin(), end(), [c] (const area& i) -> bool { return i.start <= c && c <= i.stop; }) != end();
}
vareas						vareas::		sub(filesize s, filesize o) const {
	vareas res(*this);
	res.erase(remove_if(
		res.begin(), res.end(),
		[s, o] (const area& a) -> bool {
			return a.offset > o + s - 1 || a.offset + a.size - 1 < o;
		}
	), res.end());
	#if defined DEBUG && defined DBG_AREAS
		if(res.empty())
			dbglog("NO SUBAREA.\n")
	#endif
	for(area& i: res) {
		filesize ns = i.size;
		filesize no = i.offset;
		if(o > i.offset && o < i.offset + i.size - 1) {
			no			= o;
			i.pointer	+= o - i.offset;
			ns			-= o - i.offset;
			i.start		+= (o - i.offset) >> fatx_context::get()->par.clus_pow;
		}
		if(o + s - 1 > i.offset && o + s - 1 < i.offset + i.size - 1) {
			ns			-= i.offset + i.size - o - s;
			i.stop		-= (i.offset + i.size - o - s) >> fatx_context::get()->par.clus_pow;
		}
		i.offset = no;
		i.size = ns;
	}
	#if defined DEBUG && defined DBG_AREAS
		if(o > nbcls() << fatx_context::get()->par.clus_pow)
			dbglog((format("AREAS: bad parameters, offset:0x%016X size:%d\n") % o % s).str())
		else
			dbglog("Sub" + res.print())
	#endif
	return o > nbcls() << fatx_context::get()->par.clus_pow ? vareas() : res;
}
void						vareas::		add(vareas va) {
	if(va.empty())
		return;
	if(!empty()) {
		if(va.first() == last() + 1) {
			(end() - 1)->size += va.begin()->size;
			(end() - 1)->stop = va.begin()->stop;
			va.erase(va.begin());
		}
		streamptr o = (end() - 1)->offset + (end() - 1)->size;
		for(area& i: va) {
			i.offset = o;
			o += i.size;
		}
	}
	insert(end(), va.begin(), va.end());
}
#if defined DEBUG && defined DBG_AREAS
string						vareas::		print() const {
	string sink;
	sink += (format("Area: %d subarea%s with %d cluster%s [0x%08X-0x%08X]\n")
		% size()
		% (size() > 1 ? "s" : "")
		% nbcls()
		% (nbcls() > 1 ? "s" : "")
		% first()
		% last()
	).str();
	for(const area& i: *this)
		sink += (format(" [0x%08X-0x%08X] off:0x%016X ptr:0x%016X len:%d\n") % i.start % i.stop % i.offset % i.pointer % i.size).str();
	return sink;
}
#endif

							buffer::		buffer(const streamptr o, const streamptr s) : touched(false), offset(0) {
	if(s == 0) {
		#if defined DEBUG && defined DBG_BUFFER
			dbglog((format("... buffer: empty allocation try\n")).str())
		#endif
		return;
	}
	size_t siz = min<streamptr>((const streamptr)max_buf, s);
	while(true) {
		resize(siz);
		if(size() >= siz)
			break;
		siz /= 2;
		if(siz < fatx_context::get()->par.clus_size) {
			clear();
			#if defined DEBUG && defined DBG_BUFFER
				dbglog((format("... buffer: no way to alloc 0x%08X 0x%016X %d\n") % &(*this)[0] % offset % siz).str())
			#endif
			return;
		}
	}
	offset = o;
	#if defined DEBUG && defined DBG_BUFFER
		dbglog((format(".oO buffer: 0x%08X 0x%016X %d\n") % this % offset % (*this).size()).str())
	#endif
}
							buffer::		~buffer() {
	#if defined DEBUG && defined DBG_BUFFER
		dbglog((format("Oo. buffer: 0x%08X 0x%016X %d\n") % this % offset % (*this).size()).str())
	#endif
}
void						buffer::		enlarge(const streamptr s) {
	if(s > max_buf)
		return;
	resize(s);
	if(size() < s)
		return;
	#if defined DEBUG && defined DBG_BUFFER
		dbglog((format("OOO buffer: 0x%08X 0x%016X %d\n") % this % offset % (*this).size()).str())
	#endif
}
#if defined DEBUG && defined DBGBUFDMP
void						buffer::		operator () (size_t p) {
	string res;
	for(
		size_t i = p;
		i <
			#ifdef DBGBUFDMP
				p + min<string::size_type>((string::size_type)DBGBUFDMP, size());
			#else
				p + size();
			#endif
		i++
	) {
		res += (format(" %02X") % (unsigned int)(unsigned char)(*this)[i]).str();
		if(((i + 1 - p) % DBGCR) == 0) {
			dbglog(res + "\n")
			res.clear();
		}
	}
	dbglog(res + "\n")
}
#endif

void						mutex::			genlock(char t, function<void()> lp) {
	if(fatx_context::get()->mmi.prog == frontend::fuse) {
		#if defined DEBUG && defined DBG_SEM
			int nb = cpt++;
			#ifdef DBGSEM
				if(nam == DBGSEM)
			#endif
			dbglog((format(">>> Lock request [%c:%d-%s]\n") % t % nb % nam).str())
		#else
			(void) t;
		#endif
		#ifndef NO_LOCK
			lp();
		#else
			(void) lp;
		#endif
		#if defined DEBUG && defined DBG_SEM
			#ifdef DBGSEM
				if(nam == DBGSEM)
			#endif
			dbglog((format("    done [%c:%d-%s].\n") % t % nb % nam).str())
		#endif
	}
}
void						mutex::			genunlock(char t, function<void()> ulp) {
	if(fatx_context::get()->mmi.prog == frontend::fuse) {
		#if defined DEBUG && defined DBG_SEM
			int nb = --cpt;
			#ifdef DBGSEM
				if(nam == DBGSEM)
			#endif
			dbglog((format("<<< Lock release [%c:%d-%s]\n") % t % nb % nam).str())
		#else
			(void) t;
		#endif
		#ifndef NO_LOCK
			ulp();
		#else
			(void) ulp;
		#endif
		#if defined DEBUG && defined DBG_SEM
			#ifdef DBGSEM
				if(nam == DBGSEM)
			#endif
			dbglog((format("    done [%c:%d-%s].\n") % t % nb % nam).str())
		#endif
	}
}
bool						mutex::			gentimedlock(char t, function<bool()> plp) {
	if(fatx_context::get()->mmi.prog == frontend::fuse) {
		#if defined DEBUG && defined DBG_SEM
			int nb = cpt++;
			#ifdef DBGSEM
				if(nam == DBGSEM)
			#endif
			dbglog((format(">>> Lock request [%c:%d-%s]\n") % t % nb % nam).str())
		#else
			(void) t;
		#endif
		bool res = true;
		#ifndef NO_LOCK
			res = plp();
		#else
			(void) plp;
		#endif
		#if defined DEBUG && defined DBG_SEM
			#ifdef DBGSEM
				if(nam == DBGSEM)
			#endif
			dbglog((format("    done [%c:%d-%s].\n") % t % nb % nam).str())
		#endif
		return res;
	}
	else
		return true;
}
void						mutex::			genunlockupgradableandlock(function<void()> ulualp) {
	if(fatx_context::get()->mmi.prog == frontend::fuse) {
		#if defined DEBUG && defined DBG_SEM
			#ifdef DBGSEM
				if(nam == DBGSEM)
			#endif
			dbglog((format(">>> Lock request [U->X-%s]\n") % nam).str())
		#endif
		#ifndef NO_LOCK
			ulualp();
		#else
			(void) ulualp;
		#endif
		#if defined DEBUG && defined DBG_SEM
			#ifdef DBGSEM
				if(nam == DBGSEM)
			#endif
			dbglog((format("    done [%s].\n") % nam).str())
		#endif
	}
}
void						mutex::			genunlockandlockupgradable(function<void()> ulalup) {
	if(fatx_context::get()->mmi.prog == frontend::fuse) {
		#if defined DEBUG && defined DBG_SEM
			#ifdef DBGSEM
				if(nam == DBGSEM)
			#endif
			dbglog((format("<<< Lock release [X->U-%s]\n") % nam).str())
		#endif
		#ifndef NO_LOCK
			ulalup();
		#else
			(void) ulalup;
		#endif
		#if defined DEBUG && defined DBG_SEM
			#ifdef DBGSEM
				if(nam == DBGSEM)
			#endif
			dbglog((format("    done [%s].\n") % nam).str())
		#endif
	}
}

#ifndef ENABLE_XBOX
void						console::		write(const string s, bool err) {
	(err ? cerr : cout) << s;
}
pair<bool, bool>			console::		read() {
	char c, d;
	c = d = cin.get();
	while(d != '\n')
		d = cin.get();
	return ((c == 'y') || (c == 'Y')) ? make_pair(true, true) : !((c == 'n') || (c == 'N')) ? make_pair(true, false) : make_pair(false, false);
}
#endif

							fatx_context::	fatx_context(frontend& m): mmi(m), fat(nullptr), root(nullptr) {
}
							fatx_context::	~fatx_context() {
	destroy();
	set(nullptr);
}
bool						fatx_context::	setup() {
	if(!dev.setup())
		return false;
    if(mmi.runas.length() > 0 && !drop_privileges(mmi.runas))
        return false;
	if(!par.setup())
		return false;
	if(mmi.prog == frontend::fsck || mmi.prog == frontend::unrm || (mmi.prog == frontend::fuse && mmi.recover))
		fat = new memmap(par);
	else
		fat = new dskmap(par);
	#if defined DEBUG && defined DBG_INIT
		dbglog("::EOMAP\n")
	#endif
	if(mmi.prog != frontend::mkfs) {
		root = new entry();
		#if defined DEBUG && defined DBG_INIT
			dbglog("::EOENT\n")
		#endif
	}
	return fat != nullptr && (mmi.prog == frontend::mkfs || root != nullptr);
}
void						fatx_context::	destroy() {
	delete root;
	root = nullptr;
	delete fat;
	fat = nullptr;
}

#ifndef ENABLE_XBOX
							frontend::		frontend(int ac, const char* const * const av) :
	readonly(false),		prog(unknown),			force_y(false),			force_n(false),			force_a(false),
	verbose(false),			recover(false),			local(false),			deldate(true),			dellost(true),
	fuse_debug(false),		fuse_foregrd(false),	fuse_singlethr(false),	nofat(false),			argc(ac),
	argv(av),				progname(av[0]),		dialog(true),			lostfound(def_landf),	foundfile(def_fpre),
	filecount(0),			mount(),				volname(),				fuse_option(),			unkopt(),
	partition("x2"),		table(),				clus_size(0),			uid(getuid()),			gid(getgid()),
	mask(
		#ifndef NO_FUSE
			S_IRUSR | S_IWUSR | S_IXUSR |
			S_IRGRP | S_IXGRP |
			S_IROTH | S_IXOTH
		#else
			0
		#endif
	), allyes(true), offset(0), size(0), input(), script(), runas("") {
}
bool						frontend::		getanswer(bool def) {
	bool res = false;
	console::write(def ? " [Y/n] :" : " [y/N] :");
	if(force_n) {
		console::write("n\n");
		res = false;
	}
	else if(force_y) {
		console::write("y\n");
		res = true;
	}
	else if(force_a) {
		console::write(def ? "y\n" : "n\n");
		res = def;
	}
	else {
		pair<bool, bool> r = console::read();
		res = r.first ? r.second : def;
	}
	allyes = allyes && res;
	return res;
}
#endif
							frontend::		~frontend() {
	progname.clear();
	lostfound.clear();
	foundfile.clear();
	mount.clear();
	volname.clear();
	fuse_option.clear();
	unkopt.clear();
	partition.clear();
	input.clear();
	script.clear();
}
string						frontend::		name() {
	return
		prog == fuse ? "fusefatx" :
		prog == mkfs ? "mkfs.fatx" :
		prog == fsck ? "fsck.fatx" :
		prog == unrm ? "unrm.fatx" :
		prog == label ? "label.fatx" :
		"fatx"
	;
}
bool						frontend::		setup() {
	size_t pos;
	if((pos = progname.rfind(sepdir, progname.size())) != string::npos)
		progname = progname.substr(pos + 1);
	if(progname.compare("fusefatx") == 0)
		prog	= fuse;
	if(progname.compare("mkfs.fatx") == 0) {
		prog	= mkfs;
		dialog	= false;
	}
	if(progname.compare("fsck.fatx") == 0) {
		prog	= fsck;
		dialog	= false;
	}
	if(progname.compare("unrm.fatx") == 0) {
		prog	= unrm;
		recover	= true;
		dialog	= false;
	}
	if(progname.compare("label.fatx") == 0) {
		prog	= label;
		dialog	= false;
	}
	#ifndef NO_OPTION
	options_description hidden;
	variables_map varmap;
	hidden.add_options()
		("default", "display options default values")
		("as", value<string>(),
			"choose program behavior\n"
			"\"fuse\"  for fusefatx,\n"
			"\"fsck\"  for fsck.fatx,\n"
			"\"mkfs\"  for mkfs.fatx,\n"
			"\"unrm\"  for unrm.fatx,\n"
			"\"label\" for label.fatx"
		)
		("do", value<string>(), "send a script to the program")
	;
	vector<string> visopt;
	try {
		parsed_options parsed(command_line_parser(argc, argv)
			.options(hidden)
			.allow_unregistered()
			.run()
		);
		store(parsed, varmap);
		visopt = collect_unrecognized(parsed.options, include_positional);
		notify(varmap);
	}
	catch(std::exception& e) {
		ostringstream s;
		s << hidden;
		console::write(s.str());
		s.str("");
		s << e.what();
		console::write(s.str() + "\n");
		prog = unknown;
	}
	if(varmap.count("as")) {
		if(varmap["as"].as<string>() == "fuse")
			prog = fuse;
		if(varmap["as"].as<string>() == "mkfs")
			prog = mkfs;
		if(varmap["as"].as<string>() == "fsck")
			prog = fsck;
		if(varmap["as"].as<string>() == "unrm")
			prog = unrm;
		if(varmap["as"].as<string>() == "label")
			prog = label;
	}
	if(varmap.count("do"))
		script = varmap["do"].as<string>();
	#endif
	if(prog == mkfs || prog == fsck || prog == unrm || prog == label)
		dialog	= false;
	if(prog == unrm)
		recover	= true;
	#ifdef DEBUG
		dbglog((format("=> CALL: %s\n") % name()).str())
	#endif
	if(prog != fuse && prog != fsck && prog != mkfs && prog != unrm && prog != label) {
		console::write(
			"Invalid usage.\n"
			"Please use link to this executable as:\n"
			"- fusefatx\tto mount a filesystem with fuse\n"
			"- mkfs.fatx\tto create a new filesystem\n"
			"- fsck.fatx\tto check a filesystem\n"
			"- unrm.fatx\tto try to recover deleted files\n"
			"- label.fatx\tto display or change filesystem label\n"
		);
		return false;
	}
	#ifndef NO_OPTION
	options_description visible((format("Usage: %s [options] device%s")
		% name()
		% (prog == fuse ? " mountpoint" : (prog == label ? " [label]" : ""))
	).str());
	visible.add_options()
		("help,h", "produce help message")
		("version", "produce version number")
		("verbose,v", "verbose output")
		("input,i", value<string>(), "set input device/file")
		("offset", value<streamptr>(), "force partition offset")
		("size", value<streamptr>(), "force partition size")
		("partition,p", value<string>()->default_value("x2"),
			"select partition:\n"
			"\"sc\" for system cache,\n"
			"\"gc\" for game cache,\n"
			"\"cp\" for content partition,\n"
			"\"x1\" for xbox 1,\n"
			"\"x2\" for xbox 2 (default)"
		)
	;
	if(prog == fuse) {
		visible.add_options()
			("mount,m", value<string>(), "set mountpoint")
			("recover,r", "mount with deleted files")
			("option,o", value<string>(), "mount options")
			("debug,d", "enable debug output (implies -f)")
			("foregrd,f", "foreground operation")
			("singlethr,s", "fuse on single thread")
			("uid",  value<uid_t>(), "sets uid of the filesystem")
			("gid",  value<gid_t>(), "sets gid of the filesystem")
			("mask",  value<string>(), "sets mask for entries modes")
            ("runas", value<string>(), "drop privileges after opening input device")
		;
	}
	if(prog == label || prog == mkfs) {
		visible.add_options()
			("label,l", value<string>(), "set volume name")
		;
	}
	if(prog == mkfs) {
		visible.add_options()
			("cls-size,c", value<streamptr>(), "set num of blocks per cluster")
			("table,b", value<string>(),
				"select partition table:\n"
				"\"mu\"   for Memory Unit,\n"
				"\"file\" for plain file,\n"
				"\"hd\"   for XBOX360 HDD,\n"
				"\"kit\"  for DevKit HDD,\n"
				"\"usb\"  for USB Drive"
			)
		;
	}
	if(prog == fsck || prog == unrm || prog == mkfs) {
		visible.add_options()
			("all,y", "answer yes to everything")
			("none,n", "answer no to everything")
			("auto,a", "default answer to everything")
		;
	}
	if(prog == fsck || prog == unrm || prog == mkfs || prog == fuse) {
		visible.add_options()
			("test,t", "test mode, no modification done")
		;
	}
	if(prog == unrm) {
		visible.add_options()
			("local,l", "recover files in local filesystem")
		;
	}
	if(prog == fsck || prog == unrm) {
		visible.add_options()
			("nofat,f", "disable FAT sanity check and recovery")
		;
	}
	if(prog == fuse || prog == unrm) {
		visible.add_options()
			("nodate", "dates of deleted files don't care")
			("nolost", "don't care of lost chains")
		;
	}
	positional_options_description	podesc;
	podesc.add("input", 1);
	if(prog == fuse)
		podesc.add("mount", 1);
	if(prog == label)
		podesc.add("label", 1);
	try {
		if(prog == fuse) {
			parsed_options parsed(command_line_parser(visopt)
				.options(visible)
				.positional(podesc)
				.allow_unregistered()
				.run()
			);
			store(parsed, varmap);
			unkopt = collect_unrecognized(parsed.options, exclude_positional);
		}
		else {
			parsed_options parsed(command_line_parser(visopt)
				.options(visible)
				.positional(podesc)
				.run()
			);
			store(parsed, varmap);
		}
		notify(varmap);
	}
	catch(std::exception& e) {
		ostringstream s;
		s << visible;
		console::write(s.str());
		s.str("");
		s << e.what();
		console::write(s.str() + "\n");
		prog = unknown;
	}
	if(varmap.count("all"))
		force_y		= true;
	if(varmap.count("none"))
		force_n		= true;
	if(varmap.count("auto"))
		force_a		= true;
	if(varmap.count("test"))
		readonly	= true;
	if(varmap.count("verbose"))
		verbose		= true;
	if(varmap.count("recover")) {
		recover		= true;
		readonly	= true;
	}
	if(varmap.count("local")) {
		local		= true;
		readonly	= true;
	}
	if(varmap.count("nofat")) {
		nofat		= true;
	}
	if(varmap.count("nodate")) {
		deldate		= false;
		if(prog == fuse) {
			recover		= true;
			readonly	= true;
		}
	}
	if(varmap.count("nolost")) {
		dellost		= false;
		if(prog == fuse) {
			recover		= true;
			readonly	= true;
		}
	}
	if(varmap.count("debug")) {
		fuse_debug		= true;
		fuse_foregrd	= true;
	}
	if(varmap.count("singlethr"))
		fuse_singlethr	= true;
	if(varmap.count("foregrd"))
		fuse_foregrd	= true;
	if(varmap.count("mount"))
		mount			= varmap["mount"].as<string>();
	if(varmap.count("label"))
		volname			= varmap["label"].as<string>();
	if(varmap.count("partition"))
		partition		= varmap["partition"].as<string>();
	if(varmap.count("table"))
		table			= varmap["table"].as<string>();
	if(varmap.count("cls-size"))
		clus_size		= varmap["cls-size"].as<streamptr>();
	if(varmap.count("uid"))
		uid				= varmap["uid"].as<uid_t>();
	if(varmap.count("gid"))
		gid				= varmap["gid"].as<gid_t>();
	if(varmap.count("mask")) {
		stringstream ss;
		ss << std::oct << varmap["mask"].as<string>();
		ss >> mask;
	}
	if(varmap.count("offset"))
		offset			= varmap["offset"].as<streamptr>();
	if(varmap.count("size"))
		size			= varmap["size"].as<streamptr>();
	if(varmap.count("input"))
		input			= varmap["input"].as<string>();
    if(varmap.count("runas"))
        runas           = varmap["runas"].as<string>();
	if(prog == label)
		readonly		= !varmap.count("label");
	if(varmap.count("option")) {
		tokenizer<char_separator<char> > opts(varmap["option"].as<string>(), char_separator<char>(","));
		for(const string& o: opts) {
			if(o == "ro")
				readonly = true;
			else {
				if(!fuse_option.empty())
					fuse_option += ",";
				fuse_option += o;
			}
		}
	}
	if(varmap.count("version")) {
		console::write((format("%s v%s.\nCopyright (C) 2012, 2013, 2014 Christophe Duverger.\n\n")
			% name()
			% VERSION
		).str());
		console::write(
			"This program comes with ABSOLUTELY NO WARRANTY.\n"
			"This is free software, and you are welcome to redistribute it\n"
			"under certain conditions.\n"
		);
		prog = unknown;
		return true;
	}
	if(varmap.count("default")) {
		console::write(
			(format("force yes\t%d\n")		% force_y).str() +
			(format("force no\t%d\n")		% force_n).str() +
			(format("force auto\t%d\n")		% force_a).str() +
			(format("verbose\t\t%d\n")		% verbose).str() +
			(format("read only\t%d\n")		% readonly).str() +
			(format("fuse recover\t%d\n")	% recover).str() +
			(format("local recover\t%d\n")	% local).str() +
			(format("deleted dates\t%d\n")	% deldate).str() +
			(format("preserve losts\t%d\n")	% dellost).str() +
			(format("partition\t%d\n")		% partition).str() +
			(format("fuse debug\t%d\n")		% fuse_debug).str() +
			(format("fuse foregrd\t%d\n")	% fuse_foregrd).str() +
			(format("fuse singlethr\t%d\n")	% fuse_singlethr).str() +
			(format("uid\t\t%d\n")			% uid).str() +
			(format("gid\t\t%d\n")			% gid).str() +
			(format("mask\t\t%03o\n")		% mask).str()
		);
		return false;
	}
	if(varmap.count("help") || !varmap.count("input") || (prog == fuse && !varmap.count("mount"))) {
		ostringstream s;
		s << visible;
		console::write(s.str());
		return false;
	}
	#endif
	#if defined DEBUG && defined DBG_INIT
		dbglog("::EOMMI\n")
	#endif
	return true;
}
void						frontend::		parser() {
	#ifndef ENABLE_XBOX
		script.erase(remove_if(script.begin(), script.end(), [] (const char c) ->bool { return c == ' ' || c == '\t' || c == '\n'; }), script.end());
		tokenizer<char_separator<char> > cmds(script, char_separator<char>(";"));
		for(string cmd: cmds) {
			tokenizer<char_separator<char> > args(cmd, char_separator<char>(","));
			tokenizer<char_separator<char> >::iterator i = args.begin();
			if(i->empty())
				break;
			else if((*i)[0] == '#')
				continue;
			else if(*i == "mkdir" && ++i != args.end() && !i->empty()) {
				console::write("mkdir:");
				if(!writeable()) {
					console::write("read-only\n");
					continue;
				}
				size_t l = i->find_last_of(sepdir);
				if(l == string::npos || l == i->length() - 1) {
					console::write("nothing\n");
					continue;
				}
				entry* n = new entry(i->substr(l + 1), 0, true);
				entry* s = fatx_context::get()->root->find(&(i->substr(0, l))[0]);
				if(s == nullptr) {
					console::write("nothing\n");
					continue;
				}
				s->addtodir(n);
				console::write(n->path() + "\n");
			}
			else if(*i == "rmdir" && ++i != args.end() && !i->empty()) {
				console::write("rmdir:");
				if(!writeable()) {
					console::write("read-only\n");
					continue;
				}
				entry* n = fatx_context::get()->root->find(&(*i)[0]);
				if(n == nullptr || !n->flags.dir) {
					console::write("nothing\n");
					continue;
				}
				if(n->childs.size() != 0) {
					console::write("not empty\n");
					continue;
				}
				console::write(n->path() + "\n");
				n->parent->remfrdir(n);
			}
			else if(*i == "cp" && ++i != args.end() && !i->empty()) {
				console::write("cp:");
				if(!writeable()) {
					console::write("read-only\n");
					continue;
				}
				entry* s = fatx_context::get()->root->find(&(*i++)[0]);
				if(s == nullptr) {
					console::write("nothing\n");
					continue;
				}
				size_t l = i->find_last_of(sepdir);
				if(l == string::npos || l == i->length() - 1) {
					console::write("nothing\n");
					continue;
				}
				entry* d = fatx_context::get()->root->find(&(i->substr(0, l))[0]);
				if(d == nullptr) {
					console::write("nothing\n");
					continue;
				}
				entry* n = new entry(i->substr(l + 1), s->size);
				d->addtodir(n);
				string b(s->size, '\0');
				s->data(&b[0], true, 0, s->size);
				n->data(&b[0], false, 0, n->size);
				console::write(n->path() + "\n");
			}
			else if(*i == "rcp" && ++i != args.end() && !i->empty()) {
				console::write("rcp:");
				ifstream s(&(*i++)[0], ios::binary);
				if(!s) {
					console::write("nothing\n");
					continue;
				}
				s.seekg(0, ios::end);
				streamptr size = s.tellg();
				s.seekg(0, ios::beg);
				size_t l = i->find_last_of(sepdir);
				if(l == string::npos || l == i->length() - 1) {
					console::write("nothing\n");
					continue;
				}
				entry* d = fatx_context::get()->root->find(&(i->substr(0, l))[0]);
				if(d == nullptr) {
					console::write("nothing\n");
					continue;
				}
				entry* n = new entry(i->substr(l + 1), size);
				d->addtodir(n);
				string b(size, '\0');
				s.read(&b[0], size);
				s.close();
				console::write((format("(%d)") % size).str());
				n->data(&b[0], false, 0, n->size);
				console::write(n->path() + "\n");
			}
			else if(*i == "lcp" && ++i != args.end() && !i->empty()) {
				console::write("lcp:");
				entry* s = fatx_context::get()->root->find(&(*i++)[0]);
				if(s == nullptr) {
					console::write("nothing\n");
					continue;
				}
				ifstream t;
				t.open(&(*i)[0]);
				if(t) {
					t.close();
					console::write("local file exists\n");
					continue;
				}
				ofstream d(&(*i)[0], ios::binary);
				d.seekp(0, ios::beg);
				string b(s->size, '\0');
				s->data(&b[0], true, 0, s->size);
				console::write((format("(%d)") % s->size).str());
				d.write(&b[0], s->size);
				d.close();
				console::write(*i + "\n");
			}
			else if(*i == "mv" && ++i != args.end() && !i->empty()) {
				console::write("mv:");
				if(!writeable()) {
					console::write("read-only\n");
					continue;
				}
				entry* n = fatx_context::get()->root->find(&(*i++)[0]);
				if(n == nullptr) {
					console::write("nothing\n");
					continue;
				}
				n->rename(&(*i)[0]);
				console::write(n->path() + "\n");
			}
			else if(*i == "rm" && ++i != args.end() && !i->empty()) {
				console::write("rm:");
				if(!writeable()) {
					console::write("read-only\n");
					continue;
				}
				entry* n = fatx_context::get()->root->find(&(*i)[0]);
				if(n == nullptr || n->flags.dir) {
					console::write("nothing\n");
					continue;
				}
				console::write(n->path() + "\n");
				n->parent->remfrdir(n);
			}
			else if(*i == "lsfat" && ++i != args.end() && !i->empty()) {
				console::write(*i + ":");
				const entry* e = fatx_context::get()->root->find(&(*i)[0]);
				if(e != nullptr)
					console::write(fatx_context::get()->fat->printchain(e->cluster));
				else
					console::write("not found");
				console::write("\n");
			}
			else if(*i == "mklost") {
				clusptr p = 0;
				console::write(*i + ":");
				if(!writeable()) {
					console::write("read-only\n");
					continue;
				}
				while(++i != args.end()) {
					clusptr s = 0;
					clusptr e = 0;
					size_t l = i->find_last_of(":");
					if(l != string::npos) {
						try {
							s = lexical_cast<clusptr>(i->substr(0, l));
						}
						catch(bad_lexical_cast &) {
							console::write("*ERR*");
							break;
						}
						try {
							e = lexical_cast<clusptr>(i->substr(l + 1));
						}
						catch(bad_lexical_cast &) {
							console::write("*ERR*");
							break;
						}
					}
					else {
						try {
							s = lexical_cast<clusptr>(*i);
						}
						catch(bad_lexical_cast &) {
							console::write("*ERR*");
							break;
						}
					}
					do {
						if(p != 0) {
							console::write((format("0x%08X->") % p).str());
							fatx_context::get()->fat->write(p, s);
						}
						p = s++;
					} while(e != 0 && p != e);
				}
				if(p != 0) {
					console::write((format("0x%08X->EOC") % p).str());
					fatx_context::get()->fat->write(p, EOC);
				}
				console::write("\n");
			}
			else if(*i == "rmfat") {
				console::write(*i + ":");
				if(!writeable()) {
					console::write("read-only\n");
					continue;
				}
				while(++i != args.end()) {
					clusptr s = 0;
					clusptr e = 0;
					size_t l = i->find_last_of(":");
					if(l != string::npos) {
						try {
							s = lexical_cast<clusptr>(i->substr(0, l));
						}
						catch(bad_lexical_cast &) {
							console::write("*ERR*");
							break;
						}
						try {
							e = lexical_cast<clusptr>(i->substr(l + 1));
						}
						catch(bad_lexical_cast &) {
							console::write("*ERR*");
							break;
						}
					}
					else {
						try {
							s = lexical_cast<clusptr>(*i);
						}
						catch(bad_lexical_cast &) {
							console::write("*ERR*");
							break;
						}
					}
					do {
						console::write((format("0x%08X ") % s).str());
						fatx_context::get()->fat->write(s++, FLK);
					} while(e != 0 && s != e);

				}
				console::write("\n");
			}
			else if(*i == "help") {
				console::write(
					"syntax: cmd, arg1, arg2, ...[; cmd, arg1, ...[; ...]]\n"
					"\tmkdir,\t/path/to/newdir\n"
					"\trmdir,\t/path/to/dir\n"
					"\tcp,\t/path/to/src, /path/to/dst\n"
					"\trcp,\t/path/to/local/src, /path/to/dst\n"
					"\tlcp,\t/path/to/src, /path/to/local/dst\n"
					"\tmv,\t/path/to/src, /path/to/dst\n"
					"\trm,\t/path/to/file\n"
					"\tlsfat,\t/path/to/file\n"
					"\tmklost,\tclus1, start:end, ...\n"
					"\trmfat,\tclus1, start:end, ...\n"
					"\t#comment, ...\n"
				);
			}
			else
				console::write(*i + ":unknown" + "\n");
		}
	#endif
}

							device::		device() :
		#ifndef NO_IO
			io(0),
		#endif
		#ifndef NO_FD
			fd(0),
		#endif
		tot_size(0), changes(false), authd("DEV") {
}
							device::		~device() {
	#ifndef NO_IO
		if(io) {
			io->close();
			delete io;
		}
		io = nullptr;
	#endif
	#ifndef NO_FD
		if(fd)
			fclose(fd);
		fd = nullptr;
	#endif
}
string						device::		read(const streamptr& p, const size_t s) {
	if(s == 0)
		return string();
	if(size() && p + s > size()) {
		console::write((format("Blocks out of bounds ([0x%016X ; 0x%016X] > 0x%016X).\n") % p % (p + s - 1) % size()).str(), true);
		return string();
	}
	string res(s, '\0');
	bool status = false;
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(authd);
	#endif
	#ifndef NO_IO
		io->seekg(p, ios::beg);
		if(io->bad() || io->fail()) {
			io->clear();
			console::write((format("Unreachable block at 0x%016X.\n") % p).str(), true);
			return string();
		}
		io->read(&res[0], s);
		status = io->bad() || io->fail();
	#endif
	#if !defined NO_FD && defined NO_IO
		fseek(fd, p, SEEK_SET);
		if(ferror(fd) != 0) {
			clearerr(fd);
			console::write((format("Unreachable block at 0x%016X.\n") % p).str(), true);
			return string();
		}
		fread(&res[0], s, 1, fd);
		status = (ferror(fd) != 0);
	#endif
	#if defined DEBUG && defined DBG_READ
		devlog(true, p, res);
	#endif
	if(status) {
		console::write((format("Unreadable block at 0x%016X.\n") % p).str(), true);
		#ifndef NO_IO
			io->clear();
		#endif
		#if !defined NO_FD && defined NO_IO
			clearerr(fd);
		#endif
		return string();
	}
	return res;
}
bool						device::		write(const streamptr& p, const string& s) {
	if(s.empty())
		return true;
	if(p + s.size() > size()) {
		console::write((format("Blocks out of bounds ([0x%016X;0x%016X] > 0x%016X).\n") % p % (p + s.size() - 1) % size()).str(), true);
		return false;
	}
	if(!fatx_context::get()->mmi.writeable())
		return true;
	bool status = false;
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(authd);
	#endif
	#ifndef NO_IO
		io->seekp(p, ios::beg);
		if(io->bad() || io->fail()) {
			io->clear();
			console::write((format("Unreachable block at 0x%016X.\n") % p).str(), true);
			return false;
		}
	#endif
	#if !defined NO_FD && defined NO_IO
		fseek(fd, p, SEEK_SET);
		if(ferror(fd) != 0) {
			clearerr(fd);
			console::write((format("Unreachable block at 0x%016X.\n") % p).str(), true);
			return false;
		}
	#endif
	#if defined DEBUG && defined DBG_WRITE
		devlog(false, p, s);
	#endif
	#ifndef NO_WRITE
		#ifndef NO_IO
			io->write(&s[0], s.size());
			status = io->bad() || io->fail();
		#endif
		#if !defined NO_FD && defined NO_IO
			fwrite(&s[0], s.size(), 1, fd);
			status = (ferror(fd) != 0);
		#endif
		changes = true;
	#endif
	if(status) {
		#ifndef NO_IO
			io->clear();
		#endif
		#if !defined NO_FD && defined NO_IO
			clearerr(fd);
		#endif
		console::write((format("Unwriteable block at 0x%016X.\n") % p).str(), true);
		return false;
	}
	return true;
}
bool						device::		setup() {
	bool err = false;
	#ifndef NO_IO
		io = new fstream(fatx_context::get()->mmi.input.data(), ios::binary | (fatx_context::get()->mmi.writeable() ? (ios::out | ios::in) : ios::in));
		if(io == 0)
			err = true;
		else {
			#ifndef NO_FD
				io->sync_with_stdio();
			#endif
			io->seekg(0);
			err = err || io->bad() || io->fail();
			io->seekg(0, ios::end);
			err = err || io->bad() || io->fail();
			tot_size = io->tellg();
			if(fatx_context::get()->mmi.writeable()) {
				io->seekp(0);
				err = err || io->bad() || io->fail();
				io->seekp(0, ios::end);
				err = err || io->bad() || io->fail();
			}
			if(err) {
				io->close();
				io = 0;
			}
		}
	#endif
	#ifndef NO_FD
		fd = fopen(fatx_context::get()->mmi.input.data(), fatx_context::get()->mmi.writeable() ? "rb+" : "rb");
		if(fd == 0)
			err = true;
		else {
			#ifdef NO_IO
				fseek(fd, 0, SEEK_SET);
				err = err || (ferror(fd) != 0);
				fseek(fd, 0, SEEK_END);
				err = err || (ferror(fd) != 0);
				tot_size = ftell(fd);
			#endif
			#ifndef NO_FCNTL
				flock fl;
				fl.l_type	= fatx_context::get()->mmi.writeable() ? F_WRLCK : F_RDLCK;
				fl.l_whence	= SEEK_SET;
				fl.l_start	= 0;
				fl.l_len	= tot_size;
				err = err || fcntl(fileno(fd), F_SETLK, &fl);
			#endif
			if(err) {
				fclose(fd);
				fd = 0;
			}
		}
	#endif
	if(err) {
		console::write((format("Error opening %s for read%s\n")
			% (fatx_context::get()->mmi.input)
			% (fatx_context::get()->mmi.writeable() ? "/write" : "")
		).str(), true);
	}
	#if defined DEBUG && defined DBG_INIT
		dbglog("::EODEV\n")
	#endif
	return !err;
}
#ifdef DEBUG
string						device::		address(const streamptr& p) const {
	return (format("0x%016X[%s:0x%08X]")
		% p
		% ((p < fatx_context::get()->par.fat_start) ? "PAR" : (p < fatx_context::get()->par.root_start) ? "FAT" : "CLS")
		% ((p < fatx_context::get()->par.fat_start) ? p : (p < fatx_context::get()->par.root_start) ? ((p - fatx_context::get()->par.fat_start) / fatx_context::get()->par.chain_size) : clsarithm::ptr2cls(p))
	).str();
}
void						device::		devlog(bool r, const streamptr& p, const string& s) const {
	dbglog((format("->  %s at %s (%d) :%s")
		% (r ? "read" : "write")
		% address(p)
		% s.size()
		% ((s.size() <= DBGCR) ? "" : "\n")
	).str());
	string sl;
	for(
		size_t i = 0;
		i <
			#ifdef DBGLIMIT
				min<string::size_type>((string::size_type)DBGLIMIT, s.size());
			#else
				s.size();
			#endif
		i++
	) {
		sl += (format(" %02X") % (unsigned int)(unsigned char)s[i]).str();
		if(((i + 1) % DBGCR) == 0) {
			dbglog(sl + "\n")
			sl.clear();
		}
	}
	dbglog(sl + "\n")
}
string						device::		print(const streamptr& p, const size_t& s, const size_t& g) {
	string res;
	string buf;
	size_t i = 1;
	for(const char c: read(p, s)) {
		buf += c;
		if((i % g) == 0) {
			for(size_t j = 0; j < g; j++)
				res += (format("%02X ") % (unsigned int)(unsigned char)buf[j]).str();
			for(size_t j = 0; j < g; j++)
				res += (buf[j] >= ' ' && buf[j] <= '~') ? buf[j] : '.';
			res += '\n';
			buf.erase();
		}
		i++;
	}
	return res;
}
#endif

							fatxpar::		fatxpar() :
	par_id(0),				par_label(),			par_start(0),			par_size(0),			clus_size(0),			clus_pow(0),
	clus_num(0),			clus_fat(0),			chain_size(0),			chain_pow(0),			fat_start(0),			fat_size(0),
	root_start(0),			root_clus(0) {
}
bool						fatxpar::		setup() {
	uint64_t ts = fatx_context::get()->dev.size();
	if(fatx_context::get()->mmi.verbose)
		console::write((format("Support size: %d.\n") % ts).str());
	bool found = false;
	if(
		fatx_context::get()->mmi.table.empty() &&
		fatx_context::get()->mmi.offset != 0 &&
		fatx_context::get()->dev.read(fatx_context::get()->mmi.offset).find(fsid, 0) == 0
	) {
		par_start = fatx_context::get()->mmi.offset;
		if(fatx_context::get()->mmi.verbose)
			console::write((format("Found FATX partition at 0x%016X.\n") % par_start).str());
		found = true;
	}
	if(fatx_context::get()->mmi.table == "mu" || fatx_context::get()->mmi.table == "file" || (fatx_context::get()->mmi.table.empty() && !found &&
		fatx_context::get()->dev.read(0).find(fsid, 0) == 0
	)) {
		if(ts > 0x7FF000
			&& ((!fatx_context::get()->mmi.table.empty() && fatx_context::get()->mmi.table == "mu")
			|| (fatx_context::get()->mmi.table.empty() && fatx_context::get()->dev.read(0x7FF000).find(fsid, 0) == 0))
		) {
			if(fatx_context::get()->mmi.verbose)
				console::write((boost::format("%s FATX partition in Memory Unit.\n") % (fatx_context::get()->mmi.table.empty() ? "Found" : "Force")).str());
			if(fatx_context::get()->mmi.partition == "sc") {
				par_start	= 0;
				par_size	= 0x7FF000;
			}
			else {
				par_start	= 0x7FF000;
				par_size	= ts - par_start;
				fatx_context::get()->mmi.partition = "x2";
			}
		}
		else {
			if(fatx_context::get()->mmi.verbose)
				console::write((boost::format("%s FATX partition in partition file.\n") % (fatx_context::get()->mmi.table.empty() ? "Found" : "Force")).str());
			par_start	= 0;
			par_size	= ts - par_start;
		}
		found = true;
	}
	if(ts > 0x130eb0000 && (fatx_context::get()->mmi.table == "hd" || (fatx_context::get()->mmi.table.empty() && !found &&
		fatx_context::get()->dev.read(0x130eb0000	).find(fsid, 0) == 0
	))) {
		if(fatx_context::get()->mmi.verbose)
			console::write((boost::format("%s FATX partition in XBox360 HDD.\n") % (fatx_context::get()->mmi.table.empty() ? "Found" : "Force")).str());
		if(fatx_context::get()->mmi.partition == "sc"
			&& (!fatx_context::get()->mmi.table.empty() || fatx_context::get()->dev.read(0x80000	).find(fsid, 0) == 0)
		) {
			par_start	= 0x80000;
			par_size	= 0x80000000;
		}
		else if(fatx_context::get()->mmi.partition == "gc"
			&& (!fatx_context::get()->mmi.table.empty() || fatx_context::get()->dev.read(0x80080000	).find(fsid, 0) == 0)
		) {
			par_start	= 0x80080000;
			par_size	= 0xA0E30000;
		}
		else if(fatx_context::get()->mmi.partition == "x1"
			&& (!fatx_context::get()->mmi.table.empty() || fatx_context::get()->dev.read(0x120eb0000).find(fsid, 0) == 0)
		) {
			par_start	= 0x120eb0000;
			par_size	= 0x10000000;
		}
		else {
			par_start	= 0x130eb0000;
			par_size	= ts - par_start;
			fatx_context::get()->mmi.partition = "x2";
		}
		found = true;
	}
	if(ts > 0x20000000 && (fatx_context::get()->mmi.table == "usb" || (fatx_context::get()->mmi.table.empty() && !found &&
		fatx_context::get()->dev.read(0x20000000	).find(fsid, 0) == 0
	))) {
		if(fatx_context::get()->mmi.verbose)
			console::write((boost::format("%s FATX partition in USB Drive.\n") % (fatx_context::get()->mmi.table.empty() ? "Found" : "Force")).str());
		if(fatx_context::get()->mmi.partition == "sc"
			&& (!fatx_context::get()->mmi.table.empty() || fatx_context::get()->dev.read(0x8000400	).find(fsid, 0) == 0)
		) {
			par_start	= 0x8000400;
			par_size	= 0x4800000;
		}
		else {
			par_start	= 0x20000000;
			par_size	= ts - par_start;
			fatx_context::get()->mmi.partition = "x2";
		}
		found = true;
	}
	if(fatx_context::get()->mmi.table == "kit" || (fatx_context::get()->mmi.table.empty() && !found)) {
		devheader&& dh = devheader(ts);
		if(fatx_context::get()->mmi.table.empty())
			dh = devheader(&fatx_context::get()->dev.read(0)[0]);
		if(!fatx_context::get()->mmi.table.empty() || (
			dh.id == 0x00020000 &&
			fatx_context::get()->dev.read(dh.p2_start * blksize).find(fsid, 0) == 0
		)) {
			if(fatx_context::get()->mmi.verbose)
				console::write((boost::format("%s FATX partition in DevKit HDD.\n") % (fatx_context::get()->mmi.table.empty() ? "Found" : "Force")).str());
			if(fatx_context::get()->mmi.partition == "cp"
				&& (!fatx_context::get()->mmi.table.empty() || fatx_context::get()->dev.read(dh.p1_start * blksize).find(fsid, 0) == 0)
			) {
				par_start	= dh.p1_start	* blksize;
				par_size	= dh.p1_size	* blksize;
			}
			else {
				par_start	= dh.p2_start	* blksize;
				par_size	= dh.p2_size	* blksize;
				fatx_context::get()->mmi.partition = "x2";
			}
			found = true;
		}
	}
	if(!fatx_context::get()->mmi.table.empty())
		found = false;
	if(!found) {
		console::write("No FATX partition found.\n", true);
		if(fatx_context::get()->mmi.prog != frontend::mkfs)
			return false;
	}
	if(found) {
		if(fatx_context::get()->mmi.verbose)
			console::write((format("Using \"%s\" partition.\n") % fatx_context::get()->mmi.partition).str());
		bootsect	bs(&fatx_context::get()->dev.read(par_start)[0]);
		par_id		= bs.id;
		root_clus	= bs.root;
		clus_size	= blksize * (fatx_context::get()->mmi.clus_size ? fatx_context::get()->mmi.clus_size : (bs.spc == 0 || bs.spc > 0xFFFF) ? 1 : bs.spc);
	}
	else {
		par_start	= par_start ? par_start : fatx_context::get()->mmi.offset;
		par_size	= par_size ? par_size : ts - par_start;
		par_id		= 0;
		clus_size	= blksize * (fatx_context::get()->mmi.clus_size ? fatx_context::get()->mmi.clus_size : (
			par_size > 0x200000000ULL ?	512	:
			par_size > 0x100000000ULL ?	256	:
			par_size > 0x080000000ULL ?	128	:
			par_size > 0x040000000ULL ?	64	:
			par_size > 0x020000000ULL ?	32	:
			par_size > 0x010000000ULL ?	16	:
			par_size > 0x008000000ULL ?	8	:
			par_size > 0x001000000ULL ?	4	:
			par_size > 0x000800000ULL ?	8	:
			par_size > 0x000400000ULL ?	4	:
			par_size > 0x000200000ULL ?	2	:
										1
		));
		root_clus	= 1;
	}
	if(fatx_context::get()->mmi.size != 0)
		par_size = fatx_context::get()->mmi.size;
	string s(bitset<32>(clus_size).to_string());
	if(s.find('1') != s.rfind('1')) {
		console::write("Size of clusters is not a power of 2.\n", true);
		return false;
	}
	clus_pow	= 31 - s.find('1');
	clus_num	= par_size >> clus_pow;
	chain_size	= clus_num < 0xFFF0 ? 2 : 4;
	chain_pow	= chain_size == 2 ? 1 : 2;
	fat_start	= par_start + 0x1000;
	fat_size	= clus_num * chain_size;
	fat_size	+= (0x1000 - (fat_size % 0x1000));
	root_start	= fat_start + fat_size;
	clus_fat	= ((par_size - (root_start - par_start)) >> clus_pow) - 1;
	if(root_clus < 1 || root_clus > clus_fat) {
		root_clus = 1;
		if(fatx_context::get()->mmi.prog != frontend::mkfs) {
			console::write("Bad root cluster number.", fatx_context::get()->mmi.dialog);
			if(fatx_context::get()->mmi.prog == frontend::fsck) {
				console::write(" Correct it ?", fatx_context::get()->mmi.dialog);
				if(fatx_context::get()->mmi.getanswer(true))
					write();
			}
			else
				console::write("\n", fatx_context::get()->mmi.dialog);
		}
	}
	#ifdef DEBUG
		dbglog((format("PAR size  : %d\n") % par_size).str())
		dbglog((format("CLS size  : %d (%d)\n") % clus_size % clus_pow).str())
		dbglog((format("CLS num   : %d\n") % clus_num).str())
		dbglog((format("FAT start : 0x%016X\n") % fat_start).str())
		dbglog((format("FAT cls   : %d\n") % clus_fat).str())
		dbglog((format("ROOT start: 0x%016X (0x%08X)\n") % root_start % root_clus).str())
	#endif
	#if defined DEBUG && defined DBG_INIT
		dbglog("::EOPAR\n")
	#endif
	return true;
}
bool						fatxpar::		write() {
	if(fatx_context::get()->mmi.table == "kit") {
		string buf(blksize, '\0');
		devheader(fatx_context::get()->dev.size()).write(&buf[0]);
		fatx_context::get()->dev.write(0, buf);
	}
	string buf(blksize, '\0');
	bootsect(par_id, clus_size / blksize, root_clus).write(&buf[0]);
	return fatx_context::get()->dev.write(par_start, buf);
}
size_t						fatxpar::		label(unsigned char buf[slab]) const {
	size_t res = 2;
	memset(buf, 0, slab);
	buf[0] = 0xFE;
	buf[1] = 0xFF;
	for(size_t i = 0; i < par_label.size(); i++) {
		if(res == slab)
			break;
		res += 2;
		buf[res - 1] = par_label[i];
	}
	return res;
}
void						fatxpar::		label(const unsigned char buf[slab], const size_t size) {
	for(size_t i = 3; i < slab; i += 2) {
		if(i >= size)
			break;
		par_label += buf[i];
	}
}

clusptr						clsarithm::		siz2cls(const filesize& s) {
	return (s >> fatx_context::get()->par.clus_pow) + (((s - ((s >> fatx_context::get()->par.clus_pow) << fatx_context::get()->par.clus_pow)) != 0) ? 1 : 0);
}
clusptr						clsarithm::		inccls(const clusptr& p) {
	return (p <= fatx_context::get()->par.clus_fat) ? p + 1 : 2;
}
streamptr					clsarithm::		cls2ptr(const clusptr& p) {
	if(p < fatx_context::get()->par.root_clus || p > fatx_context::get()->par.clus_fat) {
		console::write((format("Cluster pointer in data out of bounds (0x%08X).\n") % p).str(), true);
		return 0;
	}
	return fatx_context::get()->par.root_start + (p - 1) * fatx_context::get()->par.clus_size;
}
clusptr						clsarithm::		ptr2cls(const streamptr& p) {
	return ((p - fatx_context::get()->par.root_start) >> fatx_context::get()->par.clus_pow) + 1;
}
streamptr					clsarithm::		cls2fat(const clusptr& p) {
	if(p < fatx_context::get()->par.root_clus || p > fatx_context::get()->par.clus_fat) {
		console::write((format("Cluster pointer in fat out of bounds (0x%08X).\n") % p).str(), true);
		return 0;
	}
	return fatx_context::get()->par.fat_start + p * fatx_context::get()->par.chain_size;
}
string						clsarithm::		clsprint(const clusptr& p, const clusptr& r) {
	return
		(p == r + 1) ? "next" : (
		(p == FLK) ? "free" : (
		(p == EOC) ? "end" :
		(format("0x%08X") % p).str()
	));
}

							dskmap::		dskmap(const fatxpar& par) :
	memnext(
		bind(&dskmap::real_read, this, _1, _2),
		bind(&dskmap::real_write, this, _1, _2),
		#define CACHESIZE (par.clus_fat * par.chain_size / max_cache_div > par.clus_size ? par.clus_fat * par.chain_size / max_cache_div : par.clus_size)
		CACHESIZE, 
		(CACHESIZE / nb_cache_div > par.clus_size ? CACHESIZE / nb_cache_div : par.clus_size)
		#undef CACHESIZE
	), authm("FAT") {
}
							dskmap::		~dskmap() {
	freegaps.clear();
	bad.clear();
}
void						dskmap::		forfat(lbdfat_t lbd) {
	clusptr c = fatx_context::get()->par.root_clus;
	for(
		streamptr p = fatx_context::get()->par.fat_start;
		p < fatx_context::get()->par.fat_start + fatx_context::get()->par.fat_size;
		p += fatx_context::get()->par.clus_size
	) {
		string buf = fatx_context::get()->dev.read(p, fatx_context::get()->par.clus_size);
		for(
			uint16_t i = (p == fatx_context::get()->par.fat_start ? fatx_context::get()->par.root_clus : 0);
			i < (fatx_context::get()->par.clus_size >> fatx_context::get()->par.chain_pow) && c < fatx_context::get()->par.clus_fat;
			i++, c++
		)
			lbd(c, (
				fatx_context::get()->par.chain_size == 4) ?
				endian<4>::litend(&buf[i * fatx_context::get()->par.chain_size])() :
				endian<2>::litend(&buf[i * fatx_context::get()->par.chain_size])()
			);
	}
}
dskmap::memnext_t::lkval_t	dskmap::		real_read(const clusptr& p, size_t s) {
	memnext_t::lkval_t res;
	s = min<size_t>(s, fatx_context::get()->par.clus_fat - p);
	string buf = fatx_context::get()->dev.read(clsarithm::cls2fat(p), fatx_context::get()->par.chain_size * s);
	for(size_t i = 0; i < buf.size(); i += fatx_context::get()->par.chain_size) {
		clusptr a = (fatx_context::get()->par.chain_size == 4) ? endian<4>::litend(&buf[i])() : endian<2>::litend(&buf[i])();
		if(fatx_context::get()->par.chain_size == 2 && a == (EOC & 0xFFFF))
			a = EOC;
		if(a != FLK && a != EOC && (a < 1 || a > fatx_context::get()->par.clus_fat) && bad.find(p + i) == bad.end()) {
			console::write((format("Cluster value in FAT out of bounds (0x%08X) for cluster 0x%08X.") % a % (p + i)).str(), fatx_context::get()->mmi.dialog);
			bad.insert(p + i);
			if(fatx_context::get()->mmi.prog == frontend::fsck) {
				console::write(" Free it ?", fatx_context::get()->mmi.dialog);
				if(fatx_context::get()->mmi.getanswer(true)) {
					write((p + i), FLK);
					a = FLK;
				}
			}
			else
				console::write("\n", fatx_context::get()->mmi.dialog);
		}
		res.push_back(memnext_t::pair_type(a, p + (i >> fatx_context::get()->par.chain_pow)));
	}
	return res;
}
bool						dskmap::		real_write(const clusptr& p, const clusptr& v) {
	string buf(fatx_context::get()->par.chain_size, '\0');
	if(fatx_context::get()->par.chain_size == 4)
		buf = endian<4>::litend(v);
	else
		buf = endian<2>::litend(v);
	return fatx_context::get()->dev.write(clsarithm::cls2fat(p), buf);
}
vareas						dskmap::		getareas(const clusptr& orig, lbdarea_t lbd) {
	#ifndef NO_LOCK
		if(lbd == 0)
			sharable_lock<mutex> lock(authm);
	#endif
	set<clusptr> sc;
	vareas res;
	if(orig == EOC || orig == FLK)
		return res;
	streamptr	area_off	= 0;
	streamptr	area_ptr	= 0;
	streamptr	area_siz	= 0;
	clusptr		area_start	= 0;
	clusptr cur_cls = orig, prv_cls = 0;
	while(true) {
		if(cur_cls != EOC && cur_cls != FLK) {
			if(sc.find(cur_cls) == sc.end())
				sc.insert(cur_cls);
			else {
				console::write((format("Circular reference in FAT chain starting at 0x%08X.") % orig).str(), fatx_context::get()->mmi.dialog);
				if(fatx_context::get()->mmi.prog == frontend::fsck) {
					console::write(" Cut it ?", fatx_context::get()->mmi.dialog);
					if(fatx_context::get()->mmi.getanswer(true))
						write(prv_cls, EOC);
				}
				else
					console::write(" Ignoring.\n", fatx_context::get()->mmi.dialog);
				break;
			}
			if(area_ptr == 0) {
				area_ptr = clsarithm::cls2ptr(cur_cls);
				area_start = cur_cls;
			}
			area_siz += fatx_context::get()->par.clus_size;
		}
		if((prv_cls != 0 && prv_cls != cur_cls - 1) || cur_cls == EOC || cur_cls == FLK) {
			if(cur_cls != EOC && cur_cls != FLK)
				area_siz -= fatx_context::get()->par.clus_size;
			res.push_back(area(area_off, area_ptr, area_siz, area_start, prv_cls));
			area_off += area_siz;
			if(cur_cls != EOC && cur_cls != FLK)
                area_ptr = clsarithm::cls2ptr(cur_cls);
			area_siz = fatx_context::get()->par.clus_size;
			area_start = cur_cls;
		}
		if(cur_cls == EOC || cur_cls == FLK)
			break;
		cur_cls = read(prv_cls = cur_cls);
		if(lbd)
			lbd(prv_cls, cur_cls);
	}
	#if defined DEBUG && defined DBG_AREAS
		dbglog((lbd ? "Write" : "Get") + res.print())
	#endif
	return res;
}
clusptr						dskmap::		clsavail() {
	clusptr res = 0;
	if(freegaps.empty())
		gapcheck();
	for(const auto& i: freegaps.right)
		res += i.first;
	return res;
}
void						dskmap::		erase() {
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(authm);
	#endif
	freegaps.clear();
	fatx_context::get()->dev.write(clsarithm::cls2fat(fatx_context::get()->par.root_clus), string(
		(fatx_context::get()->par.clus_fat - fatx_context::get()->par.root_clus) * fatx_context::get()->par.chain_size,
		'\0'
	));
	memnext.clear();
	gapcheck();
}
void						dskmap::		gapcheck() {
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(authm);
	#endif
	#ifdef DEBUG
		dbglog((format("Calculating free gaps out of %d fat entries...\n") % fatx_context::get()->par.clus_fat).str())
	#endif
	freegaps.clear();
	mapptr_t b = 0;
	mapsiz_t s = 0;
	forfat([this, &b, &s] (const clusptr& o, const clusptr& v) -> void {
		if(v == FLK) {
			if(b != 0)
				s++;
			else {
				b = o;
				s = 1;
			}
		}
		else if(b != 0) {
			freegaps.insert(gap_t::value_type(b, s));
			b = 0;
		}
	});
	if(b != 0)
		freegaps.insert(gap_t::value_type(b, s));
	#if defined DEBUG && defined DBG_GAPS
		dbglog("Gaps:\n")
		printgaps();
	#endif
}
clusptr						dskmap::		read(const clusptr& p) {
	if(p == FLK || p == EOC) {
		console::write((format("Can't read FAT at special cluster value (0x%08X).\n") % p).str(), true);
		return 0;
	}
	if(p < 1 || p > fatx_context::get()->par.clus_fat) {
		console::write((format("Cluster pointer to FAT out of bounds (0x%08X).\n") % p).str(), true);
		return 0;
	}
	return memnext(p);
}
bool						dskmap::		write(const clusptr& p, const clusptr& v) {
	if(p == FLK || p == EOC) {
		console::write((format("Can't write FAT at special cluster value (0x%08X).\n") % p).str(), true);
		return false;
	}
	if(p < 1 || p > fatx_context::get()->par.clus_fat) {
		console::write((format("Cluster pointer to FAT out of bounds (0x%08X).\n") % p).str(), true);
		return false;
	}
	if(v != FLK && v != EOC && (v < 1 || v > fatx_context::get()->par.clus_fat)) {
		console::write((format("Cluster value to FAT out of bounds (0x%08X) for cluster 0x%08X.\n") % v % p).str(), true);
		return false;
	}
	return memnext(p, v);
}
vareas						dskmap::		alloc(const clusptr& s, const clusptr& o) {
	vareas res;
	if(s == 0)
		return vareas();
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(authm);
	#endif
	if(freegaps.empty()) {
		console::write("No space left on device, disk full.\n", true);
		return vareas();
	}
	clusptr			gap_clus = 0;
	clusptr			gap_size = 0;
	if(o != 0) {
		// we first try to allocate in continuity with o
		gap_t::left_map::iterator			prev = freegaps.left.find(o);
		if(prev != freegaps.left.end() && prev->second >= s) {
			gap_clus	= prev->first;
			gap_size	= prev->second;
		}
	}
	if(gap_clus == 0) {
		// we try to allocate at end of used cluster
		gap_t::left_map::reverse_iterator	end = freegaps.left.rbegin();
		if(end->second >= s) {
			gap_clus	= end->first;
			gap_size	= end->second;
		}
	}
	if(gap_clus == 0) {
		// we find smallest gap that fits
		gap_t::right_map::iterator			fit = freegaps.right.lower_bound(s);
		if(fit != freegaps.right.end() && fit->first == s) {
			gap_clus	= fit->second;
			gap_size	= fit->first;
		}
	}
	if(gap_clus != 0) {
		// contiguous case
		for(clusptr i = gap_clus; i < gap_clus + s; i++)
			write(i, (i == gap_clus + s - 1) ? EOC : i + 1);
		freegaps.left.erase(gap_clus);
		if(gap_size != s)
			freegaps.insert(gap_t::value_type(gap_clus + s, gap_size - s));
		res.push_back(area(
			0,
			clsarithm::cls2ptr(gap_clus),
			s * fatx_context::get()->par.clus_size,
			gap_clus,
			gap_clus + s - 1
		));
	}
	else {
		// first, evaluate the number of free clusters
		gap_size	= 0;
		for(const auto& i: freegaps.right)
			gap_size += i.first;
		if(gap_size >= s) {
			// we take gaps in decreasing size order
			gap_t::right_map::reverse_iterator gap;
			clusptr	tot_size = s;
			clusptr old_clus = 0;
			do {
				gap = freegaps.right.rbegin();
				gap_clus = gap->second;
				gap_size = gap->first;
				if(old_clus != 0)
					write(old_clus, gap_clus);
				for(clusptr i = gap_clus; i < gap_clus + min<clusptr>(gap_size, tot_size); i++)
					write(i, (i == gap_clus + min<clusptr>(gap_size, tot_size) - 1) ? EOC : i + 1);
				res.push_back(area(
					res.empty() ? 0 : res.back().offset + res.back().size,
					clsarithm::cls2ptr(gap_clus),
					min<clusptr>(gap_size, tot_size) * fatx_context::get()->par.clus_size,
					gap_clus,
					gap_clus + min<clusptr>(gap_size, tot_size) - 1
				));
				freegaps.right.erase(gap_clus);
				if(tot_size < gap_size) {
					freegaps.insert(gap_t::value_type(gap_clus + gap_size, tot_size - gap_size));
					tot_size = 0;
				}
				else
					tot_size -= gap_size;
				old_clus = gap_clus;
			} while(tot_size != 0);
		}
		else {
			console::write((format("Not enough disk space for %d cluster allocation.\n") % s).str(), true);
			return vareas();
		}
	}
	#ifdef DEBUG
		dbglog(
			gap_clus ? (
				format("*** FAT alloc: %d cluster%s starting at 0x%08X.\n") % s % (s > 1 ? "s" : "") % gap_clus
			).str() : (
				format("*** FAT alloc: failed to allocate %d clusters.\n") % s
			).str()
		);
	#endif
	return res;
}
void						dskmap::		free(const clusptr& o) {
	if(o == FLK || o == EOC)
		return;
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(authm);
	#endif
	vareas va = getareas(o, [this] (const clusptr& c, const clusptr& v) -> void { (void)v; write(c, FLK); });
	if(fatx_context::get()->mmi.prog == frontend::fsck)
		return;
	for(area i: va) {
		#ifdef DEBUG
			dbglog((format("*** FAT free:  %d cluster%s starting at 0x%08X.\n") % (i.stop - i.start + 1) % ((i.stop - i.start) > 0 ? "s" : "") % i.start).str())
		#endif
		gap_t::left_map::iterator next = freegaps.left.upper_bound(i.start);
		gap_t::left_map::iterator prev = next;
		prev--;
		if(next != freegaps.left.end() && prev->first + prev->second == i.start && i.stop + 1 == next->first) {
			// new gap is adjascent with previous and next gap
			clusptr prev_clus = prev->first;
			clusptr prev_size = prev->second;
			clusptr next_size = next->second;
			freegaps.left.erase(prev);
			freegaps.left.erase(next);
			freegaps.insert(gap_t::value_type(prev_clus, prev_size + i.stop - i.start + 1 + next_size));
		}
		else if(next != freegaps.left.end() && prev->first + prev->second == i.start) {
			// new gap is adjascent with a previous gap
			clusptr prev_clus = prev->first;
			clusptr prev_size = prev->second;
			freegaps.left.erase(prev);
			freegaps.insert(gap_t::value_type(prev_clus, prev_size + i.stop - i.start + 1));
		}
		else if(next != freegaps.left.end() && i.stop + 1 == next->first) {
			// new gap is adjascent with a next gap
			clusptr next_size = next->second;
			freegaps.left.erase(next);
			freegaps.insert(gap_t::value_type(i.start, i.stop - i.start + 1 + next_size));
		}
		else {
			// new gap is not adjascent with another gap
			freegaps.insert(gap_t::value_type(i.start, i.stop - i.start + 1));
		}
	}
}
bool						dskmap::		resize(ptr_vareas o, const clusptr& s) {
	if(!o)
		return false;
	if(o->empty()) {
		if(s == 0)
			return true;
		else
			return false;
	}
	if(s == 0) {
		free(o->first());
		return true;
	}
	bool res = true;
	if(o->nbcls() < s) {
		// we need to extend the chain
		vareas extend = alloc(s - o->nbcls(), o->last() + 1);
		res = !extend.empty();
		if(res) {
			#ifndef NO_LOCK
				authm.lock();
			#endif
			res = write(o->last(), extend.first());
			o->add(extend);
			#ifndef NO_LOCK
				authm.unlock();
			#endif
		}
	}
	else if(o->nbcls() > s) {
		// we need to reduce the chain
		#ifndef NO_LOCK
			authm.lock();
		#endif
		res = write(o->at(s), EOC);
		#ifndef NO_LOCK
			authm.unlock();
		#endif
		if(res)
			free(o->at(s + 1));
		#ifndef NO_LOCK
			authm.lock();
		#endif
		o->erase(o->in(s) + 1, o->end());
		o->back().size -= o->nbcls() - s;
		o->back().stop = o->back().start + o->back().size - 1;
		#ifndef NO_LOCK
			authm.unlock();
		#endif
	}
	return res;
}
void						dskmap::		change(const clusptr&, entry*, const clusptr&, const status_t) {
	console::write(string("Invalid call to :") + __FUNCTION__ + "\n", true);
	exit(2);
}
ptr_vareas					dskmap::		markchain(clusptr, entry*) {
	console::write(string("Invalid call to :") + __FUNCTION__ + "\n", true);
	exit(2);
}
void						dskmap::		restore(clusptr, bool) {
	console::write(string("Invalid call to :") + __FUNCTION__ + "\n", true);
	exit(2);
}
dskmap::status_t			dskmap::		status(const clusptr&) const {
	console::write(string("Invalid call to :") + __FUNCTION__ + "\n", true);
	exit(2);
	return dskmap::disk;
}
entry*						dskmap::		getentry(const clusptr&) const {
	console::write(string("Invalid call to :") + __FUNCTION__ + "\n", true);
	exit(2);
	return nullptr;
}
void						dskmap::		fatlost() {
	console::write(string("Invalid call to :") + __FUNCTION__ + "\n", true);
	exit(2);
}
void						dskmap::		fatcheck() {
	console::write(string("Invalid call to :") + __FUNCTION__ + "\n", true);
	exit(2);
}
string						dskmap::		printchain(clusptr orig) {
	string res;
	while(orig != FLK && orig != EOC) {
		res += (format("->0x%08X") % orig).str();
		orig = dskmap::read(orig);
	}
	return res;
}
#ifdef DEBUG
void						dskmap::		printgaps() const {
	for(const auto& i: freegaps.left)
		dbglog((format("%08X: %d cluster%s free\n") % i.first % i.second % (i.second > 1 ? "s": "")).str())
}
void						dskmap::		printfat() {
	console::write(string("Invalid call to :") + __FUNCTION__ + "\n", true);
	exit(2);
}
#endif

							memmap::		memmap(const fatxpar& par) : dskmap(par) {
}
							memmap::		~memmap() {
	memchain.clear();
	lost.clear();
}
clusptr						memmap::		read(const clusptr& p) {
	return (memchain.find(p) != memchain.end()) ? memchain.find(p)->second.next : (fatx_context::get()->mmi.dellost ? dskmap::read(p) : FLK);
}
void						memmap::		change(const clusptr& p, entry* e, const clusptr& n, const status_t s) {
	if(memchain.find(p) != memchain.end()) {
		if(n != FLK)
			memchain.find(p)->second.next	= n;
		memchain.find(p)->second.ent	= e;
		memchain.find(p)->second.status	= s;
	}
	else
		memchain.insert(make_pair(p, link(e, n, s)));
}
ptr_vareas					memmap::		markchain(clusptr p, entry* e) {
	ptr_vareas res;
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(authm);
	#endif
	bool dl = fatx_context::get()->mmi.dellost;
	fatx_context::get()->mmi.dellost = true;
	res = make_shared<vareas>(getareas(p, [&e, this] (const clusptr& c, const clusptr& v) -> void { (void)v; change(c, e); }));
	fatx_context::get()->mmi.dellost = dl;
	return res;
}
memmap::status_t			memmap::		status(const clusptr& p) const {
	return (memchain.find(p) == memchain.end()) ? disk : memchain.find(p)->second.status;
}
entry*						memmap::		getentry(const clusptr& p) const {
	return (memchain.find(p) == memchain.end()) ? 0 : memchain.find(p)->second.ent;
}
void						memmap::		fatlost() {
	lost.clear();
	forfat([this] (const clusptr& o, const clusptr& v) -> void {
		if(v != FLK && status(o) == disk && find_if(lost.begin(), lost.end(), [o] (const vareas& i) -> bool { return i.isin(o); }) == lost.end()) {
			vareas va = getareas(o);
			lost.erase(remove_if(lost.begin(), lost.end(), [&va] (const vareas& i) -> bool { return va.isin(i.first()); }), lost.end());
			lost.push_back(va);
		}
	});
}
void						memmap::		fatcheck() {
	if(fatx_context::get()->mmi.prog == frontend::fsck) {
		// we propose to correct each erroneous entry in fat
		for(const auto& p: memchain) {
			if(status(p.first) == modified) {
				console::write((format("Cluster number in FAT 0x%08X shall be %s instead of %s. Correct it ?")
					% p.first
					% clsarithm::clsprint(p.second.next, p.first)
					% clsarithm::clsprint(dskmap::read(p.first), p.first)
				).str(), fatx_context::get()->mmi.dialog);
				if(fatx_context::get()->mmi.getanswer(true)) {
					write(p.first, p.second.next);
					memchain.erase(p.first);
				}
			}
		}
	}
	if(fatx_context::get()->mmi.prog == frontend::fsck || fatx_context::get()->mmi.prog == frontend::unrm) {
		// we propose to rescue or to clear each chain with no reference found in fat
		for(const vareas& va: lost) {
			if(fatx_context::get()->mmi.prog == frontend::fsck) {
				console::write((format("Found unknown chain at 0x%08X (%d). Free it ?")
					% va.first()
					% (va.nbcls() * fatx_context::get()->par.clus_size)
				).str(), fatx_context::get()->mmi.dialog);
				if(fatx_context::get()->mmi.getanswer(true))
					free(va.first());
			}
			else {
				console::write((format("Found unknown chain at 0x%08X (%d). Recover in %s ?")
					% va.first()
					% (va.nbcls() * fatx_context::get()->par.clus_size)
					% fatx_context::get()->mmi.lostfound
				).str(), fatx_context::get()->mmi.dialog);
				if(fatx_context::get()->mmi.getanswer(false)) {
					if(fatx_context::get()->mmi.local) {
						ptr_entry f(new entry((format("%s%03d") % fatx_context::get()->mmi.foundfile % fatx_context::get()->mmi.filecount++).str()));
						f->cluster = va.first();
						f->size = (va.nbcls() * fatx_context::get()->par.clus_size);
						f->recover();
					}
					else {
						entry* lf = fatx_context::get()->root->find(&(fatx_context::get()->mmi.lostfound)[0]);
						if(lf == nullptr) {
							// we first have to create lost+found directory
							if(!fatx_context::get()->root->addtodir((lf = new entry(fatx_context::get()->mmi.lostfound, 0, true)))) {
								console::write((format("Unable to create directory %s.\n") % fatx_context::get()->mmi.lostfound).str(), fatx_context::get()->mmi.dialog);
								return;
							}
						}
						else {
							// we find the latest file number
							for(const entry& e: lf->childs) {
								unsigned int n;
								if(sscanf(e.name, &(string(def_fpre) + "%3d")[0], &n) == 1)
									fatx_context::get()->mmi.filecount = max<unsigned int>(fatx_context::get()->mmi.filecount, n + 1);
							}
						}
						// we create the file and put it in lost+found
						entry* f = new entry((format("%s%03d") % fatx_context::get()->mmi.foundfile % fatx_context::get()->mmi.filecount++).str(), 0);
						f->size = va.nbcls() * fatx_context::get()->par.clus_size;
						f->cluster = va.first();
						if(!lf->addtodir(f))
							console::write((format("Unable to create file %s.\n") % f->name).str(), fatx_context::get()->mmi.dialog);
					}
				}
			}
		}
	}
}
#ifdef DEBUG
void						memmap::		printfat() {
	bool empty = false;
	status_t s = marked;
	entry* ent = fatx_context::get()->root;
	clusptr l = 1;
	for(clusptr i = fatx_context::get()->par.root_clus; i < fatx_context::get()->par.clus_fat; i++) {
		if(
			empty != ((read(i) == FLK) && (dskmap::read(i) == FLK)) ||
			(!empty && (
				(s != status(i)) ||
				(status(i) != disk && ent != getentry(i))
			)) ||
			i == fatx_context::get()->par.clus_fat
		) {
			dbglog((format("%s %s%s\n")
				% (
					!empty ? (
						(s == disk) ? "LOST" :
						(s == marked) ? ((getentry(ent->cluster) == ent) ? ((ent->cluster == l) ? "OK  " : "OK->") : "*ERR") :
						(s == deleted) ? ((getentry(ent->cluster) == ent) ? ((ent->cluster == l) ? "DEL " : "DEL>") : "*BAD") :
						"?MOD"
					) : "...."
				)
				% (
					(l == (i - 1)) ?
					(format("0x%08X:            ") % l).str() :
					(format("0x%08X-0x%08X: ") % l % (i - 1)).str()
				)
				% (
					(!empty && s != disk) ?
					(format("(0x%08X: %s)") % ent->cluster % ent->path()).str() : ""
				)
			).str());
			empty = (read(i) == FLK) && (dskmap::read(i) == FLK);
			s = status(i);
			ent = (status(i) == disk) ? 0 : getentry(i);
			l = i;
		}
	}
}
#endif

							// root entry constructor
							entry::			entry() :
	cptacc(0),
	writeopened(none),
	authb("B:/"),
	authw("W:/"),
	status(valid),
	namesize(0),
	cluster(fatx_context::get()->par.root_clus),
	size(0),
	loc(0),
	parent(this),
	entbuf(),
	areas() {
	memset	(name, '\0', name_size + 1);
	flags.dir = true;
	touch();
	opendir();
	entry* idx = find(fidx);
	if(idx !=  nullptr) {
		creation	= idx->creation;
		access		= idx->access;
		update		= idx->update;
		unsigned char lab[slab];
		if(idx->data((char*)lab, true, 0, idx->size))
			fatx_context::get()->par.label(lab, idx->size);
	}
}
							// existing entry constructor
							entry::			entry(const streamptr& s, const char buf[ent_size]) :
	cptacc(0),
	writeopened(none),
	authb(),
	authw(),
	namesize(buf != 0 ? buf[0] : 0),
	flags(buf != 0 ? buf[1] : '\0'),
	cluster(buf != 0 ? endian<4>::litend(&buf[0x2C])() : 0),
	size(buf != 0 ? endian<4>::litend(&buf[0x30])() : 0),
	creation((const unsigned char*)(buf != 0 ? &buf[0x34] : "\0\0\0\0")),
	access((const unsigned char*)(buf != 0 ? &buf[0x38] : "\0\0\0\0")),
	update((const unsigned char*)(buf != 0 ? &buf[0x3C] : "\0\0\0\0")),
	loc(s),
	parent(),
	entbuf(),
	areas() {
	status = (
		// 0xFF or 0x00 on 2 firsts bytes = end of entries
		(buf == 0 || (buf[0] == EOD && buf[1] == EOD) || (buf[0] == 0 && buf[1] == 0)) ? end : (
			// invalid = invalid cluster (we scan something else than an entry) or invalid name
			(
				(cluster > fatx_context::get()->par.clus_fat) ||
				(buf[2] < ' ' || buf[2] > '~') ||
				(buf[3] != 0 && (buf[3] < ' ' || buf[3] > '~' ||
				(buf[4] != 0 && (buf[4] < ' ' || buf[4] > '~'))))
			) ? invalid : (
				// valid entry = good name size + cluster not free in FAT
				(buf[0] >= 0 && buf[0] <= name_size && ((size == 0 && cluster == 0) || (cluster != 0 && fatx_context::get()->fat->dskmap::read(cluster) != FLK))) ? valid : (
					// recoverable entry = entry with cluster not allocated to something else
					(
						(cluster == 0 && !flags.dir) || (
							cluster != 0 && fatx_context::get()->fat->dskmap::read(cluster) == FLK && (
							typeid(fatx_context::get()->fat) != typeid(memmap*) || (
								fatx_context::get()->fat->status(cluster) == memmap::deleted &&
								!fatx_context::get()->fat->getentry(cluster)->flags.dir &&
								fatx_context::get()->fat->getentry(cluster)->update.seq() < update.seq()
							))
						)
					) ? delwdata : (flags.dir ? invalid : delnodata)
				)
			)
		)
	);
	// cleaning name
	memset(name, '\0', name_size + 1);
	if(buf != 0)
		memcpy(name, &buf[2], (namesize <= name_size) ? namesize : name_size);
	if(status != end && ((namesize != deleted_size && namesize != strlen(name)) || strlen(name) == 0 || name[0] == sepdir[0]))
			status = invalid;
	if(status != end && status != invalid) {
		for(size_t i = 0; i < strlen(name); i++)
			name[i] = (name[i] == EOD) ? '\0' : ((name[i] < ' ') || name[i] > '~') ? '~' : name[i];
	}
	authw.name("W:" + path());
	authb.name("B:" + path());
}
							// new entry constructor
							entry::			entry(const string& n, const filesize& s, const bool d) :
	cptacc(0),
	writeopened(none),
	authb(),
	authw(),
	status(invalid),
	namesize(0),
	size(d ? 0 : s),
	loc(0),
	parent(),
	entbuf() {
	areas = make_shared<vareas>(fatx_context::get()->fat->alloc(d ? 1 : clsarithm::siz2cls(s)));
	cluster = ((!d && s == 0) || areas == 0) ? FLK : areas->first();
	if(cluster == 0)
		size		= 0;
	namesize		= (n.length() <= name_size) ? n.length() : name_size;
	memset			(name, '\0', name_size + 1);
	strncpy			(name, &n[0], namesize);
	authw.name("W:" + string(name));
	authb.name("B:" + string(name));
	flags.dir		= d;
	touch();
	if(d && cluster != 0)
		entry(clsarithm::cls2ptr(cluster)).write();
}
							entry::			~entry() {
	flush(false);
	entbuf.reset();
	childs.clear();
	parent = nullptr;
	areas.reset();
}

#ifdef DEBUG
string						entry::			print() const {
	return "-> entry " + ((status == end) ? "*END*\n" : (format("'%s' : %s, 0x%08X->0x%08X (%d) at %s\n        (%s, C=>%s A=>%s U=>%s, 0x%02X)\n")
		% ((status == invalid) ? "*INVALID*" : path())
		% ((status == invalid) ? "bad" : (status == valid) ? "ok" : (status == delwdata) ? "del" : "ko")
		% cluster
		% ((cluster != FLK && cluster != EOC && cluster <= fatx_context::get()->par.clus_fat) ? fatx_context::get()->fat->dskmap::read(cluster) : 0)
		% size
		% fatx_context::get()->dev.address(loc)
		% flags.print()
		% creation.print()
		% access.print()
		% update.print()
		% (unsigned int)namesize
	).str());
}
#endif
string						entry::			path() const {
	return (
		(cluster == fatx_context::get()->par.root_clus) ? string(sepdir) : (
		((parent != nullptr) ? parent->path() : (string("?") + sepdir)) + name + (flags.dir ? sepdir : "")
	));
}
void						entry::			opendir() {
	if(status == end || status == invalid || !flags.dir || cluster == FLK || cluster == EOC)
		return;
	bool marked		= false;
	bool bad		= false;
	for(clusptr clus_curr = cluster; clus_curr != EOC && clus_curr != FLK && !(marked && !fatx_context::get()->mmi.recover); clus_curr = fatx_context::get()->fat->read(clus_curr)) {
		string buf = fatx_context::get()->dev.read(clsarithm::cls2ptr(clus_curr), fatx_context::get()->par.clus_size);
		for(size_t i = 0; i < buf.size() && !(marked && !fatx_context::get()->mmi.recover); i += ent_size) {
			std::auto_ptr<entry> ent(new entry(clsarithm::cls2ptr(clus_curr) + i, &buf[i]));
			ent->parent = this;
			ent->loc = clsarithm::cls2ptr(clus_curr) + i;
			ent->authw.name("W:" + ent->path());
			ent->authb.name("B:" + ent->path());
			if(ent->status == end)
				marked = true;
			if(ent->status == invalid && !marked)
				bad = true;
			if(ent->status == end || ent->status == invalid)
				continue;
			if(marked && ent->status == valid)
				ent->status = delwdata;
			if(!fatx_context::get()->mmi.recover && ent->status != valid)
				continue;
			#ifdef DEBUG
				dbglog(ent->print())
			#endif
			if(ent->status == valid) {
				for(const entry& e: childs) {
					if(e.status == valid && strncmp(ent->name, e.name, name_size) == 0) {
						// duplicate reference case
						console::write((format("Duplicate reference in same directory %s for entry %s.") % path() % ent->name).str(), fatx_context::get()->mmi.dialog);
						if(fatx_context::get()->mmi.prog == frontend::fsck) {
							// interactive way
							if(ent->cluster != e.cluster) {
								strncat(ent->name, "~", name_size);
								console::write((format(" Create it (as %s) ?") % ent->name).str(), fatx_context::get()->mmi.dialog);
								if(fatx_context::get()->mmi.getanswer(true)) {
									ent->write();
									break;
								}
							}
							console::write(" Remove it ?", fatx_context::get()->mmi.dialog);
							if(fatx_context::get()->mmi.getanswer(true)) {
								if(ent->cluster != e.cluster)
									remfrdir(ent.get(), false);
								else {
									ent->status = delnodata;
									ent->cluster = FLK;
									ent->write();
								}
							}
							ent.reset();
							break;
						}
						if(ent->cluster != e.cluster && fatx_context::get()->mmi.prog == frontend::unrm) {
							// consider it if different from its brother
							strncat(ent->name, "~", name_size);
							console::write((format(" Reading it as %s.\n") % ent->name).str(), fatx_context::get()->mmi.dialog);
							break;
						}
						// forget it
						console::write(" Skipping.\n", fatx_context::get()->mmi.dialog);
						ent.reset();
						break;
					}
				}
				if(ent.get() == nullptr)
					continue;
			}
			if(ent->flags.dir) {
				bool del = false;
				// check if no circular reference
				for(const entry* e = this; ; e = e->parent) {
					if(e == 0 || e->loc == 0)
						break;
					if(e->status != valid)
						del = true;
					if(ent->cluster == e->cluster) {
						// circular reference case
						if(!del) {
							console::write((format("Circular reference for entry %s found in %s.") % ent->path() % path()).str(), fatx_context::get()->mmi.dialog);
							if(fatx_context::get()->mmi.prog == frontend::fsck) {
								console::write(" Remove it ?", fatx_context::get()->mmi.dialog);
								if(fatx_context::get()->mmi.getanswer(true)) {
									remfrdir(ent.get(), false);
									ent.reset();
									break;
								}
							}
							else
								console::write(" Skipping.\n", fatx_context::get()->mmi.dialog);
						}
						bad = true;
						ent.reset();
						break;
					}
				}
				if(ent.get() == nullptr)
					continue;
			}
			childs.push_back(ent);
			ent.reset();
			if(childs.back().flags.dir && childs.back().status != delnodata) {
				// go one step deep
				childs.back().opendir();
			}
		}
		if(!marked && fatx_context::get()->fat->read(clus_curr) == EOC)
			marked = true;
	}
	if(status == valid && !marked) {
		console::write((format("No end mark for directory \"%s\".") % name).str(), fatx_context::get()->mmi.dialog);
		if(fatx_context::get()->mmi.prog == frontend::fsck) {
			console::write(" Mark it ?", fatx_context::get()->mmi.dialog);
			if(fatx_context::get()->mmi.getanswer(true))
				closedir();
		}
		else
			console::write("\n", fatx_context::get()->mmi.dialog);
	}
	if(childs.empty() && bad)
		status = delnodata;
}
void						entry::			closedir() {
	if(!flags.dir)
		return;
	set<const entry*> checked;
	bool closed = false;
	clusptr j = 0;
	for(
		clusptr i = cluster;
		!closed && i != EOC && i != FLK;
		i = fatx_context::get()->fat->read(i)
	) {
		string buf = fatx_context::get()->dev.read(clsarithm::cls2ptr(i), fatx_context::get()->par.clus_size);
		for(
			j = 0;
			j < fatx_context::get()->par.clus_size;
			j += ent_size
		) {
			if((buf[j + 0] == EOD && buf[j + 1] == EOD) || (buf[j + 0] == 0 && buf[j + 1] == 0)) {
				closed = true;
				break;
			}
		}
	}
	if(!closed && j != 0 && (j - (j >> (fatx_context::get()->par.clus_size >> ent_pow) << (fatx_context::get()->par.clus_size >> ent_pow)) != 0)) {
		console::write((format("Directory %s supposed to be closed.") % path()).str(), fatx_context::get()->mmi.dialog);
		if(fatx_context::get()->mmi.prog == frontend::fsck) {
			console::write(" Close it ?", fatx_context::get()->mmi.dialog);
			if(fatx_context::get()->mmi.getanswer(true))
				entry(j).write();
		}
		else {
			console::write(" Closing.\n", fatx_context::get()->mmi.dialog);
			entry(j).write();
		}
	}
}
bool						entry::			addtodir(entry* e) {
	if(e == nullptr || !flags.dir || cluster == 0 || (e->flags.dir && e->cluster == 0))
		return false;
	#ifndef NO_LOCK
		authw.lock();
	#endif
	for(const entry& i: childs) {
		if(i.namesize == e->namesize && strncmp(i.name, e->name, i.namesize) == 0) {
			#ifndef NO_LOCK
				authw.unlock();
			#endif
			return false;
		}
	}
	streamptr end = 0;
	streamptr del = 0;
	for(clusptr i = cluster; end == 0 && i != EOC && i != FLK; i = fatx_context::get()->fat->read(i)) {
		string buf = fatx_context::get()->dev.read(clsarithm::cls2ptr(i), fatx_context::get()->par.clus_size);
		for(size_t j = 0; j < buf.size(); j += ent_size) {
			if(buf[j] == EOD && buf[j + 1] == EOD) {
				end = clsarithm::cls2ptr(i) + j;
				break;
			}
			if((unsigned char)buf[j] == deleted_size)
				del = clsarithm::cls2ptr(i) + j;
		}
	}
	if(end != 0) {
		// we have enough space in directory to add one more entry
		e->loc		= end;
		e->status	= entry::valid;
		if((e->loc + ent_size) - (((e->loc + ent_size) >> fatx_context::get()->par.clus_pow) << fatx_context::get()->par.clus_pow) != 0) {
			// we must mark the end of entries
			entry(e->loc + ent_size).write();
		}
	}
	else {
		// we search a deleted entry
		if(del != 0) {
			// we found one, and use it
			e->loc		= del;
			e->status	= entry::valid;
		}
		else {
			// we have to allocate one cluster more in the directory
			if(!areas || areas->empty()) {
				if((areas = make_shared<vareas>(fatx_context::get()->fat->getareas(cluster)))->empty()) {
					#ifndef NO_LOCK
						authw.unlock();
					#endif
					return false;
				}
			}
			if(!fatx_context::get()->fat->resize(areas, areas->nbcls() + 1)) {
				#ifndef NO_LOCK
					authw.unlock();
				#endif
				return false;
			}
			e->loc		= clsarithm::cls2ptr(areas->last());
			e->status	= entry::valid;
			// we must mark the end of entries
			entry(e->loc + ent_size).write();
		}
	}
	e->parent = this;
	childs.push_back(e);
	e->write();
	#ifndef NO_LOCK
		authw.unlock();
	#endif
	touch(false, false, true);
	return save();
}
void						entry::			remfrdir(entry* e, bool c) {
	if(e->status != valid || e->flags.lab)
		return;
	if(c) {
		for(entry& f: e->childs)
			e->remfrdir(&f);
	}
	#ifndef NO_LOCK
		authw.lock();
	#endif
	if(e->cluster != 0)
		fatx_context::get()->fat->free(e->cluster);
	e->status = delnodata;
	e->write();
	if(c)
		childs.release(find_if(childs.begin(), childs.end(), [e] (const entry& a) -> bool { return a == *e; })).release();
	#ifndef NO_LOCK
		authw.unlock();
	#endif
	touch(false, false, true);
	save();
	return;
}
entry*						entry::			find(const char* path) {
	entry*									res = this;
	char_separator<char>					sep(sepdir);
	string									src(path);
	if(src.rfind(sepdir, src.size()) != string::npos && src.rfind(sepdir, src.size()) == (src.size() - 1))
		src.erase(src.size() - 1);
	tokenizer<char_separator<char> >		dirs(src, sep);
	bool									found = true;
	for(string d: dirs) {
		#ifndef NO_LOCK
			sharable_lock<mutex> lock(res->authw);
		#endif
		found = false;
		for(entry& e: res->childs) {
			if(e.status == entry::valid && e.name == d.substr(0, name_size)) {
				found = true;
				res	= &e;
				break;
			}
		}
		if(!found && fatx_context::get()->mmi.recover) {
			for(entry& e: res->childs) {
				if(e.name == d.substr(0, name_size)) {
					found = true;
					res	= &e;
					break;
				}
			}
		}
		if(!found)
			break;
	}
	if(!found)
		res = nullptr;
	return res;
}
void						entry::			touch(bool cre, bool acc, bool upd) {
	#ifndef NO_TIME
		time_t t = std::time(nullptr);
		if(cre)
			creation(t);
		if(acc)
			access(t);
		if(upd)
			update(t);
	#else
		(void) cre;
		(void) acc;
		(void) upd;
	#endif
}
bool						entry::			write() {
	if(status == invalid || (flags.dir && cluster == 0))
		return false;
	if(loc == 0)
		return true;
	string buf(ent_size, '\0');
	if(status == end)
		memset(&buf[0], EOD, ent_size);
	else {
		touch(false, true, false);
		buf[0] = (status == delwdata || status == delnodata) ? deleted_size : strlen(name);
		flags.write(&buf[1]);
		memcpy(&buf[2], name, name_size);
		memcpy(&buf[0x2C], &endian<4>::litend(cluster)[0], 4);
		memcpy(&buf[0x30], &endian<4>::litend(size)[0], 4);
		creation.write((unsigned char*)&buf[0x34]);
		access.write((unsigned char*)&buf[0x38]);
		update.write((unsigned char*)&buf[0x3C]);
	}
	return fatx_context::get()->dev.write(loc, buf);
}
bool						entry::			save() {
	if(parent == this)
		return true;
	assert(parent != nullptr);
	#ifndef NO_LOCK
		parent->authw.lock();
	#endif
	bool res = write();
	#ifndef NO_LOCK
		parent->authw.unlock();
	#endif
	return res;
}
bool						entry::			rename(const char* n) {
	if(status != valid)
		return false;
	string nstr = string(n);
	if(nstr.empty() || flags.lab)
		return true;
	if(nstr.rfind(sepdir, nstr.size()) != string::npos) {
		assert(parent != nullptr);
		entry* newpar = fatx_context::get()->root->find(&nstr.substr(0, nstr.rfind(sepdir, nstr.size()))[0]);
		entry* oldpar = parent;
		if(newpar == nullptr)
			return false;
		if(oldpar != newpar) {
			#ifndef NO_LOCK
				oldpar->authw.lock();
			#endif
			status = delwdata;
			write();
			status = valid;
			ptr_vector<entry>::auto_type me = oldpar->childs.release(find_if(oldpar->childs.begin(), oldpar->childs.end(), [this] (const entry& a) -> bool { return &a == this; }));
			#ifndef NO_LOCK
				oldpar->authw.unlock();
			#endif
			oldpar->touch(false, false, true);
			if(!oldpar->save())
				return false;
			if(!newpar->addtodir(me.get())) {
				me.release();
				return false;
			}
			me.release();
		}
		nstr.erase(0, nstr.rfind(sepdir, nstr.size()) + 1);
	}
	memset		(name, '\0', name_size + 1);
	strncpy		(name, &nstr[0], (nstr.size() <= name_size) ? nstr.size() : name_size);
	namesize	= (status == valid) ? ((nstr.size() <= name_size) ? nstr.size() : name_size) : namesize;
	return save();
}
void						entry::			recover() {
	#ifndef ENABLE_XBOX
	if(fatx_context::get()->mmi.local) {
		if(!flags.dir) {
			ifstream fr;
			fr.open(name);
			if(fr) {
				fr.close();
				console::write("Can't open file for writing, file already exists locally.\n", true);
			}
			else {
				ofstream f(name, ios::binary | ios::trunc);
				string s(size, '\0');
				data(&s[0], true, 0, size);
				f << s;
				f.close();
			}
		}
		else
			console::write("I don't rebuild locally the directory tree.\n", true);
	}
	else
	#endif
	{
		entry* e = parent->find(name);
		if(e != nullptr && e->status == entry::valid) {
			console::write("Can't restore file. Another valid file with same name exists in this directory.\n", true);
		}
		else {
			string buf = fatx_context::get()->dev.read(clsarithm::cls2ptr(clsarithm::ptr2cls(loc)), fatx_context::get()->par.clus_size);
			streamptr mark = 0;
			for(size_t i = 0; i < buf.size(); i+= ent_size) {
				if(buf[i] == EOD) {
					mark = clsarithm::cls2ptr(clsarithm::ptr2cls(loc)) + i;
					break;
				}
			}
			if(mark != 0 && loc > mark) {
				entry none("_none", 0);
				none.loc = mark;
				none.status = delwdata;
				none.write();
				for(size_t i = mark + ent_size; i < clsarithm::cls2ptr(clsarithm::ptr2cls(loc)) + fatx_context::get()->par.clus_size; i+= ent_size)
					fatx_context::get()->dev.write(i, string(1, deleted_size));
			}
			status = entry::valid;
			write();
			fatx_context::get()->fat->getareas(cluster, [this] (const clusptr& c, const clusptr& v) -> void {
				fatx_context::get()->fat->write(c, v);
				dynamic_cast<memmap*>(fatx_context::get()->fat)->memchain.find(c)->second.status = memmap::modified;
			});
		}
	}
}
void						entry::			mark() {
	areas = fatx_context::get()->fat->markchain(cluster, this);
	clusptr cnt	= areas->nbcls();
	if(!flags.dir && cnt != clsarithm::siz2cls(size)) {
		console::write((format("Entry %s has wrong size: declared %d, found %d.")
			% path()
			% size
			% (cnt * fatx_context::get()->par.clus_size)
		).str(), fatx_context::get()->mmi.dialog);
		if(fatx_context::get()->mmi.prog == frontend::fsck) {
			console::write((format(" Possible %s data, correct it ?") % ((cnt > clsarithm::siz2cls(size)) ? "extra" : "loss of")).str(), fatx_context::get()->mmi.dialog);
			if(fatx_context::get()->mmi.getanswer(true)) {
				size = cnt * fatx_context::get()->par.clus_size;
				write();
			}
		}
		else
			console::write("\n", fatx_context::get()->mmi.dialog);
	}
}
void						entry::			guess() {
	clusptr	p = cluster;
	clusptr s = 0;
	set<entry*> old;
	#if defined DEBUG && defined DBG_GUESS
		dbglog((format("GUESS: %s 0x%08X (%d)\n") % path() % cluster % clsarithm::siz2cls(size)).str())
	#endif
	for(
		clusptr q = cluster, nb = flags.dir ? 1 : clsarithm::siz2cls(size);
		nb > 0;
		q = clsarithm::inccls(q)
	) {
		// is it free ?
		if(fatx_context::get()->fat->read(q) != FLK) {
			// check if first cluster is not available
			if(q == cluster) {
				// if it is the case, give up, it's unrecoverable
				status = delnodata;
				if(fatx_context::get()->mmi.verbose)
					console::write(path() + (flags.dir ? sepdir : (format(" (%d)") % size).str()) + " not recoverable\n");
				return;
			}
			// check if it's a previous guess, if it is the case, use it
			if(fatx_context::get()->fat->getentry(q) != this) {
				// check if cluster is occupied by a file deleted older than ours
				if(
					fatx_context::get()->fat->status(q) == memmap::deleted &&
					!fatx_context::get()->fat->getentry(q)->flags.dir &&
					fatx_context::get()->mmi.deldate &&
					fatx_context::get()->fat->getentry(q)->update.seq() < update.seq()
				) {
					// we remember it and use that cluster
					old.insert(fatx_context::get()->fat->getentry(q));
				}
				else {
					// check if it belongs to a lost chain (occupied but not marked)
					if(!fatx_context::get()->fat->getentry(q)) {
						memmap::lost_t& lost = dynamic_cast<memmap*>(fatx_context::get()->fat)->lost;
						memmap::lost_t::iterator lc = find_if(lost.begin(), lost.end(), [q] (const vareas& i) -> bool { return i.first() == q; });
						// if it's the beginning of a lost chain not exeeding the remaining size, use it
						if(lc != lost.end() && lc->nbcls() <= nb) {
							// using lost chain
							lost.erase(lc);
							// linking to lost chain
							fatx_context::get()->fat->change(p, this, q, memmap::deleted);
							// marking lost chain
							vareas va = fatx_context::get()->fat->getareas(q, [this] (const clusptr& c, const clusptr& v) -> void {
								fatx_context::get()->fat->change(c, this, v, memmap::deleted);
							});
							// changing to last cluster of lost chain
							q = va.last();
							nb -= va.nbcls();
							p = q;
							continue;
						}
					}
					// cluster is occuped, skip it
					if(s == 0)
						s = q;
					continue;
				}
			}
		}
		// q is free
		if(s != 0) {
			#if defined DEBUG && defined DBG_GUESS
				dbglog((format(" skip: 0x%08X-0x%08X [belongs to %s [%c]]\n")
					% s % (q - 1)
					% (fatx_context::get()->fat->getentry(s) == 0 ? "lost chain" : fatx_context::get()->fat->getentry(s)->path())
					% (fatx_context::get()->fat->status(s) == memmap::deleted ? 'D' : 'X')
				).str());
			#endif
			s = 0;
		}
		// we suppose that it belongs to our entry
		fatx_context::get()->fat->change(p, this, q, memmap::deleted);
		nb--;
		p = q;
	}
	fatx_context::get()->fat->change(p, this, EOC, memmap::deleted);
	#ifdef DEBUG
	dbglog((format("%s  Guessed clusters: %06d out of %06d for size %d (%s)\n")
		% ((clsarithm::siz2cls(size) != 0 && fatx_context::get()->fat->getareas(cluster).nbcls() != clsarithm::siz2cls(size)) ? "!!!" : "   ")
		% fatx_context::get()->fat->getareas(cluster).nbcls()
		% (flags.dir ? 1 : clsarithm::siz2cls(size))
		% size
		% path()
	).str());
	#endif
	// we guess older deleted things pushed by our entry
	for(entry* i: old)
		i->guess();
}
bool						entry::			analyse(const pass_t& step, const string header) {
	bool recovered	= false;
	if(step != findfile && flags.dir && status == delnodata) {
		console::write((format("Entry %s points to invalid data. Skipping.\n") % (header + name)).str(), fatx_context::get()->mmi.dialog);
		return false;
	}
	if(step == findfile && status == valid) {
		// sanity check
		if(flags.dir && cluster == FLK) {
			console::write((format("Entry %s has invalid cluster pointer.") % (header + name)).str(), fatx_context::get()->mmi.dialog);
			if(fatx_context::get()->mmi.prog == frontend::fsck) {
				console::write(" Remove it ?", fatx_context::get()->mmi.dialog);
				if(fatx_context::get()->mmi.getanswer(true)) {
					assert(parent != nullptr);
					parent->remfrdir(this, false);
				}
			}
			else
				console::write("\n", fatx_context::get()->mmi.dialog);
			return false;
		}
		// we mark valid entries
		if(fatx_context::get()->mmi.verbose)
			console::write(header + name + (flags.dir ? sepdir : (format(" (%d)") % size).str()) + "\n");
		mark();
	}
	if(step == finddel && (status == delwdata || status == delnodata)) {
		if(fatx_context::get()->mmi.verbose) {
			console::write(header + name + (flags.dir ? sepdir : (format(" (%d)") % size).str())
				+ " " + (status == delwdata ? "deleted" : "not recoverable") + "\n"
			);
		}
		// we guess recoverable entries
		if(status == delwdata)
			guess();
	}
	if(step == tryrecov && status == delwdata && !flags.dir) {
		// we recover file sub-entries
		console::write(header + name + (format(" (%d)")	% size).str() + " recover ?");
		if(fatx_context::get()->mmi.getanswer()) {
			recovered = true;
			recover();
		}
	}
	if(flags.dir) {
		for(entry& ent: childs) {
			// we go one step deeper
			recovered = ent.analyse(step, header + name + sepdir) || recovered;
		}
	}
	if(recovered && flags.dir && status != valid && !fatx_context::get()->mmi.local) {
		console::write((format("Recovering parent directory %s.\n") % (header + name)).str(), fatx_context::get()->mmi.dialog);
		recover();
	}
	return recovered;
}
bool						entry::			resize(const filesize s) {
	#ifdef DEBUG
		dbglog((format("RESIZE: %s size:%d->%d\n") % path() % size % s).str())
	#endif
	if(flags.dir)
		return false;
	if(!writeable()) {
		#ifdef DEBUG
			dbglog((format("**> writing a file not opened for write (%s)") % path()).str())
		#endif
		return false;
	}
	if(s == size)
		return true;
	if(s == 0) {
		areas.reset();
		fatx_context::get()->fat->free(cluster);
		cluster = 0;
		size = s;
	}
	else if(size == 0) {
		vareas v = fatx_context::get()->fat->alloc(clsarithm::siz2cls(s));
		if(v.empty())
			return false;
		cluster = v.first();
		size = s;
		areas = make_shared<vareas>(v.sub(size));
	}
	if(s != size) {
		if(!fatx_context::get()->fat->resize(areas, clsarithm::siz2cls(s)))
			return false;
		size = s;
		areas = make_shared<vareas>(areas->sub(size));
	}
	return save();
}
bool						entry::			data(char* buf, bool r, filesize offset, filesize s) {
	s			= (s == 0) ? (r ? size - offset : size) : (r ? min<filesize>(s, size - offset) : s);
	#ifdef DEBUG
		dbglog((format("DATA: %s(%d) [%c:0x%08X(%d)]\n") % path() % size % (r ? 'R' : 'W') % offset % s).str())
	#endif
	if(flags.dir || (!r && !writeable()))
		return false;
	if(!r && (size == 0 || (offset + s) > size)) {
		if(!resize(offset + s)) {
			#ifdef DEBUG
				dbglog((format("DATA:%s resize failed.\n") % path()).str())
			#endif
			return false;
		}
	}
	if(size != 0) {
		if(!areas || areas->empty()) {
			areas.reset(new vareas(fatx_context::get()->fat->getareas(cluster).sub(size)));
			if(areas->empty())
				return false;
		}
		for(const area& i: areas->sub(s, offset)) {
			if(r)
				memcpy(buf + i.offset - offset, &fatx_context::get()->dev.read(i.pointer, i.size)[0], i.size);
			else {
				if(!fatx_context::get()->dev.write(i.pointer, string(buf + i.offset - offset, i.size)))
					return false;
			}
		}
	}
	bool res = true;
	if(!r) {
		touch(false, false, true);
		res = save();
	}
	return res;
}
size_t						entry::			bufread(char* buf, filesize offset, filesize s) {
	s = min<filesize>(size, offset + s) - offset;
	if(s == 0)
		return 0;
	#ifndef NO_LOCK
		scoped_lock<mutex> xlock(authb);
	#endif
	if(entbuf && (offset < entbuf->offset || (entbuf->offset + entbuf->size() - 1) < (offset + s - 1))) {
		if(!flush(false)) {
			#ifdef DEBUG
				dbglog((format("**> flush buffer failed (%s)") % path()).str())
			#endif
			return 0;
		}
		entbuf.reset();
	}
	if(!entbuf) {
		entbuf.reset(new buffer(offset, size - offset));
		if(entbuf->size() < s || !data(&(*entbuf.get())[0], true, entbuf->offset, entbuf->size())) {
			#ifdef DEBUG
				dbglog((format("**> alloc buffer or read operation failed (%s: 0x%08X %d)") % path() % offset % s).str())
			#endif
			return 0;
		}
		#ifdef DEBUG
			dbglog((format("--> new read buffer (%s at 0x%08X: %d)\n") % path() %  entbuf.get() % entbuf->size()).str())
		#endif
	}
	#ifndef NO_LOCK
		sharable_lock<mutex> slock(boost::move(xlock));
	#endif
	memcpy(buf, &(*entbuf)[offset - entbuf->offset], s);
	#ifdef DEBUG
		dbglog((format("--> read buffer (%s at 0x%08X(%d): %d(%d))\n") % path() %  entbuf.get() % entbuf->size() % (offset - entbuf->offset) % min<filesize>(s, entbuf->size())).str())
	#endif
	#if defined DEBUG && defined DBGBUFDMP
		(*entbuf)(offset - entbuf->offset);
	#endif
	return s;
}
size_t						entry::			bufwrite(const char* buf, filesize offset, filesize s) {
	if(!writeable()) {
		#ifdef DEBUG
			dbglog((format("**> writing a file not opened for write (%s)") % path()).str())
		#endif
		return false;
	}
	#ifndef NO_LOCK
		scoped_lock<mutex> lock(authb);
	#endif
	if(size < offset + s && !resize(offset + s)) {
		#ifdef DEBUG
			dbglog((format("**> file resize failed (%s: 0x%08X %d)") % path() % offset % s).str())
		#endif
		return 0;
	}
	bool res = true;
	if(entbuf) {
		if(entbuf->offset + entbuf->size() == offset) {
			entbuf->enlarge(entbuf->size() + s);
			if(entbuf->offset + entbuf->size() < offset + s) {
				res = flush(false);
				entbuf.reset();
			}
		}
		else {
			res = flush(false);
			entbuf.reset();
		}
		if(!res) {
			#ifdef DEBUG
				dbglog((format("**> flush buffer failed (%s)") % path()).str())
			#endif
			return 0;
		}
	}
	if(!entbuf) {
		entbuf.reset(new buffer(offset, s));
		if(!entbuf || entbuf->size() < s) {
			#ifdef DEBUG
				dbglog((format("**> alloc buffer failed (%s: 0x%08X %d)") % path() % offset % s).str())
			#endif
			return 0;
		}
	}
	memcpy(&(*entbuf)[offset - entbuf->offset], buf, s);
	entbuf->touched = true;
	#ifdef DEBUG
		dbglog((format("--> write buffer (%s at 0x%08X(%d): %d(%d))\n") % path() %  entbuf.get() % entbuf->size() % (offset - entbuf->offset) % min<filesize>(s, entbuf->size())).str())
	#endif
	#if defined DEBUG && defined DBGBUFDMP
		(*entbuf)(offset - entbuf->offset);
	#endif
	return s;
}
bool						entry::			flush(bool l) {
	if(flags.dir)
		return true;
	bool res = true;
	#ifndef NO_LOCK
		if(l)
			authb.lock();
	#else
		(void) l;
	#endif
	if(entbuf) {
		#ifdef DEBUG
		if(entbuf->touched)
			dbglog((format("<-> flush buffer (%s at 0x%08X: %d)\n") % path() % entbuf.get() % entbuf->size()).str())
		else
			dbglog((format("<-> no need to flush buffer (%s at 0x%08X: %d)\n") % path() % entbuf.get() % entbuf->size()).str())
		#endif
		if(entbuf->touched) {
			if(writeable()) {
				res = data(&(*entbuf.get())[0], false, entbuf->offset, entbuf->size());
				if(res)
					entbuf.reset();
			}
			else
				res = false;
		}
	}
	#ifndef NO_LOCK
		if(l)
			authb.unlock();
	#endif
	return res;
}
void						entry::			open(bool w) {
	if(!flags.dir) {
		#ifndef NO_LOCK
			// res = authw.timed_lock(posix_time::second_clock::local_time() + posix_time::seconds(timeout));
			if(w)
				authw.lock();
			else
				authw.lock_sharable();
		#endif
		if(writeopened != yes)
			writeopened = w ? yes : no;
		if(cptacc++ == 0 && cluster != 0 && size != 0)
			areas = make_shared<vareas>(fatx_context::get()->fat->getareas(cluster).sub(size));
	}
}
void						entry::			close(bool w) {
	if(!flags.dir) {
		#ifndef NO_LOCK
			if(w)
				authw.unlock();
			else
				authw.unlock_sharable();
		#endif
		if(writeable()) {
			flush(true);
			if(writeopened == yes)
				writeopened = no;
		}
		if(--cptacc == 0) {
			areas.reset();
			#ifndef NO_LOCK
				authb.lock();
			#endif
			if(entbuf)
				entbuf.reset();
			#ifndef NO_LOCK
				authb.unlock();
			#endif
		}
	}
}
bool						entry::			operator == (const entry& b) const {
	return
		loc == b.loc &&
		cluster == b.cluster &&
		size == b.size &&
		parent == b.parent &&
		strncmp(name, b.name, namesize) == 0
	;
}
#if !defined NO_FUSE && !defined NO_SPLICE
struct fuse_bufvec*			entry::			getbufvec(filesize offset, filesize s) {
	ptr_vareas va = fatx_context::get()->fat->getareas(cluster).sub(s, offset);
	if(va->empty())
		return 0;
	struct fuse_bufvec *bufv2 = 0;
	struct fuse_bufvec *bufv = (struct fuse_bufvec*)malloc(sizeof(struct fuse_bufvec));
	if(!bufv)
		return 0;
	bufv->idx	= 0;
	bufv->off	= offset % fatx_context::get()->par.clus_size;
	size_t count = 0;
	for(const area& i: *va) {
		if(count > 0) {
			if(!(bufv2 = (struct fuse_bufvec*)realloc(bufv, sizeof(struct fuse_bufvec) + count * sizeof(struct fuse_buf)))) {
				free(bufv);
				return 0;
			}
			bufv = bufv2;
		}
		bufv->buf[count].size	= i.size;
		bufv->buf[count].flags	= (fuse_buf_flags)(FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK);
		bufv->buf[count].fd		= fileno(fatx_context::get()->dev.getfd());
		bufv->buf[count].mem	= 0;
		bufv->buf[count].pos	= i.pointer;
		count++;
	}
	return bufv;
}
#endif

#ifndef NO_FUSE
static int					fatx_getattr	(const char* path, struct stat* st) {
	#ifdef DEBUG
		dbglog((format("GETATTR: %s\n") % path).str())
	#endif
	entry* f = fatx_context::get()->root->find(path);
	if(f == nullptr || f->status == entry::invalid || (f->flags.dir && f->cluster == 0))
		return -ENOENT;
	memset(st, 0, sizeof(struct stat));
	st->st_dev		= fatx_context::get()->par.par_id;
	st->st_mode		= f->flags() & (fatx_context::get()->mmi.mask | S_IFDIR | S_IFREG);
	st->st_nlink	= f->childs.size() + 1;
	st->st_size		= f->flags.dir ? f->childs.size() : f->size;
	st->st_blksize	= fatx_context::get()->par.clus_size;
	st->st_blocks	= clsarithm::siz2cls(f->size) * fatx_context::get()->par.clus_size / blksize;
	st->st_atime	= f->access();
	st->st_mtime	= f->update();
	st->st_ctime	= f->creation();
	st->st_uid		= fatx_context::get()->mmi.uid;
	st->st_gid		= fatx_context::get()->mmi.gid;
	#ifdef DEBUG
		dbglog(f->print())
	#endif
	return 0;
}
static int					fatx_truncate	(const char* path, off_t size) {
	#ifdef DEBUG
		dbglog((format("TRUNCATE: %s\n") % path).str())
	#endif
	entry* f = fatx_context::get()->root->find(path);
	if(f == nullptr || f->status == entry::invalid)
		return -ENOENT;
	if(!fatx_context::get()->mmi.writeable())
		return -EROFS;
	if(f->flags.ro)
		return -EACCES;
	f->resize(size);
	return 0;
}
static int					fatx_open		(const char* path, struct fuse_file_info* fi) {
	#ifdef DEBUG
		dbglog((format("OPEN: %s [%c]\n") % path % (fi->flags & (S_IWUSR | S_IWGRP | S_IWOTH) ? 'w' : 'r' )).str())
	#endif
	entry* f = fatx_context::get()->root->find(path);
	if(f == nullptr || f->status == entry::invalid)
		return -ENOENT;
	if((fi->flags & (O_WRONLY | O_RDWR)) != 0 && !fatx_context::get()->mmi.writeable())
		return -EROFS;
	if((fi->flags & (O_WRONLY | O_RDWR)) != 0 && f->flags.ro)
		return -EPERM;
	f->open((fi->flags & (O_WRONLY | O_RDWR)) != 0);
	fi->fh = (uint64_t)f;
	return 0;
}
static int					fatx_flush		(const char* path, struct fuse_file_info* fi) {
	#ifdef DEBUG
		dbglog((format("FLUSH: %s\n") % path).str())
	#endif
	entry* f((entry*)(fi->fh));
	if(f == nullptr)
		f = fatx_context::get()->root->find(path);
	return f->flush() ? 0 : -ENOSPC;
}
static int					fatx_close		(const char* path, struct fuse_file_info* fi) {
	#ifdef DEBUG
		dbglog((format("CLOSE: %s\n") % path).str())
	#endif
	entry* f((entry*)(fi->fh));
	if(f == nullptr)
		f = fatx_context::get()->root->find(path);
	f->close((fi->flags & (O_WRONLY | O_RDWR)) != 0);
	fi->fh = (uint64_t)0;
	return 0;
}
static int					fatx_read		(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
	#ifdef DEBUG
		dbglog((format("READ: %s\n") % path).str())
	#endif
	entry* f((entry*)(fi->fh));
	if(f == nullptr)
		f = fatx_context::get()->root->find(path);
	return f->bufread(buf, offset, size);
}
static int					fatx_write		(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
	#ifdef DEBUG
		dbglog((format("WRITE: %s\n") % path).str())
	#endif
	entry* f((entry*)(fi->fh));
	if(f == nullptr)
		f = fatx_context::get()->root->find(path);
	if(!fatx_context::get()->mmi.writeable())
		return -EROFS;
	if(f->flags.ro)
		return -EACCES;
	return f->bufwrite(buf, offset, size);
}
static int					fatx_chmod		(const char* path, mode_t mode) {
	#ifdef DEBUG
		dbglog((format("CHMOD: %s\n") % path).str())
	#endif
	entry* f = fatx_context::get()->root->find(path);
	if(f == nullptr || f->status == entry::invalid)
		return -ENOENT;
	if(!fatx_context::get()->mmi.writeable())
		return -EROFS;
	f->flags(mode);
	f->save();
	return 0;
}
static int					fatx_chown		(const char* path, uid_t uid, gid_t gid) {
	(void) uid;
	(void) gid;
	#ifdef DEBUG
		dbglog((format("CHOWN: %s\n") % path).str())
	#endif
	entry* f = fatx_context::get()->root->find(path);
	if(f == nullptr || f->status == entry::invalid)
		return -ENOENT;
	if(!fatx_context::get()->mmi.writeable())
		return -EROFS;
	// nothing done
	return 0;
}
static int					fatx_readdir	(const char* path, void* buf, fuse_fill_dir_t ff, off_t offset, struct fuse_file_info* fi) {
	#ifdef DEBUG
		dbglog((format("READDIR: %s\n") % path).str())
	#endif
	(void)offset;
	entry* f((entry*)(fi->fh));
	if(f == nullptr)
		f = fatx_context::get()->root->find(path);
	struct stat st;
	int res;
	if((res = fatx_getattr(&f->path()[0], &st)) != 0)
		return res;
	ff(buf, ".", &st, 0);
	if((res = fatx_getattr(&f->parent->path()[0], &st)) != 0)
		return res;
	ff(buf, "..", &st, 0);
	for(const entry& i: f->childs) {
		if(i.status == entry::valid || (fatx_context::get()->mmi.recover && i.status == entry::delwdata)) {
			if((res = fatx_getattr(&i.path()[0], &st)) != 0)
				return res;
			if(ff(buf, i.name, &st, 0) != 0)
				return -EBADF;
			#ifdef DEBUG
				dbglog((format(" %s\n") % i.path()).str())
			#endif
		}
	}
	return 0;
}
static int					fatx_create		(const char* path, mode_t mode) {
	#ifdef DEBUG
		dbglog((format("CREATE: %s\n") % path).str())
	#endif
	if(!fatx_context::get()->mmi.writeable())
		return -EACCES;
	string p(path);
	size_t l = p.find_last_of(sepdir);
	if(l == string::npos || l == p.length() - 1)
		return -ENOENT;
	if(p.length() - ( l + 1 ) > name_size)
		return -ENAMETOOLONG;
	if(fatx_context::get()->root->find(path) != nullptr)
		return -EEXIST;
	entry* n = new entry(p.substr(l + 1), 0, ((mode & S_IFREG) == 0));
	if(n->flags.dir && n->cluster == 0) {
		delete n;
		return -ENOSPC;
	}
	entry* s = fatx_context::get()->root->find(&(p.substr(0, l))[0]);
	if(s == nullptr) {
		delete n;
		return -ENOENT;
	}
	if(!s->addtodir(n)) {
		delete n;
		return -EBADF;
	}
	return fatx_chmod(path, mode);
}
static int					fatx_creope		(const char* path, mode_t mode, struct fuse_file_info* fi) {
	#ifdef DEBUG
		dbglog((format("CREOPE: %s\n") % path).str())
	#endif
	return fatx_create(path, mode) | fatx_open(path, fi);
}
static int					fatx_remove		(const char* path) {
	#ifdef DEBUG
		dbglog((format("REMOVE: %s\n") % path).str())
	#endif
	entry* f = fatx_context::get()->root->find(path);
	if(f == nullptr || f->status == entry::invalid)
		return -ENOENT;
	if(!fatx_context::get()->mmi.writeable())
		return -EROFS;
	if(f->flags.ro)
		return -EACCES;
	if(f == fatx_context::get()->root)
		return -EBUSY;
	if(f->flags.dir && f->childs.size() != 0)
		return -ENOTEMPTY;
	assert(f->parent != nullptr);
	f->parent->remfrdir(f);
	return 0;
}
static int					fatx_rename		(const char* from, const char* to) {
	#ifdef DEBUG
		dbglog((format("RENAME: %s to %s\n") % from % to).str())
	#endif
	entry* f = fatx_context::get()->root->find(from);
	if(f == nullptr || f->status == entry::invalid)
		return -ENOENT;
	if(!fatx_context::get()->mmi.writeable())
		return -EROFS;
	if(f->flags.ro)
		return -EACCES;
	if(!f->rename(to))
		return -ENOSPC;
	return 0;
}
static int					fatx_utimens	(const char* path, const struct timespec tv[2]) {
	#ifdef DEBUG
		dbglog((format("UTIMENS: %s\n") % path).str())
	#endif
	entry* f = fatx_context::get()->root->find(path);
	if(f == nullptr || f->status == entry::invalid)
		return -ENOENT;
	if(!fatx_context::get()->mmi.writeable())
		return -EROFS;
	if(f->flags.ro)
		return -EACCES;
	f->access(tv[0].tv_sec);
	f->update(tv[1].tv_sec);
	f->save();
	return 0;
}
static int					fatx_statfs		(const char* path, struct statvfs* sfs) {
	#ifdef DEBUG
		dbglog((format("STATFS: %s\n") % path).str())
	#endif
	(void)path;
	sfs->f_bsize	= 1;									// blksize;
	sfs->f_frsize	= 1;									// fatx_context::get()->par.clus_size / sfs->f_bsize;
	sfs->f_blocks	= (fatx_context::get()->par.clus_fat - fatx_context::get()->par.root_clus) * fatx_context::get()->par.clus_size;
	sfs->f_bfree	= fatx_context::get()->fat->clsavail() * fatx_context::get()->par.clus_size;
	sfs->f_bavail	= fatx_context::get()->fat->clsavail() * fatx_context::get()->par.clus_size;
	sfs->f_files	= 0;
	sfs->f_ffree	= 0;
	sfs->f_favail	= 0;
	sfs->f_fsid		= fatx_context::get()->par.par_id;
	sfs->f_flag		= ST_NOSUID | (fatx_context::get()->mmi.writeable() ? 0 : ST_RDONLY);
	sfs->f_namemax	= name_size;
	#ifdef DEBUG
		dbglog((format(
			" f_bsize:\t%d\n"
			" f_frsize:\t%d\n"
			" f_blocks:\t%d\n"
			" f_bfree:\t%d\n"
			" f_bavail:\t%d\n"
			" f_files:\t%d\n"
			" f_ffree:\t%d\n"
			" f_favail:\t%d\n"
			" f_fsid:\t0x%08X\n"
			" f_flag:\t0x%08X\n"
			" f_namemax:\t%d\n"
		)
			% sfs->f_bsize
			% sfs->f_frsize
			% sfs->f_blocks
			% sfs->f_bfree
			% sfs->f_bavail
			% sfs->f_files
			% sfs->f_ffree
			% sfs->f_favail
			% sfs->f_fsid
			% sfs->f_flag
			% sfs->f_namemax
		).str());
	#endif
	return 0;
}
static void*				fatx_init		(struct fuse_conn_info* fci) {
	#ifdef DEBUG
		dbglog("INIT\n")
	#endif
	fci->want = FUSE_CAP_BIG_WRITES | FUSE_CAP_DONT_MASK
	#ifndef NO_SPLICE
		| FUSE_CAP_SPLICE_READ | FUSE_CAP_SPLICE_WRITE | FUSE_CAP_SPLICE_MOVE
	#endif
	;
	return 0;
}
static void					fatx_destroy	(void*) {
	#ifdef DEBUG
		dbglog("DESTROY\n")
	#endif
	fatx_context::get()->destroy();
}
#ifndef NO_SPLICE
static int					fatx_read_buf	(const char* path, struct fuse_bufvec** bufp, size_t size, off_t offset, struct fuse_file_info* fi) {
	#ifdef DEBUG
		dbglog((format("READBUF: %s\n") % path).str())
	#endif
	entry* f((entry*)(fi->fh));
	if(f == nullptr)
		f = fatx_context::get()->root->find(path);
	*bufp = f->getbufvec(offset, size);
	return (*bufp) ? size : 0;
}
static int					fatx_write_buf	(const char* path, struct fuse_bufvec* buf, off_t offset, struct fuse_file_info* fi) {
	#ifdef DEBUG
		dbglog((format("WRITEBUF: %s\n") % path).str())
	#endif
	entry* f((entry*)(fi->fh));
	if(f == nullptr)
		f = fatx_context::get()->root->find(path);
	if(!fatx_context::get()->mmi.writeable())
		return -EROFS;
	if(f->flags.ro)
		return -EACCES;
	#if !defined NO_FUSE_CALL && !defined NO_WRITE
		return fuse_buf_copy(f->getbufvec(offset, fuse_buf_size(buf)), buf, (fuse_buf_copy_flags)0);
	#else
		(void) buf;
		(void) offset;
		return 0;
	#endif
}
#endif
static struct fuse_operations				fatx_ops;
#endif

#ifdef ENABLE_XBOX
int							fatx			(int argc, char* argv[]) {
#else
int							main			(int argc, char* argv[]) {
#endif
	int err = 0;
	frontend mmi(argc, argv);
	if(!mmi.setup())
		return code_usage;
	if(mmi.prog != frontend::unknown && mmi.prog != frontend::label)
		console::write("Analysing filesystem, please wait.\n");
	if(mmi.prog != frontend::unknown) {
		fatx_context::set(new fatx_context(mmi));
		if(!fatx_context::get()->setup())
			return code_operr;
	}
	if(mmi.prog == frontend::fsck || (mmi.prog != frontend::unknown && mmi.recover)) {
		console::write("Finding all files and directories.\n");
		fatx_context::get()->root->analyse(entry::findfile);
		#if defined DEBUG && defined DBG_FAT
			fatx_context::get()->fat->printfat();
		#endif
	}
	if(mmi.prog != frontend::unknown && mmi.recover) {
		console::write("Finding all deleted files and directories.\n");
		fatx_context::get()->fat->fatlost();
		fatx_context::get()->root->analyse(entry::finddel);
		#if defined DEBUG && defined DBG_FAT
			fatx_context::get()->fat->printfat();
		#endif
	}
	if(mmi.prog != frontend::mkfs && !mmi.script.empty()) {
		fatx_context::get()->fat->gapcheck();
		mmi.parser();
	}
	if(mmi.prog == frontend::unrm) {
		console::write("Trying to recover deleted files and directories.\n");
		if(!mmi.local)
			fatx_context::get()->fat->gapcheck();
		fatx_context::get()->root->analyse(entry::tryrecov);
	}
	if(!mmi.nofat && (mmi.prog == frontend::fsck || mmi.prog == frontend::unrm)) {
		console::write("Checking FAT consistency.\n");
		fatx_context::get()->fat->fatlost();
		fatx_context::get()->fat->fatcheck();
	}
	if(mmi.prog == frontend::fuse) {
		int fuse_argc = 0;
		char* fuse_argv[max_fuse_args];
		fuse_argv[fuse_argc++] = &mmi.progname[0];
		fuse_argv[fuse_argc++] = &mmi.mount[0];
		if(!mmi.writeable()) {
			fuse_argv[fuse_argc++] = &(*(new string("-o")))[0];
			fuse_argv[fuse_argc++] = &(*(new string("ro")))[0];
		}
		if(mmi.fuse_debug)
			fuse_argv[fuse_argc++] = &(*(new string("-d")))[0];
		if(mmi.fuse_foregrd)
			fuse_argv[fuse_argc++] = &(*(new string("-f")))[0];
		#ifndef NO_LOCK
		if(mmi.fuse_singlethr)
		#endif
			fuse_argv[fuse_argc++] = &(*(new string("-s")))[0];
		if(!mmi.fuse_option.empty()) {
			fuse_argv[fuse_argc++] = &(*(new string("-o")))[0];
			fuse_argv[fuse_argc++] = &mmi.fuse_option[0];
		}
		for(const string& i: mmi.unkopt) {
			if(fuse_argc < (max_fuse_args - 1))
				fuse_argv[fuse_argc++] = &(*(new string(i)))[0];
			else {
				console::write("Too many arguments for fuse. Ignoring last arguments.\n", true);
				break;
			}
		}
		fuse_argv[fuse_argc] = 0;
		if(!mmi.recover)
			fatx_context::get()->fat->gapcheck();
		#ifndef NO_FUSE
			memset(&fatx_ops, 0, sizeof(fatx_ops));
			fatx_ops.getattr		= fatx_getattr;
			fatx_ops.utimens		= fatx_utimens;
			fatx_ops.chmod			= fatx_chmod;
			fatx_ops.chown			= fatx_chown;
			fatx_ops.create			= fatx_creope;
			fatx_ops.open			= fatx_open;
			fatx_ops.read			= fatx_read;
			fatx_ops.write			= fatx_write;
			fatx_ops.flush			= fatx_flush;
			fatx_ops.release		= fatx_close;
			fatx_ops.truncate		= fatx_truncate;
			fatx_ops.unlink			= fatx_remove;
			fatx_ops.mkdir			= fatx_create;
			fatx_ops.opendir		= fatx_open;
			fatx_ops.readdir		= fatx_readdir;
			fatx_ops.releasedir		= fatx_close;
			fatx_ops.rmdir			= fatx_remove;
			fatx_ops.rename			= fatx_rename;
			fatx_ops.statfs			= fatx_statfs;
			fatx_ops.init			= fatx_init;
			fatx_ops.destroy		= fatx_destroy;
			#ifndef NO_SPLICE
				fatx_ops.read_buf		= fatx_read_buf;
				fatx_ops.write_buf		= fatx_write_buf;
			#endif
			#ifdef DEBUG
				string s;
				for(int i = 0; i < fuse_argc; i++)
					s += string(" ") + fuse_argv[i];
				dbglog((format("Fuse called with%s\n:") % s).str())
			#endif
			console::write("Ready.\n");
			#ifdef NO_FUSE_CALL
				(void) fuse_argv;
			#else
				err = fuse_main(fuse_argc, fuse_argv, &fatx_ops, nullptr);
			#endif
			#ifdef DEBUG
				dbglog((format("Fuse returned: %d\n") % err).str())
			#endif
		#else
			(void) fuse_argv;
		#endif
	}
	bool answ;
	if(mmi.prog == frontend::mkfs) {
		console::write((format("Are you sure you want to erase all data in %s ?") % mmi.input).str());
		if((answ = mmi.getanswer(false))) {
			console::write("Creating new FATX filesystem");
			fatx_context::get()->par.write();
			console::write(".");
			fatx_context::get()->fat->erase();
			console::write(".");
			fatx_context::get()->dev.write(fatx_context::get()->par.root_start, string(fatx_context::get()->par.clus_size, '\0'));
			console::write(".");
			fatx_context::get()->root = new entry("", 0, true);
			fatx_context::get()->root->parent = fatx_context::get()->root;
			fatx_context::get()->root->status = entry::valid;
			console::write("done.\n");
			console::write((format("FATX filesystem created with %d clusters.\n") % fatx_context::get()->par.clus_fat).str());
			if(mmi.volname.empty())
				mmi.volname = def_label;
		}
	}
	if((mmi.prog == frontend::mkfs && answ) || (mmi.prog == frontend::label && !mmi.volname.empty())) {
		if(mmi.prog == frontend::label)
			fatx_context::get()->fat->gapcheck();
		fatx_context::get()->par.par_label = fatx_context::get()->mmi.volname;
		unsigned char lab[slab];
		entry* idx = fatx_context::get()->root->find(fidx);
		filesize s = fatx_context::get()->par.label(lab);
		if(idx == nullptr) {
			fatx_context::get()->root->addtodir(new entry(fidx, 0, false));
			idx = fatx_context::get()->root->find(fidx);
			idx->flags.lab = true;
			idx->save();
		}
		if(idx->resize(s) && idx->data((char*)lab, false, 0, s))
			console::write((format("Volume name has been changed to %s\n") % fatx_context::get()->par.par_label).str());
		else
			console::write("Unable to change volume name.\n");
	}
	if(mmi.prog == frontend::fsck) {
		if(fatx_context::get()->par.par_label.empty())
			console::write("Warning: volume has no name.\n");
		if(mmi.verbose) {
			console::write((format(
					"Volume name:\t%s\n"
					"Clusters size:\t%d\n"
					"Total clusters:\t%d\n"
					"Clusters free:\t%d\n"
				)
				% (fatx_context::get()->par.par_label.empty() ? "none" : fatx_context::get()->par.par_label)
				% fatx_context::get()->par.clus_size
				% fatx_context::get()->par.clus_fat
				% fatx_context::get()->fat->clsavail()
			).str());
		}
	}
	if(mmi.prog == frontend::label && mmi.volname.empty())
		console::write((fatx_context::get()->par.par_label.empty() ? "No volume name." : fatx_context::get()->par.par_label) + "\n");
	if(mmi.verbose) {
		if(fatx_context::get()->dev.modified())
			console::write("Changes have been made.\n");
		else
			console::write("No change has been made.\n");
	}
	if(err != 0)
		err = mmi.prog != frontend::fsck ? code_noerr : !fatx_context::get()->dev.modified() ? code_noerr : fatx_context::get()->mmi.allyes ? code_corrd : code_ncorr;
	delete fatx_context::get();
	#ifdef DEBUG
		dbglog((format("<= ENDC: %s\n") % mmi.name()).str())
	#endif
	return err;
}
