"""Bench client for an Open Protocol controller (run-services skill, *Open Protocol bench controller*;
docs/open-protocol/plan.md Phase 10b):  the hand checks the driver cannot make of itself, a mode each, speaking the
protocol raw and reading it by the specification's tables, never through the codec it checks.  Every frame the
controller sends is printed, written to <out>/<label>.log, and kept as a file a frame, up to --per of each MID and
revision, under the names libs/opc/openProtocol/tests/fixtures takes.

	identify HOST PORT            MID 0001 stepping down from --revision, and MID 0002 field by field
	ladder HOST PORT              each request the driver makes from the revision it starts at, the refusals on the way down
	first HOST PORT               what follows each subscription's MID 0005 with nothing changed at the controller
	results HOST PORT             subscribed as the driver is, each MID 0061 field by field, its bytes past ASCII in hex
	walk HOST PORT                MID 0064 from the latest result back, each MID 0065's id, time and station
	hold HOST PORT                results while the acknowledgement of the first is withheld, then what its acknowledgement frees
	alarms HOST PORT              MID 0070, and each MID 0071, 0074 and 0076 that follows
	clock HOST PORT               MID 0080, and the controller's time against this box's
	link HOST PORT                link level's numbers:  a request acknowledged, the same request again, and with --break one out of order
	ask HOST PORT MID [REV] [DATA]  any request, and whatever answers it
	frames LOG                    the frames a gateway's log holds (/gateway/drivers/openProtocol/log), as files

A header goes as the driver's encoder writes it, every field digits;  --spaces writes the unused ones blank, as the
specification's examples do."""
import argparse, datetime, pathlib, re, socket, sys, time

SUBSCRIBES = { 14: (17, "the selected parameter set"), 34: (37, "the job info"), 51: (54, "the VIN"), 60: (63, "the results"), 70: (73, "the alarms") }
CEILINGS = { 1: 6, 14: 3, 34: 6, 51: 2, 60: 7, 64: 7, 70: 3, 80: 1 }#the revision the driver starts each request at, the codec's last.
ACKS = { 15: 16, 35: 36, 52: 53, 61: 62, 71: 72, 74: 75, 76: 77 }#a push and the MID that acknowledges it at application level.
REPLIES = { 1: 2, 64: 65, 80: 81, 9999: 9999 }#a request and the MID that answers it, MID 0005 for the rest.
STEPS_DOWN = ( 74, 76, 97 )#MID 0004's codes for a revision the controller lacks.
TIME = 19#YYYY-MM-DD:HH:MM:SS

