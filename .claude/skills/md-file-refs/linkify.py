#!/usr/bin/env python3
"""Turn `path:line` refs in a markdown doc into VS Code-clickable relative links.

	`libs/db/src/DBException.cpp:30/37`  ->  [`libs/db/src/DBException.cpp:30/37`](../opc-hub/libs/db/src/DBException.cpp#L30)

Paths are resolved against `git ls-files` of the repo, so bare basenames
(`Entry.cpp`) and partial paths (`sqlServer/access_user_insert_login.sql`) work.
Ambiguous or unknown names are left untouched and reported.

Usage:
	linkify.py DOC [DOC...]            rewrite in place
	linkify.py --check DOC [DOC...]    validate only, exit 1 on a broken link
	linkify.py --dry-run DOC           report what would change
	linkify.py --repo PATH DOC         repo to resolve against (default: the doc's
	                                   git toplevel, else $JDE_DIR)
"""
import argparse, collections, os, re, subprocess, sys

EXT = r"cpp|cc|c|h|hpp|sql|jsonnet|proto|json|cmake|ts|py|sh|yml|yaml"
SPEC = r"(?::\d+(?:[-,/]\d+)*)"
# one pass, left to right, so a continuation ref binds to the file that precedes it
REF_RE = re.compile( r"`([A-Za-z0-9_./+-]+\.(?:" + EXT + r"))(" + SPEC + r"?)`"
	r"|`(" + SPEC + r")`" )
LINK_RE = re.compile(r"\[[^\]\n]*\]\([^)\n]*\)")
FENCE_RE = re.compile(r"^\s*(```|~~~)")
REL_LINK_RE = re.compile(r"\]\(([^)\s#]+)(?:#L(\d+))?\)")


def repo_root( doc, override )->str:
	if override:
		return os.path.abspath( override )
	r = subprocess.run( ["git","-C",os.path.dirname(os.path.abspath(doc)) or ".","rev-parse","--show-toplevel"],
		capture_output=True, text=True )
	if r.returncode==0:
		return r.stdout.strip()
	env = os.environ.get("JDE_DIR") or os.environ.get("JDE_BASH")
	if not env:
		sys.exit( f"{doc}: not in a git repo and $JDE_DIR is unset - pass --repo" )
	return os.path.abspath( env )


class Index:
	def __init__( self, repo ):
		self.repo = repo
		out = subprocess.run( ["git","-C",repo,"ls-files"], capture_output=True, text=True )
		if out.returncode!=0:
			sys.exit( f"{repo}: git ls-files failed - {out.stderr.strip()}" )
		self.files = out.stdout.split()
		self.fset = set( self.files )
		self.by_base = collections.defaultdict( list )
		for f in self.files:
			self.by_base[f.rsplit("/",1)[-1]].append( f )
		self._lines, self._short = {}, {}

	def resolve( self, p )->list:
		"""-> [path] when unique, [] when unknown, [a,b,...] when ambiguous."""
		if p in self.fset:
			return [p]
		c = [f for f in self.files if f.endswith("/"+p)]
		return c or self.by_base.get( p.rsplit("/",1)[-1], [] )

	def short( self, f )->str:
		"""Shortest trailing segments that name f unambiguously in the repo.

		`include/jde/db/DBException.h` -> `DBException.h`, but the two
		`access_user_insert_key.sql` twins keep `mysql/` / `sqlServer/`."""
		if f not in self._short:
			parts = f.split( "/" )
			label = parts[-1]
			for k in range( 1, len(parts)+1 ):
				label = "/".join( parts[-k:] )
				if len( self.resolve(label) )==1:
					break
			# a twin of the same stem in a sibling dir (config/sql/{mysql,sqlServer,sqlite})
			# means the dir is the distinguishing part - keep it even though the name is unique
			if "/" not in label and len(parts)>2:
				stem, gp = parts[-1].rsplit(".",1)[0], "/".join( parts[:-2] )
				twin = any( o!=f and o.startswith(gp+"/") and o.rsplit("/",1)[0]!="/".join(parts[:-1])
					and o.rsplit("/",1)[-1].rsplit(".",1)[0]==stem for o in self.files )
				if twin:
					label = "/".join( parts[-2:] )
			self._short[f] = label
		return self._short[f]

	def nlines( self, f )->int:
		if f not in self._lines:
			with open( os.path.join(self.repo,f), errors="ignore" ) as fh:
				self._lines[f] = sum( 1 for _ in fh )
		return self._lines[f]


LABELLED_RE = re.compile( r"^\[`([A-Za-z0-9_./+-]+\.(?:" + EXT + r"))(" + SPEC + r"?)`\]\(([^)\s]+?)(#L\d+)?\)$" )

