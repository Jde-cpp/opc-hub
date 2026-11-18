#!/usr/bin/env python3
# The public demo's smoke, its socket half (docs/demo/plan.md Phase 10;  smoke.sh runs it, README.md beside this file is the
# runbook):  what the floor page does on the gateway's socket.  A guest's session from /serverSettings' response header, /opc
# opened with it, an OeeSubscribe for each machine with its state read from the ack, a push awaited on the Filler, which counts
# every second, and the floor's other subscriptions, the two lines' and the site's.
#   smoke.py [--url <site>] [--resolve <address>] [--insecure] [--live <seconds>] [--margin <seconds>]
#     --url, --resolve, --insecure   as smoke.sh's
#     --live      the gateway's /gateway/oee/live, how often at most a machine's figures are pushed     default: 1
#     --margin    what the Filler's push is given beyond it                                           default: 10
#   smoke.py guests <n> [--url <hub>] [--resolve <address>] [--insecure] [--connection <slug>] [--hold <seconds>]
#     the soak's load (README.md, The soak), on the box against the hub itself, http://127.0.0.1:1967 by default:  n visitors
#     on a PLC's node browser, plc1's by default, each a guest's session at an address of its own, a socket and a subscription
#     to the machine's five variables, held for --hold, 60 s by default.  It says how many were taken and what refused the rest.
# Each check is a line, `ok` or `FAILED`, as smoke.sh's;  the exit is 0 when none failed.
# Python on apt's python3-websockets and python3-protobuf.  The modules under proto/ beside this file are build-demo.sh's, made
# from the gateway's proto files by apt's protoc, whose code the apt runtime reads.
import argparse, asyncio, http.client, json, os, re, socket, ssl, sys, time
from urllib.parse import urlsplit

here = os.path.dirname( os.path.abspath(__file__) )
sys.path.insert( 0, os.path.join(here, 'proto') )
try:
	import websockets
	from Opc import Common_pb2 as Common, FromClient_pb2 as FromClient, FromServer_pb2 as FromServer
except ImportError as e:
	print( f"FAILED  the socket's half has no {e.name}:  apt install python3-websockets python3-protobuf, and proto/ beside smoke.py is the demo tarball's" )
	sys.exit( 1 )

STATES = { FromServer.OeeRunning: 'running', FromServer.OeeStopped: 'stopped', FromServer.OeeUnknown: 'unknown' }
NODES = range( 6001, 6006 ) #demo.NodeSet2.xml's Machine variables, status to speed, in urn:jde:demo
DEMO_NS = 'urn:jde:demo' #its index is the PLC's to say:  a nodeset loaded before it moves it (reviews-issue/487-demo-ops.md #8)
failed = False
def ok( what ): print( f"ok      {what}", flush=True )
def fail( what ):
	global failed
	failed = True
	print( f"FAILED  {what}", flush=True )

class Site:
	def __init__( self, url, resolve=None, insecure=False ):
		u = urlsplit( url )
		self.secure = u.scheme=='https'
		self.host = u.hostname
		self.port = u.port or ( 443 if self.secure else 80 )
		self.authority = u.netloc
		self.address = resolve or self.host #--resolve dials the name at an address, the certificate still the name's
		self.context = ssl.create_default_context() if self.secure else None
		if self.secure and insecure:
			self.context.check_hostname = False
			self.context.verify_mode = ssl.CERT_NONE
	def dial( self ): return socket.create_connection( (self.address, self.port), timeout=30 )
	#the status, the response's headers and its body
	def call( self, method, path, headers={}, body=None ):
		sock = self.dial()
		try:
			if self.secure:
				sock = self.context.wrap_socket( sock, server_hostname=self.host )
			c = http.client.HTTPConnection( self.host, self.port )
			c.sock = sock
			c.request( method, path, body, {'Host': self.authority, 'Connection': 'close', **headers} )
			r = c.getresponse()
			return r.status, r.headers, r.read()
		finally:
			sock.close()
	#a session the hub mints for a call that names none:  the guest's
	def guest( self, headers={} ):
		_, answered, _ = self.call( 'GET', '/serverSettings', headers )
		return answered.get( 'Authorization' )
	#the answer's data, each query's answered:  an error, or a query answered null, raises with the answer's start
	def ql( self, session, query ):
		status, _, body = self.call( 'POST', '/graphql', {'Content-Type': 'application/json', 'Authorization': session}, json.dumps({'query': query}) )
		answer = json.loads( body ) if status==200 else None
		data = answer.get( 'data' ) if isinstance( answer, dict ) else None
		if not isinstance( data, dict ) or answer.get( 'errors' ) or any( v is None for v in data.values() ):
			raise RuntimeError( f"{status} {body[:200].decode(errors='replace')}" )
		return data
	async def socket( self, path, headers={} ):
		args = { 'sock': self.dial(), 'max_size': None, 'open_timeout': 30 }
		if self.secure:
			args |= { 'ssl': self.context, 'server_hostname': self.host }
		if headers: #websockets 14 renamed it, and noble's is 10
			args['additional_headers' if int(websockets.__version__.split('.')[0])>=14 else 'extra_headers'] = headers
		return await websockets.connect( f"{'wss' if self.secure else 'ws'}://{self.authority}{path}", **args )