def layout( mid, r ):
	"""A MID's data field at revision `r` as the specification's tables list it:  ( parameter id or None, name, width )."""
	if mid==2:
		y = [ (1, "cell id", 4), (2, "channel id", 2), (3, "controller name", 25) ]
		if r>=2: y += [ (4, "supplier code", 3) ]
		if r>=3: y += [ (5, "open protocol version", 19), (6, "controller software version", 19), (7, "tool software version", 19) ]
		if r>=4: y += [ (8, "RBU type", 24), (9, "controller serial number", 10) ]
		if r>=5: y += [ (10, "system type", 3), (11, "system subtype", 3) ]
		if r>=6: y += [ (12, "sequence number support", 1), (13, "linking support", 1), (14, "station id", 10), (15, "station name", 25), (16, "client id", 1) ]
		return y
	if mid==4: return [ (None, "MID", 4), (None, "code", 2 if r==1 else 3) ]
	if mid in ( 5, 9997 ): return [ (None, "MID", 4) ]
	if mid==9998: return [ (None, "MID", 4), (None, "code", 4) ]
	if mid==15:
		if r==1: return [ (None, "parameter set", 3), (None, "last change", TIME) ]
		y = [ (1, "parameter set", 3), (2, "name", 25), (3, "last change", TIME), (4, "rotation direction", 1), (5, "batch size", 2), (6, "torque min", 6), (7, "torque max", 6),
			(8, "torque final target", 6), (9, "angle min", 5), (10, "angle max", 5), (11, "angle final target", 5), (12, "first target", 6), (13, "start final angle", 6) ]
		if r>=3: y += [ (14, "selected identifier number", 4), (15, "joint id", 25) ]
		return y
	if mid==35:
		y = [ (1, "job", 2 if r==1 else 4), (2, "job status", 1), (3, "batch mode", 1), (4, "batch size", 4), (5, "batch counter", 4), (6, "time", TIME) ]
		if r>=3: y += [ (7, "current step", 3), (8, "total steps", 3), (9, "step type", 2) ]
		if r>=4: y += [ (10, "tightening status", 2) ]
		if r>=5: y += [ (11, "sequence number", 5), (12, "VIN", 25), (13, "identifier part 2", 25), (14, "identifier part 3", 25), (15, "identifier part 4", 25) ]
		if r>=6: y += [ (16, "joint id", 25) ]
		return y
	if mid==52:
		if r==1: return [ (None, "VIN", 25) ]
		return [ (1, "VIN", 25), (2, "identifier part 2", 25), (3, "identifier part 3", 25), (4, "identifier part 4", 25) ]
	if mid==61:
		y = [ (1, "cell id", 4), (2, "channel id", 2), (3, "controller name", 25), (4, "VIN", 25) ]
		if r==1:
			return y + [ (5, "job", 2), (6, "parameter set", 3), (7, "batch size", 4), (8, "batch counter", 4), (9, "status", 1), (10, "torque status", 1), (11, "angle status", 1),
				(12, "torque min", 6), (13, "torque max", 6), (14, "torque final target", 6), (15, "torque", 6), (16, "angle min", 5), (17, "angle max", 5), (18, "angle final target", 5),
				(19, "angle", 5), (20, "time", TIME), (21, "parameter set last change", TIME), (22, "batch status", 1), (23, "tightening id", 10) ]
		y += [ (5, "job", 4), (6, "parameter set", 3), (7, "strategy", 2), (8, "strategy options", 5), (9, "batch size", 4), (10, "batch counter", 4), (11, "status", 1),
			(12, "batch status", 1), (13, "torque status", 1), (14, "angle status", 1), (15, "rundown angle status", 1), (16, "current monitoring status", 1), (17, "self tap status", 1),
			(18, "prevail torque monitoring status", 1), (19, "prevail torque compensate status", 1), (20, "tightening error status", 10), (21, "torque min", 6), (22, "torque max", 6),
			(23, "torque final target", 6), (24, "torque", 6), (25, "angle min", 5), (26, "angle max", 5), (27, "angle final target", 5), (28, "angle", 5), (29, "rundown angle min", 5),
			(30, "rundown angle max", 5), (31, "rundown angle", 5), (32, "current monitoring min", 3), (33, "current monitoring max", 3), (34, "current monitoring value", 3),
			(35, "self tap min", 6), (36, "self tap max", 6), (37, "self tap torque", 6), (38, "prevail torque monitoring min", 6), (39, "prevail torque monitoring max", 6),
			(40, "prevail torque", 6), (41, "tightening id", 10), (42, "job sequence number", 5), (43, "sync tightening id", 5), (44, "tool serial number", 14), (45, "time", TIME),
			(46, "parameter set last change", TIME) ]
		if r>=3: y += [ (47, "parameter set name", 25), (48, "torque unit", 1), (49, "result type", 2) ]
		if r>=4: y += [ (50, "identifier part 2", 25), (51, "identifier part 3", 25), (52, "identifier part 4", 25) ]
		if r>=5: y += [ (53, "customer error code", 4) ]
		if r>=6: y += [ (54, "prevail torque compensate value", 6), (55, "tightening error status 2", 10) ]
		if r>=7: y += [ (56, "compensated angle", 7), (57, "final angle decimal", 7) ]
		return y
	if mid==65:
		y = [ (1, "tightening id", 10), (2, "VIN", 25) ]
		if r==1:
			return y + [ (3, "parameter set", 3), (4, "batch counter", 4), (5, "status", 1), (6, "torque status", 1), (7, "angle status", 1), (8, "torque", 6), (9, "angle", 5),
				(10, "time", TIME), (11, "batch status", 1) ]
		y += [ (3, "job", 4), (4, "parameter set", 3), (5, "strategy", 2), (6, "strategy options", 5), (7, "batch size", 4), (8, "batch counter", 4), (9, "status", 1),
			(10, "batch status", 1), (11, "torque status", 1), (12, "angle status", 1), (13, "rundown angle status", 1), (14, "current monitoring status", 1), (15, "self tap status", 1),
			(16, "prevail torque monitoring status", 1), (17, "prevail torque compensate status", 1), (18, "tightening error status", 10), (19, "torque", 6), (20, "angle", 5),
			(21, "rundown angle", 5), (22, "current monitoring value", 3), (23, "self tap torque", 6), (24, "prevail torque", 6), (25, "job sequence number", 5),
			(26, "sync tightening id", 5), (27, "tool serial number", 14), (28, "time", TIME) ]
		if r>=3: y += [ (29, "torque unit", 1), (30, "result type", 2) ]
		if r>=4: y += [ (31, "identifier part 2", 25), (32, "identifier part 3", 25), (33, "identifier part 4", 25) ]
		if r>=5: y += [ (34, "customer error code", 4) ]
		if r>=6: y += [ (35, "prevail torque compensate value", 6), (36, "tightening error status 2", 10) ]
		if r>=7: y += [ (37, "station id", 10), (38, "station name", 25) ]
		return y
	if mid==71:
		y = [ (1, "error code", 4 if r==1 else 5), (2, "controller ready", 1), (3, "tool ready", 1), (4, "time", TIME) ]
		if r>=3: y += [ (5, "tool health", 1), (6, "alarm text", 50) ]
		return y
	if mid==74: return [ (None, "error code", 4 if r==1 else 5) ]
	if mid==76:
		y = [ (1, "alarm status", 1), (2, "error code", 4 if r==1 else 5), (3, "controller ready", 1), (4, "tool ready", 1), (5, "time", TIME) ]
		if r>=3: y += [ (6, "tool health", 1) ]
		return y
	if mid==81: return [ (None, "time", TIME) ]
	return None