def relabel( link, idx, prefix )->str:
	"""Shorten the label of an existing link: `include/jde/db/DBException.h:37` -> `DBException.h:37`.

	Only when the label's path and the link target agree, so hand-written labels are left alone."""
	m = LABELLED_RE.match( link )
	if not m:
		return link
	label, spec, target, frag = m.group(1), m.group(2), m.group(3), m.group(4) or ""
	if not target.startswith( prefix ):
		return link
	path = target[len(prefix):]
	if path not in idx.fset or idx.resolve( label )!=[path]:
		return link
	return f"[`{idx.short(path)}{spec}`]({target}{frag})"


def linkify( doc, idx, prefix )->tuple:
	"""-> (new_text, added, relabelled, problems)"""
	added, relabelled, problems = 0, 0, []
	out, fence = [], False
	for lineno, line in enumerate( open(doc).read().split("\n"), 1 ):
		if FENCE_RE.match( line ):
			fence = not fence
		if fence:
			out.append( line ); continue

		# mask existing links so a rerun can't nest them; shorten their labels in passing
		holes = []
		def hole( m ):
			nonlocal relabelled
			short = relabel( m.group(0), idx, prefix )
			relabelled += short!=m.group(0)
			holes.append( short )
			return f"\x00{len(holes)-1}\x00"
		line = LINK_RE.sub( hole, line )

		last = {"f": None}
		def anchor( tgt, spec ):
			n = int( re.split(r"[-,/]", spec.lstrip(":"))[0] )
			if n > idx.nlines( tgt ):
				problems.append( (lineno, f"{tgt}:{n} past EOF ({idx.nlines(tgt)} lines)") )
			return f"#L{n}"
		def on_ref( m ):
			nonlocal added
			if m.group(3):								# bare `:12-15` - inherits the file to its left
				if not last["f"]:
					problems.append( (lineno, f"{m.group(3)}: no file ref precedes it") )
					return m.group(0)
				added += 1
				return f"[`{m.group(3)}`]({prefix}{last['f']}{anchor(last['f'],m.group(3))})"
			p, spec = m.group(1), m.group(2)
			c = idx.resolve( p )
			if len(c)!=1:
				problems.append( (lineno, f"{p}: " + ("ambiguous " + ", ".join(c[:4]) if c else "not in repo")) )
				return m.group(0)
			last["f"] = c[0]
			added += 1
			return f"[`{idx.short(c[0])}{spec}`]({prefix}{c[0]}{anchor(c[0],spec) if spec else ''})"
		line = REF_RE.sub( on_ref, line )

		line = re.sub( r"\x00(\d+)\x00", lambda m: holes[int(m.group(1))], line )
		out.append( line )
	return "\n".join(out), added, relabelled, problems


def check( doc, idx, prefix )->list:
	"""Validate links already in the doc."""
	bad, n = [], 0
	base = os.path.dirname( os.path.abspath(doc) )
	for lineno, line in enumerate( open(doc), 1 ):
		for m in REL_LINK_RE.finditer( line ):
			t = m.group(1)
			if "://" in t or t.startswith("#"):
				continue
			n += 1
			p = os.path.normpath( os.path.join(base,t) )
			if not os.path.isfile( p ):
				bad.append( (lineno, f"{t}: missing") ); continue
			if m.group(2):
				with open( p, errors="ignore" ) as fh:
					total = sum( 1 for _ in fh )
				if int(m.group(2)) > total:
					bad.append( (lineno, f"{t}#L{m.group(2)}: past EOF ({total} lines)") )
	return bad, n


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument( "docs", nargs="+" )
	ap.add_argument( "--repo" )
	ap.add_argument( "--check", action="store_true" )
	ap.add_argument( "--dry-run", action="store_true" )
	a = ap.parse_args()

	rc = 0
	for doc in a.docs:
		if not os.path.isfile( doc ):
			print( f"{doc}: no such file" ); rc = 1; continue
		idx = Index( repo_root(doc, a.repo) )
		rel = os.path.relpath( idx.repo, os.path.dirname(os.path.abspath(doc)) )
		prefix = "" if rel=="." else rel.replace(os.sep,"/") + "/"

		if a.check:
			bad, n = check( doc, idx, prefix )
			print( f"{doc}: {n} links, {len(bad)} broken" )
			for lineno, msg in bad:
				print( f"  line {lineno}: {msg}" )
			rc |= 1 if bad else 0
			continue

		text, added, relabelled, problems = linkify( doc, idx, prefix )
		changed = text!=open(doc).read()
		what = f"{added} links added, {relabelled} labels shortened"
		if a.dry_run:
			print( f"{doc}: would be {what}" if changed else f"{doc}: no change" )
		else:
			if changed:
				open( doc, "w" ).write( text )
			print( f"{doc}: {what} (prefix {prefix or './'})" )
		for lineno, msg in problems:
			print( f"  line {lineno}: {msg}" )
	sys.exit( rc )


if __name__=="__main__":
	main()
