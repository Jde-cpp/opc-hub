# What build-deb.sh and ../demo/build-demo.sh share, sourced by each:  the .so's a staged product dir carries beside its exes.
# The caller sets `patchelf` and defines die().

owner(){ #path - the package that owns it, or nothing
	local pkg
	pkg=$(dpkg -S "$1" 2>/dev/null | head -1 | cut -d: -f1) || true
	[ -n "$pkg" ] || pkg=$(dpkg -S "$(realpath "$1")" 2>/dev/null | head -1 | cut -d: -f1) || true
	echo "$pkg"
}

#Every .so an exe or module resolves outside the system dirs comes along (the deps tree: fmt, abseil, boost, jsonnet), and of
#the system ones LLVM's libc++/libc++abi (and libunwind, should libc++abi ever link it) plus libxml2 and the ICU pair it
#links.  libxml2 is the one system library whose soname moves between the releases we claim to support - Ubuntu 24.04
#builds against libxml2.so.2, 26.04 ships only libxml2.so.16 (package `libxml2-16`, and `libicu74` -> `libicu78`) - so a
#package built on the older release could not install on the newer one, and forcing it past apt left Jde.Opc.Server
#unable to load at all (reviews/install-issues.md #28).  Carrying it costs ~8.6 MB compressed, almost all of it
#libicudata, and is what makes "24.04 or later" true.  What to copy comes from ldd on
#the *originals* - their build RUNPATHs still reach the deps tree, and ldd lists the whole closure as the loader would
#resolve it for that exe, a bundled lib's own deps included.  Then every staged file gets RUNPATH=$ORIGIN, and ldd on
#the staged copies must resolve everything beside the exe or on the system - the target's view.
bundle(){ #dir original...
	local dir=$1 name path; shift
	for f in "$@"; do
		if ldd "$f" | grep -q 'not found'; then die "$f: $(ldd "$f" | grep 'not found' | tr -s ' \t' ' ' | tr '\n' ';') - the build tree does not resolve its own deps"; fi
	done
	while read -r name path; do
		[ -n "$path" ] && [ ! -e "$dir/$name" ] || continue
		case "$path" in
			/lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*)
				case "$name" in libc++.so.*|libc++abi.so.*|libunwind.so.*|libxml2.so.*|libicuuc.so.*|libicudata.so.*) ;; *) continue;; esac;;
		esac
		install -m 644 "$(realpath "$path")" "$dir/$name"
	done < <(for f in "$@"; do ldd "$f" | awk '/ => \//{print $1, $3}'; done | sort -u)
	for f in "$dir"/*; do "$patchelf" --set-rpath '$ORIGIN' "$f"; done
	for f in "$dir"/*; do
		if ldd "$f" | grep -q 'not found'; then die "$f: $(ldd "$f" | grep 'not found' | tr -s ' \t' ' ' | tr '\n' ';')"; fi
	done
}

#The Depends: a system may lack ride along in a tarball:  apt installs the .deb's, but nothing installs a tarball's - its
#install needs no package, and getting one does.  liburing2 is optional, absent from a 24.04 that never had the .deb, and
#without it no product loads (reviews/install-issues.md #59).  They go beside the exes that load them, where RUNPATH=$ORIGIN
#finds them first; the toolchain floor and openssl stay the system's - its glibc is paired with its own loader, and apt updates
#its openssl.  Both lists are named, not read from dpkg's Priority:  that is the build machine's status file, and the release
#runner's records libc6 and the rest as optional, which bundled all of them (#64).  A package in neither list fails the
#build until it is placed.
tarballPkgs=' liburing2 '
systemPkgs=' libc6 libgcc-s1 libstdc++6 libssl3t64 libzstd1 liblzma5 zlib1g '
carry(){ #stage dir... - each dir a bundled product dir under the stage
	local stage=$1 dir name path pkg bundled=' '; shift
	for dir in "$@"; do
		while read -r name path; do
			case "$path" in "$stage"/*) continue;; esac
			pkg=$(owner "$path")
			case "$systemPkgs" in *" $pkg "*) continue;; esac
			case "$tarballPkgs" in *" $pkg "*) ;; *) die "$path (${pkg:-no package}) is in neither package-lib.sh's tarballPkgs nor its systemPkgs - name it in one: a tarball carries the first, a system must have the second";; esac
			install -m 644 "$(realpath "$path")" "$dir/$name"
			"$patchelf" --set-rpath '$ORIGIN' "$dir/$name" #a bundled one's own deps resolve beside it too - RUNPATH is not inherited
			echo "tarball: ${dir#"$stage"/}/$name from $pkg, which a system may lack"
			bundled="$bundled$pkg "
		done < <(for f in "$dir"/*; do ldd "$f" | awk '/ => \//{print $1, $3}'; done | sort -u)
	done
	for pkg in $tarballPkgs; do
		case "$bundled" in *" $pkg "*) ;; *) die "tarballPkgs names $pkg, which nothing staged loads - drop it";; esac
	done
}