class Frame:
	"""A frame without its NUL, and its header's fields:  a digit field of spaces is its default."""
	def __init__( self, raw ):
		self.raw = raw
		number = lambda at, to, dflt: int( raw[at:to].strip() or dflt ) if ( raw[at:to].strip() or b"0" ).isdigit() else dflt
		self.mid = number( 4, 8, 0 )
		self.rev = number( 8, 11, 1 ) or 1
		self.no_ack = raw[11:12]==b"1"
		self.seq = number( 16, 18, 0 )
		self.data = raw[20:]
	def fields( self ):
		"""( the fields read by the MID's table as name -> bytes, in the table's order;  what stopped the read, or None )."""
		rows = layout( self.mid, self.rev )
		if self.mid==4 and len( self.data )==6:
			rows = layout( 4, 1 )#a code of two digits whatever the header's revision, as the simulator writes one.
		if rows is None:
			return {}, None
		y, at = {}, 0
		for id, name, width in rows:
			if id is not None:
				if self.data[at:at+2]!=b"%02d" % id:
					return y, f"byte {21+at}:  parameter {id:02d}, {name}, expected, found {self.data[at:at+2]!r}"
				at += 2
			if len( self.data )<at+width:
				return y, f"byte {21+at}:  {name} is {width} bytes and {len( self.data )-at} are left"
			y[name] = self.data[at:at+width]
			at += width
		return y, f"{len( self.data )-at} bytes past the revision's last field:  {self.data[at:]!r}" if at<len( self.data ) else None
	def number( self, name ):
		value = self.fields()[0].get( name, b"" ).strip()
		return int( value ) if value.isdigit() else None
	def text( self ):
		return self.raw.decode( "latin-1" )

def show( value ):
	"""A field's bytes for a line of the log, with every reading of the ones past ASCII."""
	y = repr( value.decode( "latin-1" ) )
	if any( b>=0x80 for b in value ):
		y += f"   bytes {value.rstrip().hex( ' ' )}:"
		for codec in ( "latin-1", "cp1252", "cp437", "cp850", "utf-8" ):
			try: y += f"  {codec} {value.rstrip().decode( codec )!r}"
			except UnicodeDecodeError: y += f"  not {codec}"
	return y