#The gateway's socket (apps/OpcGateway/src/types/proto):  each frame a Transmission of messages, a request's answers under its id.
class Gateway:
	def __init__( self, ws ):
		self.ws = ws
		self.id = 0
		self.inbox = {} #a request's id, 0 the gateway's own, to what came under it
	async def send( self, **value ):
		self.id += 1
		await self.ws.send( FromClient.Transmission(messages=[FromClient.Message(request_id=self.id, **value)]).SerializeToString() )
		return self.id
	#reads until `done()` or the timeout:  whether it was done
	async def read( self, done, timeout ):
		deadline = time.monotonic()+timeout
		while not done():
			left = deadline-time.monotonic()
			if left<=0:
				return False
			try:
				frame = await asyncio.wait_for( self.ws.recv(), left )
			except asyncio.TimeoutError:
				return False
			for m in FromServer.Transmission.FromString( frame ).messages:
				self.inbox.setdefault( m.request_id, [] ).append( m )
		return True
	#the first of a request's answers, as its kind and the message:  `complete` for one with no value, None for none
	def answer( self, id ):
		if id not in self.inbox:
			return None, None
		m = self.inbox[id][0]
		return m.WhichOneof( 'Value' ) or 'complete', m
	#the socket's own ack, then the session, which every subscription is asked under:  what refused it, or None
	async def open( self, session ):
		if not await self.read( lambda: 0 in self.inbox, 30 ) or self.answer( 0 )[0]!='ack':
			return "the gateway sent no ack"
		id = await self.send( session_id=session )
		await self.read( lambda: id in self.inbox, 30 )
		kind, m = self.answer( id )
		return None if kind=='complete' else refusal( kind, m )

def refusal( kind, m ):
	return f"{m.exception.what} ({m.exception.status_code})" if kind=='exception' else "no answer" if kind is None else f"answered {kind}"

#How long a machine running in planned time has gone with no count, where that is past what its cycle allows, else None:  its
#PLC, or the collector's session on it, is down, which its engine learns only once the collector is back, the machine
#reading as it last was until then.  From the later of its last count and its state's start, in the engine's own time.
def quiet( status, cycle ):
	if status.state!=FromServer.OeeRunning or not status.planned or not status.HasField( 'figures' ):
		return None
	age = ( status.figures.end.ToNanoseconds()-max(status.last_count.ToNanoseconds(), status.since.ToNanoseconds()) )/1e9
	return age if age>30+3*cycle/1000 else None