class Capture:
	"""The log, and the frames kept as files:  at most `per` of a MID and revision, none twice."""
	def __init__( self, out, label, per, log=True ):
		self.out, self.label, self.per = pathlib.Path( out ), re.sub( r"[^A-Za-z0-9.]+", "-", label ).strip( "-" ), per
		self.out.mkdir( parents=True, exist_ok=True )
		self.log = open( self.out/f"{self.label}.log", "a", encoding="utf-8" ) if log else None
		self.start = time.monotonic()
		if log: self.say( f"# {datetime.datetime.now().isoformat( timespec='seconds' )}  {' '.join( sys.argv[1:] )}" )
	def say( self, line ):
		print( line, flush=True )
		self.log.write( line+"\n" )
		self.log.flush()
	def stamp( self ):
		return f"{time.monotonic()-self.start:8.3f}"
	def keep( self, frame ):
		kept = sorted( self.out.glob( f"{self.label}-mid{frame.mid:04d}-r{frame.rev}-*.frame" ) )
		if len( kept )>=self.per or any( f.read_bytes()==frame.raw+b"\0" for f in kept ):
			return
		n = 1
		while ( path := self.out/f"{self.label}-mid{frame.mid:04d}-r{frame.rev}-{n}.frame" ).exists(): n += 1
		path.write_bytes( frame.raw+b"\0" )

class Closed( Exception ): pass

class Bench:
	"""A connection to the controller:  its frames by their lengths, link level's numbers where MID 0002 at revision 6 says
	the controller numbers its messages, and each push acknowledged as the driver's session acknowledges it."""
	def __init__( self, args ):
		self.args = args
		self.capture = Capture( args.out, args.label or f"{args.host}-{args.port}", args.per )
		self.sock = socket.create_connection( (args.host, args.port), timeout=args.timeout )
		self.sock.setsockopt( socket.IPPROTO_TCP, socket.TCP_NODELAY, 1 )
		self.buf = b""
		self.numbered = False
		self.number = 1#the next message out's.
		self.expect = 1#the next message in's.
		self.acked = 0
		self.hold = set()#the MIDs not acknowledged.
		self.pushes = []#every frame in that no request waited for, with the time it came.
		self.last = time.monotonic()
	def say( self, line ): self.capture.say( line )
	def write( self, mid, rev=1, data=b"", seq=0, note="" ):
		head = b"%04d%04d%03d" % ( 20+len( data ), mid, rev )
		if self.args.spaces: head += b"     " + ( b"%02d" % seq if seq else b"  " ) + b"  "
		else: head += b"00101%02d00" % seq
		self.say( f"{self.capture.stamp()} -> MID {mid:04d} rev {rev}{f' seq {seq}' if seq else ''}{'  '+note if note else ''}:  {( head+data ).decode( 'latin-1' )!r}" )
		self.sock.sendall( head+data+b"\0" )
		self.last = time.monotonic()
	def send( self, mid, rev=1, data=b"", note="" ):
		"""A request, under link level's next number where the link is numbered."""
		seq = self.number if self.numbered else 0
		if seq: self.number = self.number%99+1
		self.write( mid, rev, data, seq, note )
	def acknowledge( self, frame ):
		"""MID 9997 for a numbered message, carrying the number after its own, or the message's own acknowledgement."""
		if self.numbered and frame.seq:
			self.write( 9997, 1, b"%04d" % frame.mid, self.expect )
		elif frame.mid in ACKS:
			self.write( ACKS[frame.mid] )
	def cut( self ):
		"""The buffer's first frame, None until it is whole.  Bytes that begin no frame are dropped through the next NUL."""
		while len( self.buf )>=4:
			if self.buf[:4].isdigit() and int( self.buf[:4] )>=20:
				length = int( self.buf[:4] )
				if len( self.buf )<length+1:
					return None
				raw, nul, self.buf = self.buf[:length], self.buf[length:length+1], self.buf[length+1:]
				if nul!=b"\0":
					self.say( f"{self.capture.stamp()} !! byte {nul!r} where the length ends, not a NUL" )
				return Frame( raw )
			end = self.buf.find( b"\0" )
			if end<0:
				return None
			self.say( f"{self.capture.stamp()} !! {self.buf[:end+1]!r} begins no frame:  dropped" )
			self.buf = self.buf[end+1:]
		return None
	def read( self, timeout ):
		"""The next frame, logged, kept and acknowledged, or None where none comes in time."""
		deadline = time.monotonic()+timeout
		while ( frame := self.cut() ) is None:
			left = deadline-time.monotonic()
			if left<=0:
				return None
			self.sock.settimeout( left )
			try: chunk = self.sock.recv( 4096 )
			except socket.timeout: return None
			if not chunk:
				raise Closed()
			self.buf += chunk
		self.last = time.monotonic()
		frame.at = time.monotonic()
		self.say( f"{self.capture.stamp()} <- MID {frame.mid:04d} rev {frame.rev}{f' seq {frame.seq}' if frame.seq else ''}:  {frame.text()!r}" )
		self.capture.keep( frame )
		if frame.mid in ( 9997, 9998 ):
			return frame
		if self.numbered and frame.seq:
			if frame.seq==self.expect:
				if frame.mid in self.hold:
					return frame#not acknowledged:  the number stays the one expected, and a retransmission is the same message again.
				self.acked, self.expect = frame.seq, frame.seq%99+1
			elif frame.seq==self.acked:
				self.say( f"{self.capture.stamp()} !! sequence number {frame.seq} again:  a retransmission, acknowledged again" )
			else:
				self.say( f"{self.capture.stamp()} !! sequence number {frame.seq} where {self.expect} was expected:  not acknowledged" )
				return frame
		if frame.mid not in self.hold:
			self.acknowledge( frame )
		return frame
	def until( self, is_it, timeout=None ):
		"""Frames until `is_it` holds of one, which is the return;  the rest are pushes.  None at the timeout."""
		deadline = time.monotonic()+( timeout or self.args.timeout )
		while ( left := deadline-time.monotonic() )>0:
			frame = self.read( left )
			if frame is None:
				return None
			if is_it( frame ):
				return frame
			if frame.mid not in ( 9997, 9998 ):
				self.pushes.append( frame )
		return None
	def ask( self, mid, rev=None, data=b"" ):
		"""A request from `rev`, a revision down on each MID 0004 that says the controller lacks it:  ( the revision it was
		answered at, the answer, [( revision refused, code )] ).  The answer is the MID 0004 where it was refused for good."""
		rev, refusals = rev or CEILINGS.get( mid, 1 ), []
		want = REPLIES.get( mid, 5 )
		while True:
			self.send( mid, rev, data )
			answer = self.until( lambda f: f.mid==want and ( want!=5 or f.number( "MID" ) in ( mid, None ) ) or f.mid==4 and f.number( "MID" ) in ( mid, None ) )
			if answer is None:
				raise SystemExit( f"MID {mid:04d} at revision {rev} had no answer within {self.args.timeout} s." )
			if answer.mid!=4:
				return rev, answer, refusals
			code = answer.number( "code" )
			refusals.append( (rev, code) )
			if code not in STEPS_DOWN or rev==1:
				return rev, answer, refusals
			rev -= 1
	def start( self, rev=None ):
		"""MID 0001 and the MID 0002 that answers it, which opens the numbering where it says so."""
		rev, identity, refusals = self.ask( 1, rev or self.args.revision )
		if identity.mid==4:
			raise SystemExit( f"MID 0001 was refused:  code {identity.number( 'code' )}." )
		self.numbered = identity.rev>=6 and identity.number( "sequence number support" )==1
		down = "".join( f", revision {r} refused with code {code}" for r, code in refusals )
		self.say( f"# MID 0002 at revision {identity.rev} to MID 0001 at {rev}{down};  acknowledged at {'link' if self.numbered else 'application'} level." )
		return identity
	def pump( self, seconds, each=None ):
		"""Frames for `seconds`, a keep-alive after ten of silence:  a controller drops a link silent for fifteen."""
		end = time.monotonic()+seconds
		while ( left := end-time.monotonic() )>0:
			if time.monotonic()-self.last>=10:
				self.send( 9999, note="keep-alive" )
			frame = self.read( min(left, 1) )
			if frame is not None and frame.mid not in ( 9997, 9998, 9999 ):
				self.pushes.append( frame )
				if each: each( frame )
	def stop( self ):
		try:
			self.send( 3 )
			self.until( lambda f: f.mid in ( 4, 5 ), 3 )
		except ( Closed, OSError ): pass
		self.sock.close()
	def table( self, frame ):
		fields, stopped = frame.fields()
		for name, value in fields.items():
			self.say( f"#   {name:34} {show( value )}" )
		if stopped:
			self.say( f"#   !! {stopped}" )