async def smoke( site, live, margin ):
	try:
		session = site.guest()
	except OSError as e:
		return fail( f"the socket's half:  {site.authority} does not answer /serverSettings:  {e}" )
	if not session:
		return fail( "the socket's half:  a call with no session is given none" )
	try:
		plant = site.ql( session, 'oeeMachines{ id slug idealCycle } oeeLines{ id slug }' )
	except (OSError, RuntimeError, ValueError, KeyError) as e:
		return fail( f"the socket's half:  the guest does not read the plant:  {e}" )
	try:
		ws = await site.socket( '/opc' )
	except Exception as e:
		return fail( f"the gateway's socket, /opc, does not open:  {e}" )
	try:
		gateway = Gateway( ws )
		refused = await gateway.open( session )
		if refused:
			return fail( f"the gateway's socket takes the guest's session:  {refused}" )
		ok( "the gateway's socket, /opc, opens and takes the guest's session" )

		machines = { m['slug']: await gateway.send( oee_subscribe=FromClient.OeeSubscribe(machine=m['id']) ) for m in plant['oeeMachines'] }
		await gateway.read( lambda: all( id in gateway.inbox for id in machines.values() ), 30 )
		open_ = 0
		waited = None #the Filler's status, where it is running in planned time
		for machine in plant['oeeMachines']:
			slug, id = machine['slug'], machines[machine['slug']]
			kind, m = gateway.answer( id )
			if kind!='oee_subscribe_ack':
				fail( f"the {slug}'s subscription is acked:  {refusal(kind, m)}" )
				continue
			open_ += 1
			status = m.oee_subscribe_ack.status
			state = STATES.get( status.state, 'unknown' )
			silent = quiet( status, machine.get('idealCycle') or 0 )
			if state=='unknown':
				fail( f"the {slug}'s ack carries its state:  unknown, no engine running it" )
			elif silent:
				fail( f"the {slug} counts while it runs:  no count for {silent:.0f} s, so its PLC, or the collector's session on it, is down" )
			else:
				ok( f"the {slug}'s subscription is acked, the machine {state}{'' if status.planned else ', outside planned time'}" )
			if slug=='filler' and state=='running' and status.planned:
				waited = id
		#A push comes only when something changed:  a machine in a stop or a break is not waited on.  The Filler counts every second.
		if waited:
			pushed = lambda: any( m.WhichOneof('Value')=='oee_status' for m in gateway.inbox[waited][1:] )
			await gateway.read( pushed, live+margin )
			( ok if pushed() else fail )( f"the Filler's figures are pushed within {live+margin:g} s" )
		elif 'filler' in machines:
			print( "the Filler is in a stop or a break:  its push is not waited on" )
		else:
			fail( "the plant has a machine `filler`, whose push is waited on" )

		lines = { l['slug']: await gateway.send( oee_subscribe=FromClient.OeeSubscribe(line=l['id']) ) for l in plant['oeeLines'] }
		await gateway.read( lambda: all( id in gateway.inbox for id in lines.values() ), 30 )
		for slug, id in lines.items():
			kind, m = gateway.answer( id )
			if kind!='oee_subscribe_ack':
				fail( f"the line {slug}'s subscription is acked:  {refusal(kind, m)}" )
				continue
			open_ += 1
			count = m.oee_subscribe_ack.status.machines
			total = count.running+count.stopped+count.unknown
			( ok if total else fail )( f"the line {slug}'s subscription is acked, {count.running} of {total} running" )
		asked = len( machines )+len( lines )
		( ok if open_==asked and asked else fail )( f"the floor's subscriptions are open, {open_} of {asked}:  a machine's each, a line's each and the site's" )
	except websockets.ConnectionClosed as e:
		fail( f"the gateway's socket stays open:  {e}" )
	except Exception as e: #a check's own line, not a traceback the watch's log buries the cause under
		fail( f"the socket's half:  {type(e).__name__}:  {e}" )
	finally:
		await ws.close()

#One visitor on a PLC's node browser.  The hub reads a visitor's address from X-Forwarded-For where the caller is its trusted
#front, which a caller on the box's loopback is, so each has an address of its own, as visitors do, and a session tied to it.
#The first asks the PLC's namespaces with its own session, so the lookup costs the PLC no session of its own, and `ns` gives the
#rest urn:jde:demo's index.
async def visitor( site, i, connection, hold, tally, gate, ns ):
	headers = { 'X-Forwarded-For': f"10.{i>>16&255}.{i>>8&255}.{i&255}" }
	ws = None
	try:
		async with gate: #the opening, a few at a time
			session = await asyncio.to_thread( site.guest, headers )
			if not session:
				raise RuntimeError( "no session" )
			tally['sessions'] += 1
			if i==1:
				spaces = ( await asyncio.to_thread(site.ql, session, f'namespaces( opc:"{connection}" ){{ index uri }}') )['namespaces']
				index = next( (s['index'] for s in spaces if s['uri']==DEMO_NS), None )
				if index is None:
					raise RuntimeError( f"{connection} has no namespace {DEMO_NS}" )
				ns.set_result( index )
			index = await ns
			ws = await site.socket( '/opc', headers )
			gateway = Gateway( ws )
			refused = await gateway.open( session )
			if refused:
				raise RuntimeError( refused )
			tally['sockets'] += 1
			id = await gateway.send( subscribe=FromClient.Subscribe(opc_id=connection, nodes=[Common.NodeId(namespace_index=index, numeric=n) for n in NODES]) )
			await gateway.read( lambda: id in gateway.inbox, 60 )
			kind, m = gateway.answer( id )
			if kind!='subscription_ack':
				raise RuntimeError( refusal(kind, m) )
			bad = [ r.status_code for r in m.subscription_ack.results if r.status_code ]
			if bad:
				raise RuntimeError( f"an item refused {bad[0]:#x}" )
			tally['subscribed'] += 1
		await gateway.read( lambda: False, hold )
		tally['values'] += sum( 1 for answers in gateway.inbox.values() for m in answers if m.WhichOneof('Value')=='node_values' )
	except Exception as e:
		if i==1 and not ns.done():
			ns.set_exception( RuntimeError(f"{connection}'s namespaces were not read:  {e}") )
			ns.exception() #retrieved:  the rest raise it, and none may be left to
		what = re.sub( r'^\[[0-9a-f]+\]', '', str(e) ) or type( e ).__name__ #without the client's handle, so one cause is one line
		tally['refused'][what] = tally['refused'].get( what, 0 )+1
	finally:
		if ws:
			await ws.close()

async def guests( site, n, connection, hold ):
	tally = { 'sessions': 0, 'sockets': 0, 'subscribed': 0, 'values': 0, 'refused': {} }
	gate, ns = asyncio.Semaphore( 20 ), asyncio.get_running_loop().create_future()
	await asyncio.gather( *( visitor(site, i+1, connection, hold, tally, gate, ns) for i in range(n) ) )
	print( f"{n} visitors on {connection}:  {tally['sessions']} sessions, {tally['sockets']} sockets, {tally['subscribed']} subscribed, {tally['values']} values in {hold:g} s" )
	for what, count in sorted( tally['refused'].items(), key=lambda r: -r[1] ):
		print( f"  {count} refused:  {what}" )
	if tally['subscribed']!=n:
		global failed
		failed = True

def main():
	p = argparse.ArgumentParser( add_help=False )
	p.add_argument( 'mode', nargs='?', choices=['guests'] )
	p.add_argument( 'n', nargs='?', type=int )
	p.add_argument( '--url' )
	p.add_argument( '--resolve' )
	p.add_argument( '--insecure', action='store_true' )
	p.add_argument( '--live', type=float, default=1 )
	p.add_argument( '--margin', type=float, default=10 )
	p.add_argument( '--connection', default='plc1' )
	p.add_argument( '--hold', type=float, default=60 )
	p.add_argument( '-h', '--help', action='store_true' )
	a = p.parse_args()
	if a.help or ( a.mode=='guests' and not a.n ):
		with open( __file__ ) as f:
			print( ''.join( l[2:] if l.startswith('# ') else l[1:] for l in f.readlines()[1:16] ), end='' )
		sys.exit( 0 if a.help else 2 )
	if a.mode=='guests':
		asyncio.run( guests(Site((a.url or 'http://127.0.0.1:1967').rstrip('/'), a.resolve, a.insecure), a.n, a.connection, a.hold) )
	else:
		asyncio.run( smoke(Site((a.url or 'https://opchub.trypoply.com').rstrip('/'), a.resolve, a.insecure), a.live, a.margin) )
	sys.exit( 1 if failed else 0 )

if __name__=='__main__':
	main()