def identify( b ):
	b.table( b.start() )

def ladder( b ):
	b.start()
	for mid in ( 60, 14, 34, 51, 70, 64, 80 ):
		rev, answer, refusals = b.ask( mid, data=b"0000000000" if mid==64 else b"" )
		down = "".join( f"{r} refused with code {code}, " for r, code in refusals if not ( answer.mid==4 and r==rev ) )
		b.say( f"# MID {mid:04d}:  {down}" + ( f"refused at {rev} with code {answer.number( 'code' )}." if answer.mid==4 else f"answered at {rev} by MID {answer.mid:04d} at revision {answer.rev}." ) )
		if mid in SUBSCRIBES and answer.mid==5:
			b.ask( SUBSCRIBES[mid][0], 1 )

def first( b ):
	b.start()
	for mid, (_, what) in SUBSCRIBES.items():
		if mid==60:
			continue
		before = len( b.pushes )
		rev, answer, _ = b.ask( mid )
		if answer.mid==4:
			b.say( f"# MID {mid:04d}, {what}:  refused with code {answer.number( 'code' )}." )
			continue
		b.pump( b.args.wait )
		came = [ f"MID {f.mid:04d} at revision {f.rev} {( f.at-answer.at )*1000:.0f} ms after the MID 0005" for f in b.pushes[before:] ]
		b.say( f"# MID {mid:04d} at {rev}, {what}:  " + ( "; ".join( came ) if came else f"nothing within {b.args.wait} s" ) + "." )
		for f in b.pushes[before:]: b.table( f )

def results( b ):
	b.start()
	for mid in ( 60, 80, 14, 34, 51, 70 ):
		b.ask( mid )
	seen = []
	def each( f ):
		b.table( f )
		if f.mid==61: seen.append( f )
	b.say( f"# Waiting {b.args.seconds} s for {b.args.count} result(s):  make them at the station." )
	end = time.monotonic()+b.args.seconds
	while len( seen )<b.args.count and time.monotonic()<end:
		b.pump( 1, each )
	b.say( f"# {len( seen )} result(s), tightening ids {[f.number( 'tightening id' ) for f in seen]}." )

def walk( b ):
	identity = b.start()
	station = identity.number( "station id" )
	b.say( f"# The connection's station id is {station}." )
	below, ids = 0, []
	for _ in range( b.args.steps ):
		rev, answer, _ = b.ask( 64, data=b"%010d" % ( below-1 if below else 0 ) )
		if answer.mid==4:
			b.say( f"# MID 0064 for {below-1 if below else 0} refused with code {answer.number( 'code' )}:  the walk's end." )
			break
		fields, stopped = answer.fields()
		id, made = answer.number( "tightening id" ), answer.number( "station id" )
		text = lambda name: fields.get( name, b"" ).decode( "latin-1" ).strip()
		whose = "no station id" if made is None else f"station {made} {text( 'station name' )!r}" + ( "" if made==station else ", another station's" )
		b.say( f"# asked {below-1 if below else 0}, answered {id} at revision {answer.rev}, time {text( 'time' )}, {whose}" + ( f"  !! {stopped}" if stopped else "" ) )
		if id is None:
			break
		ids.append( id )
		if id<=1:
			break#below it is 0, the latest.
		below = id
	gaps = [ a-c for a, c in zip( ids, ids[1:] ) ]
	b.say( f"# {len( ids )} result(s), newest first:  {ids};  the steps between them {gaps}." )

def hold( b ):
	if b.args.level=="app":
		b.start( min(b.args.revision, 5) )#below MID 0002's revision 6 nothing is numbered, and MID 0062 acknowledges.
		if b.numbered: raise SystemExit( "The link is numbered below revision 6." )
	else:
		b.start()
		if not b.numbered: raise SystemExit( "The controller does not number its messages:  --level app." )
	withheld = "MID 0062" if b.args.level=="app" else "MID 9997"
	rev, answer, _ = b.ask( 60 )
	if answer.mid==4: raise SystemExit( f"MID 0060 was refused:  code {answer.number( 'code' )}." )
	b.hold.add( 61 )
	b.say( f"# Results subscribed at {rev}, {withheld} withheld for {b.args.seconds} s:  make three tightenings at the station, a few seconds apart." )
	came = []
	b.pump( b.args.seconds, lambda f: came.append( f ) if f.mid==61 else None )
	b.say( f"# While {withheld} was withheld, {len( came )} MID 0061:  " + ", ".join( f"id {f.number( 'tightening id' )}{f' seq {f.seq}' if f.seq else ''} at {f.at-came[0].at:.1f} s" for f in came ) + "." )
	if not came:
		return
	b.hold.clear()
	if b.numbered: b.acked, b.expect = came[-1].seq, came[-1].seq%99+1
	b.acknowledge( came[-1] )
	after = []
	b.pump( 15, lambda f: after.append( f ) if f.mid==61 else None )
	b.say( f"# In the 15 s after one {withheld}, {len( after )} MID 0061:  " + ", ".join( f"id {f.number( 'tightening id' )} at {f.at-came[0].at:.1f} s" for f in after ) + "." )

def alarms( b ):
	b.start()
	rev, answer, _ = b.ask( 70 )
	if answer.mid==4: raise SystemExit( f"MID 0070 was refused:  code {answer.number( 'code' )}." )
	b.say( f"# Alarms subscribed at {rev}, for {b.args.seconds} s:  raise one at the controller, then acknowledge it there." )
	b.pump( b.args.seconds, b.table )

def clock( b ):
	b.start()
	rev, answer, _ = b.ask( 80 )
	if answer.mid==4: raise SystemExit( f"MID 0080 was refused:  code {answer.number( 'code' )}." )
	here = datetime.datetime.now()
	text = answer.fields()[0].get( "time", b"" ).decode( "latin-1" )
	b.say( f"# The controller's clock {text}, this box's {here.strftime( '%Y-%m-%d:%H:%M:%S' )} in its own zone." )
	try: b.say( f"# The controller is {round( ( datetime.datetime.strptime( text, '%Y-%m-%d:%H:%M:%S' )-here ).total_seconds() ):+d} s from this box, read in this box's zone." )
	except ValueError: b.say( "# !! The time names no date." )

def link( b ):
	b.start()
	if not b.numbered: raise SystemExit( "The controller does not number its messages:  nothing to check." )
	def exchange( seq, note ):
		b.write( 9999, 1, b"", seq, note )
		got = []
		try:
			while ( frame := b.read( 3 ) ) is not None: got.append( frame )
		except Closed:
			got.append( None )
		b.say( "#   " + ( ", then ".join( "the connection closed" if f is None else f"MID {f.mid:04d} seq {f.seq}" + ( f" code {f.number( 'code' )}" if f.mid==9998 else "" ) for f in got ) or "nothing within 3 s" ) + "." )
		return got
	seq = b.number
	b.say( f"# A keep-alive under sequence number {seq}:  MID 9997 carrying {seq%99+1}, then the controller's MID 9999 under its own number." )
	exchange( seq, "numbered" )
	b.number = seq%99+1
	b.say( f"# The same message again under {seq}, a retransmission:  the same MID 9997 and nothing else (§3.2.2)." )
	exchange( seq, "again" )
	if b.args.break_:
		wrong = ( b.number+4 )%99+1
		b.say( f"# One under {wrong} where {b.number} is next:  MID 9998, and the link is the integrator's to open again (§3.2.2)." )
		if None in exchange( wrong, "out of order" ):
			return "closed"

def ask( b ):
	b.start()
	rev, answer, refusals = b.ask( b.args.mid, b.args.rev, b.args.data.encode( "latin-1" ) )
	b.table( answer )
	b.pump( b.args.wait, b.table )

def frames( args ):
	"""Each frame a session read, from a gateway's log:  `(host:port) < '…'`, a byte that is no printable ASCII as \\xNN."""
	line = re.compile( r"\((?P<name>[^()]*)\) < '(?P<frame>.*)'" )
	captures, sessions, count = {}, set(), 0
	for text in open( args.log, encoding="utf-8", errors="replace" ):
		found = line.search( text )
		if not found or found["name"].startswith( "controller " ) or ( args.name and not re.search( args.name, found["name"] ) ):
			continue
		raw = re.sub( rb"\\x([0-9a-f]{2})", lambda m: bytes( [int( m[1], 16 )] ), found["frame"].encode( "latin-1" ) )
		name = args.label or found["name"]
		if name not in captures:
			captures[name] = Capture( args.out, name, args.per, log=False )
		captures[name].keep( Frame( raw ) )
		sessions.add( found["name"] )
		count += 1
	print( f"{count} frame(s) of {len( sessions )} session(s) read from {args.log}." )

def main():
	common = argparse.ArgumentParser( add_help=False )
	common.add_argument( "--out", default="open-protocol-capture", help="where the log and the frames go (default ./open-protocol-capture)" )
	common.add_argument( "--label", help="what sent the frames, the start of each file's name:  pf6000-3.10-s1 (default HOST-PORT)" )
	common.add_argument( "--per", type=int, default=4, help="the frames kept of each MID and revision (default 4)" )
	dial = argparse.ArgumentParser( add_help=False, parents=[common] )
	dial.add_argument( "host" )
	dial.add_argument( "port", type=int )
	dial.add_argument( "--revision", type=int, default=6, help="MID 0001's first revision (default 6, the driver's)" )
	dial.add_argument( "--timeout", type=float, default=10, help="seconds a request waits for its answer (default 10)" )
	dial.add_argument( "--spaces", action="store_true", help="a header's unused fields blank, as the specification's examples, not digits as the driver's" )
	parser = argparse.ArgumentParser( description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter )
	modes = parser.add_subparsers( dest="mode", required=True )
	for name in ( "identify", "ladder", "clock" ):
		modes.add_parser( name, parents=[dial] )
	m = modes.add_parser( "first", parents=[dial] )
	m.add_argument( "--wait", type=float, default=3, help="seconds after each MID 0005 (default 3)" )
	m = modes.add_parser( "results", parents=[dial] )
	m.add_argument( "--count", type=int, default=3 )
	m.add_argument( "--seconds", type=float, default=300 )
	m = modes.add_parser( "walk", parents=[dial] )
	m.add_argument( "--steps", type=int, default=10, help="the MID 0064 requests at most (default 10)" )
	m = modes.add_parser( "hold", parents=[dial] )
	m.add_argument( "--level", choices=( "app", "link" ), default="app", help="what is withheld:  MID 0062 on a link started below revision 6, or MID 9997 on a numbered one" )
	m.add_argument( "--seconds", type=float, default=60 )
	m = modes.add_parser( "alarms", parents=[dial] )
	m.add_argument( "--seconds", type=float, default=300 )
	m = modes.add_parser( "link", parents=[dial] )
	m.add_argument( "--break", dest="break_", action="store_true", help="also a number out of order, which costs the link" )
	m = modes.add_parser( "ask", parents=[dial] )
	m.add_argument( "mid", type=int )
	m.add_argument( "rev", type=int, nargs="?", default=1 )
	m.add_argument( "data", nargs="?", default="" )
	m.add_argument( "--wait", type=float, default=3, help="seconds to listen after the answer (default 3)" )
	m = modes.add_parser( "frames", parents=[common] )
	m.add_argument( "log" )
	m.add_argument( "--name", help="a regular expression the session's name, host:port, is to match" )
	args = parser.parse_args()
	if args.mode=="frames":
		return frames( args )
	b = Bench( args )
	closed = None
	try:
		closed = globals()[args.mode]( b )
	except Closed:
		b.say( "# !! The controller closed the connection." )
		closed = "closed"
	except KeyboardInterrupt:
		b.say( "# Stopped by hand." )
	finally:#MID 0003 on every way out the link is still up by, a mode's SystemExit among them.
		if closed!="closed":
			b.stop()

main()